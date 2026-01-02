#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include "bike_manager.h"
#include "ble_server.h"
#include "buffer_manager.h"
#include "cloud_sync.h"
#include "config_credentials.h"
#include "config_manager.h"
#include "constants.h"
#include "endpoints.h"
#include "time_sync.h"
#include "bpr_json_helper.h"

extern ConfigManager configManager;
extern ConfigCredentials configCredentials;
extern BufferManager bufferManager;

// Static members
bool CloudSync::syncInProgress = false;
SyncResult CloudSync::currentResult = SyncResult::SUCCESS;
static uint32_t syncStartTime = 0;

SyncResult CloudSync::enter()
{
    Serial.println("📡 Entering CLOUD_SYNC mode");
    syncStartTime = millis();

    // BLE já está marcado como BUSY pelo bike_pairing::exit()
    // Não precisamos parar o BLE

    syncInProgress = true;
    currentResult = SyncResult::IN_PROGRESS;

    return currentResult;
}

SyncResult CloudSync::update()
{
    if (!syncInProgress)
    {
        return currentResult;
    }

    // WiFi connection PRIMEIRO
    if (!connectWiFi())
    {
        Serial.println("❌ WiFi connection failed");
        WiFi.disconnect(true);
        syncInProgress = false;
        currentResult = SyncResult::FAILURE;
        Serial.println("❌ Sync failed");
        return currentResult;
    }

    // Sync horário
    TimeSync::init();

    // UPLOADS (dados locais -> Firebase)
    if (!uploadBufferData())
    {
        WiFi.disconnect(true);
        syncInProgress = false;
        currentResult = SyncResult::FAILURE;
        Serial.println("❌ Sync failed");
        return currentResult;
    }

    if (!uploadHeartbeat())
    {
        WiFi.disconnect(true);
        syncInProgress = false;
        currentResult = SyncResult::FAILURE;
        Serial.println("❌ Sync failed");
        return currentResult;
    }

    // DOWNLOADS (Firebase -> local)
    if (!downloadCentralConfig())
    {
        WiFi.disconnect(true);
        syncInProgress = false;
        currentResult = SyncResult::FAILURE;
        Serial.println("❌ Sync failed");
        return currentResult;
    }

    if (!downloadBikeRegistryData())
    {
        WiFi.disconnect(true);
        syncInProgress = false;
        currentResult = SyncResult::FAILURE;
        Serial.println("❌ Sync failed");
        return currentResult;
    }

    if (!downloadBikeConfigs())
    {
        WiFi.disconnect(true);
        syncInProgress = false;
        currentResult = SyncResult::FAILURE;
        Serial.println("❌ Sync failed");
        return currentResult;
    }

    // Marcar que o primeiro sync foi concluído
    if (configCredentials.isFirstSync())
    {
        configCredentials.setFirstSyncCompleted();
        configCredentials.saveCredentials();
        Serial.println("✅ First sync completed - flag updated");
    }

    // Sucesso - finalizar sync (WiFi será desconectado no exit())
    syncInProgress = false;
    currentResult = SyncResult::SUCCESS;
    Serial.println("✅ Sync complete");
    return currentResult;
}

void CloudSync::exit()
{
    WiFi.disconnect(true);

    // Voltar BLE para status READY
    BPRBLEServer::setBusyStatus(false);

    Serial.println("🔚 Exiting CLOUD_SYNC mode - BLE back to READY");
}

void CloudSync::printStatus()
{
    Serial.printf("😲 Bikes conectadas: %d | 💾 Heap: %d bytes\n", BikeManager::getConnectedCount(), ESP.getFreeHeap());
}

bool CloudSync::connectWiFi()
{
    const CredentialsConfig &creds = configCredentials.getCredentials();
    const CentralConfig &config = configManager.getConfig();

    WiFi.mode(WIFI_STA);
    WiFi.begin(creds.wifi_ssid, creds.wifi_password);

    uint32_t startTime = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        if (millis() - startTime > config.timeouts.wifi_sec * 1000)
        {
            return false;
        }
        delay(500);
        Serial.print(".");
    }

    Serial.printf("\n📶 WiFi connected: %s\n", WiFi.localIP().toString().c_str());
    return true;
}

