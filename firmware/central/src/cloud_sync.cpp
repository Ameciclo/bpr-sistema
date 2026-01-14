#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include "bike_manager.h"
#include "binary_structs.h"
#include "ble_server.h"
#include "bpr_json_helper.h"
#include "buffer_manager.h"
#include "cloud_sync.h"
#include "config_credentials.h"
#include "config_manager.h"
#include "constants.h"
#include "endpoints.h"
#include "time_sync.h"

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

    uint32_t remoteLastUpdate = response.toInt();
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
    // Ler last_update do arquivo local bike_registry.bin
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

    BikeRegistryData registry;
    size_t bytesRead = file.readBytes((char*)&registry, sizeof(registry));
    file.close();

    if (bytesRead != sizeof(registry))
    {
        return true;
    }

    return checkLastUpdateTime(Endpoints::getBikeRegistryVersion(), registry.last_update, "Bike registry");
}

bool CloudSync::needsBikeConfigsUpdate()
{
    // Com download individual, sempre verificar se há bikes para atualizar
    return (BikeManager::getConnectedCount() > 0);
}

void CloudSync::updateConfigFromFirebase(const String &csvData)
{
    Serial.println("🔄 Updating config from Firebase CSV...");

    // Usar updateFromCSV que já existe no ConfigManager
    if (configManager.updateFromCSV(csvData))
    {
        Serial.println("✅ Config updated from Firebase CSV and saved locally");
    }
    else
    {
        Serial.println("❌ Failed to update config from Firebase CSV");
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

    // HTTP OK - processar resposta JSON array
    String jsonArray = http.getString();
    http.end();

    // Converter JSON array para CSV: [90,60,15,...] -> 90,60,15,...
    jsonArray.replace("[", "");
    jsonArray.replace("]", "");
    
    // Aplicar configuração válida
    updateConfigFromFirebase(jsonArray);

    // Sucesso
    Serial.printf("✅ Central config downloaded successfully\n");
    Serial.printf("   Sync interval: %d seconds\n", configManager.getConfig().intervals.sync_sec);
    return true;
}

bool CloudSync::downloadBikeRegistryData()
{
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

    String csvData = http.getString();
    http.end();

    // Parse JSON array: ["bike01",2,"AA:BB:CC:DD:EE:01",1733400000,1733459000,"bike02",2,"AA:BB:CC:DD:EE:02",1733400000,1733458000]
    BikeRegistryData registry;
    memset(&registry, 0, sizeof(registry));
    registry.last_update = time(nullptr);
    
    // Remove brackets and quotes
    csvData.replace("[", "");
    csvData.replace("]", "");
    csvData.replace("\"", ""); // Remove quotes
    
    int bikeIndex = 0;
    int startPos = 0;
    int fieldIndex = 0;
    
    while (startPos < csvData.length() && bikeIndex < 10) {
        int commaPos = csvData.indexOf(',', startPos);
        String value = (commaPos == -1) ? csvData.substring(startPos) : csvData.substring(startPos, commaPos);
        
        switch (fieldIndex % 5) {
            case 0: // bike_id
                registry.bikes[bikeIndex] = bikeIdToInt(value);
                break;
            case 1: // status
                registry.statuses[bikeIndex] = value.toInt();
                break;
            case 2: // mac_address
                registry.mac_addresses[bikeIndex] = macStringToInt(value);
                break;
            case 3: // created_at
                registry.created_at[bikeIndex] = value.toInt();
                break;
            case 4: // last_seen
                registry.last_seen[bikeIndex] = value.toInt();
                bikeIndex++; // Próxima bike
                break;
        }
        
        if (commaPos == -1) break;
        startPos = commaPos + 1;
        fieldIndex++;
    }
    
    registry.bike_count = bikeIndex;

    // Save binary struct
    File file = LittleFS.open(BIKE_REGISTRY_FILE, "w");
    if (file)
    {
        file.write((uint8_t*)&registry, sizeof(registry));
        file.close();
        Serial.println("💾 Bike registry saved locally");
    }

    Serial.printf("✅ Bike registry downloaded: %d bikes\n", bikeIndex);
    return true;
}

bool CloudSync::downloadBikeConfigs()
{
    // Download individual otimizado para ESP32C3
    Serial.println("🔄 Downloading bike configs individually...");
    
    // Baixar apenas para bikes conectadas (economia de RAM)
    int connectedCount = BikeManager::getConnectedCount();
    if (connectedCount == 0) {
        Serial.println("📝 No bikes connected - skipping config download");
        return true;
    }
    
    // TODO: Implementar lista de bikes conectadas
    // Por enquanto, tentar baixar configs de exemplo
    String testBikes[] = {"bpr-012345", "bpr-123456", "bpr-234567"};
    int successCount = 0;
    
    for (int i = 0; i < 3; i++) {
        if (BikeManager::downloadSingleBikeConfig(testBikes[i])) {
            successCount++;
        }
        
        // Pequeno delay para não sobrecarregar
        delay(100);
    }
    
    Serial.printf("✅ Downloaded configs for %d/%d bikes\n", successCount, 3);
    return (successCount > 0); // Sucesso se pelo menos 1 config foi baixada
}

bool CloudSync::uploadBufferData()
{
    DynamicJsonDocument doc(4096);

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

    DynamicJsonDocument doc(HEARTBEAT_BUFFER);
    doc["timestamp"] = time(nullptr);
    doc["bikes_connected"] = BikeManager::getConnectedCount();
    doc["heap"] = ESP.getFreeHeap();
    doc["uptime"] = millis() / 1000;

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