bool CloudSync::checkLastUpdateTime(const String &url, uint32_t localLastUpdate, const String &componentName)
{
    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK)
    {
        Serial.printf("⚠️ %s last_update check failed: HTTP %d\n", componentName.c_str(), httpCode);
        http.end();
        return true; // Se falhar, baixa por segurança
    }

    String response = http.getString();
    http.end();

    DynamicJsonDocument doc(CONFIG_VERSION_BUFFER);
    if (deserializeJson(doc, response) != DeserializationError::Ok)
    {
        Serial.printf("⚠️ %s last_update parse failed\n", componentName.c_str());
        return true; // Se falhar, baixa por segurança
    }

    // Buscar por "last_update" no JSON
    if (!doc.containsKey("last_update"))
    {
        Serial.printf("⚠️ %s missing last_update field\n", componentName.c_str());
        return true; // Se não tem campo, baixa por segurança
    }

    uint32_t remoteLastUpdate = doc["last_update"];
    bool needsUpdate = (remoteLastUpdate > localLastUpdate);

    Serial.printf("📋 %s last_update check: local=%d, remote=%d -> %s\n",
                  componentName.c_str(), localLastUpdate, remoteLastUpdate,
                  needsUpdate ? "UPDATE NEEDED" : "UP TO DATE");

    return needsUpdate;
}

bool CloudSync::needsConfigUpdate()
{
    const CentralConfig &config = configManager.getConfig();
    return checkLastUpdateTime(Endpoints::getConfigVersion(), config.last_update, "Config");
}

bool CloudSync::needsBikeRegistryUpdate()
{
    // Ler last_update do arquivo local bike_registry.json
    if (!LittleFS.exists(BIKE_REGISTRY_FILE))
    {
        Serial.println("📋 No local bike registry - needs download");
        return true;
    }

    File file = LittleFS.open(BIKE_REGISTRY_FILE, "r");
    if (!file)
    {
        return true;
    }

    DynamicJsonDocument doc(CONFIG_VERSION_BUFFER);
    if (deserializeJson(doc, file) != DeserializationError::Ok)
    {
        file.close();
        return true;
    }
    file.close();

    uint32_t localLastUpdate = doc["last_update"] | 0;
    return checkLastUpdateTime(Endpoints::getBikeRegistryVersion(), localLastUpdate, "Bike registry");
}

bool CloudSync::needsBikeConfigsUpdate()
{
    // Ler last_update do arquivo local bike_configs.json
    if (!LittleFS.exists(BIKE_CONFIGS_FILE))
    {
        Serial.println("📋 No local bike configs - needs download");
        return true;
    }

    File file = LittleFS.open(BIKE_CONFIGS_FILE, "r");
    if (!file)
    {
        return true;
    }

    DynamicJsonDocument doc(CONFIG_VERSION_BUFFER);
    if (deserializeJson(doc, file) != DeserializationError::Ok)
    {
        file.close();
        return true;
    }
    file.close();

    uint32_t localLastUpdate = doc["last_update"] | 0;
    return checkLastUpdateTime(Endpoints::getBikeConfigsVersion(), localLastUpdate, "Bike configs");
}

void CloudSync::updateConfigFromFirebase(const DynamicJsonDocument &firebaseConfig)
{
    Serial.println("🔄 Updating config from Firebase...");

    // Converter DynamicJsonDocument para String
    String jsonString;
    serializeJson(firebaseConfig, jsonString);

    // Usar updateFromJson que já existe no ConfigManager
    if (configManager.updateFromJson(jsonString))
    {
        Serial.println("✅ Config updated from Firebase and saved locally");
    }
    else
    {
        Serial.println("❌ Failed to update config from Firebase");
    }
}

bool CloudSync::downloadCentralConfig()
{
    // Verificar se precisa atualizar antes de baixar
    if (!needsConfigUpdate())
    {
        Serial.println("📋 Config already up to date - skipping download");
        return true;
    }

    HTTPClient http;
    String url = Endpoints::getCentralConfig();

    Serial.printf("🔄 Downloading central config from Firebase...\n");

    http.begin(url);
    int httpCode = http.GET();

    // Early return se HTTP falhar
    if (httpCode != HTTP_CODE_OK)
    {
        Serial.printf("🚨 Central config download failed: HTTP %d\n", httpCode);
        http.end();
        return false;
    }

    // HTTP OK - processar resposta
    String json = http.getString();
    http.end();

    // Parse e validação
    DynamicJsonDocument doc(CONFIG_JSON_BUFFER_SIZE);
    if (deserializeJson(doc, json) != DeserializationError::Ok)
    {
        Serial.println("🚨 Config JSON parse failed");
        return false;
    }

    // Aplicar configuração válida
    updateConfigFromFirebase(doc);

    // Sucesso
    Serial.printf("✅ Central config downloaded successfully\n");
    Serial.printf("   Sync interval: %d seconds\n", configManager.getConfig().intervals.sync_sec);
    return true;
}

bool CloudSync::downloadBikeRegistryData()
{
    // Verificar se precisa atualizar antes de baixar
    if (!needsBikeRegistryUpdate())
    {
        Serial.println("🚲 Bike registry already up to date - skipping download");
        return true;
    }

    HTTPClient http;
    String url = Endpoints::getBikeRegistry();

    Serial.println("🔄 Downloading bike registry from Firebase...");

    http.begin(url);
    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK)
    {
        Serial.printf("❌ Bike registry download failed: HTTP %d\n", httpCode);
        http.end();
        return false;
    }

    String json = http.getString();
    http.end();

    DynamicJsonDocument doc(BIKE_REGISTRY_BUFFER);
    DeserializationError error = deserializeJson(doc, json);

    if (error)
    {
        Serial.printf("❌ Registry parse error: %s\n", error.c_str());
        return false;
    }

    // Adicionar last_update se não existir
    if (!doc.containsKey("last_update"))
    {
        doc["last_update"] = time(nullptr);
    }

    // Salvar no arquivo local
    File file = LittleFS.open(BIKE_REGISTRY_FILE, "w");
    if (file)
    {
        serializeJson(doc, file);
        file.close();
        Serial.println("💾 Bike registry saved locally");
    }

    Serial.println("✅ Bike registry downloaded successfully!");
    return true;
}

bool CloudSync::downloadBikeConfigs()
{
    // Verificar se precisa atualizar antes de baixar
    if (!needsBikeConfigsUpdate())
    {
        Serial.println("⚙️ Bike configs already up to date - skipping download");
        return true;
    }

    HTTPClient http;
    String url = Endpoints::getBikeConfigs();

    Serial.println("🔄 Downloading bike configs from Firebase...");

    http.begin(url);
    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK)
    {
        Serial.printf("❌ Bike configs download failed: HTTP %d\n", httpCode);
        http.end();
        return false;
    }

    String json = http.getString();
    http.end();

    // Parse e adicionar last_update se necessário
    DynamicJsonDocument doc(BIKE_REGISTRY_BUFFER);
    if (deserializeJson(doc, json) == DeserializationError::Ok)
    {
        if (!doc.containsKey("last_update"))
        {
            doc["last_update"] = time(nullptr);
        }

        // Salvar no arquivo local
        File file = LittleFS.open(BIKE_CONFIGS_FILE, "w");
        if (file)
        {
            serializeJson(doc, file);
            file.close();
            Serial.println("💾 Bike configs saved locally");
        }
    }

    // Salvar configs no buffer_manager para distribuição
    bufferManager.addConfigData("bike_configs", json);

    Serial.println("✅ Bike configs downloaded successfully!");
    return true;
}

bool CloudSync::uploadBufferData()
{
    DynamicJsonDocument doc(UPLOAD_BATCH_BUFFER);

    // Early return se não há dados
    if (!bufferManager.getDataForUpload(doc))
    {
        Serial.println("📝 No buffer data to upload");
        return true; // Não ter dados não é erro
    }

    HTTPClient http;
    String url = Endpoints::getBufferData();

    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    String jsonString;
    serializeJson(doc, jsonString);

    int httpCode = http.PATCH(jsonString);

    // Early return se falhar
    if (httpCode != HTTP_CODE_OK)
    {
        Serial.printf("❌ Buffer upload failed: HTTP %d\n", httpCode);
        Serial.printf("   URL: %s\n", url.c_str());
        http.end();
        return false;
    }

    // Sucesso
    bufferManager.markAsConfirmed();
    Serial.printf("📤 Buffer data uploaded: %d bytes\n", jsonString.length());
    http.end();
    return true;
}

bool CloudSync::uploadHeartbeat()
{
    HTTPClient http;

    String url = Endpoints::getHeartbeat();

    DynamicJsonDocument doc(CENTRAL_HEARTBEAT_BUFFER);
    BPRJsonHelper::addHeartbeatFields(doc);

    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    String jsonString;
    serializeJson(doc, jsonString);

    int httpCode = http.PUT(jsonString);

    bool success = (httpCode == HTTP_CODE_OK);

    if (success)
    {
        Serial.printf("💓 Heartbeat: Bikes: %d | Heap: %d\n",
                      BikeManager::getConnectedCount(), ESP.getFreeHeap());
    }
    else
    {
        Serial.printf("❌ Heartbeat falhou: HTTP %d\n", httpCode);
        Serial.printf("   URL: %s\n", url.c_str());
        Serial.printf("   Payload: %s\n", jsonString.c_str());
    }

    http.end();
    return success;
}