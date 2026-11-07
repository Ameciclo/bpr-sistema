#include <ArduinoJson.h>
#include <CRC32.h>
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

// O02: nunca loga a URL com credencial — só o caminho
static String redactUrl(const String& url)
{
    int cut = url.indexOf("?auth=");
    return (cut > 0) ? url.substring(0, cut) : url;
}

SyncResult CloudSync::enter()
{
    Serial.println("📡 Entering CLOUD_SYNC mode");
    syncStartTime = millis();

    syncInProgress = true;
    currentResult = SyncResult::IN_PROGRESS;

    return currentResult;
}

// F14: reinicia a operação de sync explicitamente (changeState no-op não reinicia)
void CloudSync::retry()
{
    if (!syncInProgress)
    {
        Serial.println("🔄 CloudSync retry armed");
        syncInProgress = true;
        currentResult = SyncResult::IN_PROGRESS;
    }
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
        return currentResult;
    }

    // F20: resultado do NTP é verificado; sem relógio os timestamps saem como 0/flag
    bool timeOk = TimeSync::init();
    if (!timeOk)
    {
        Serial.println("⚠️ NTP indisponível - dados serão carimbados com flag timestamp_valid=false");
    }

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

    // R15: descobertas locais (pending) publicadas para aprovação remota
    uploadPendingBikes();

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

// F20/R20: retorna a versão remota para o chamador guardar (não a hora local).
// Corpo "null" (chave inexistente no RTDB) NÃO é versão zero — é ausência.
static bool fetchRemoteVersion(const String &url, uint32_t &remoteVersion)
{
    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK)
    {
        http.end();
        return false;
    }

    String response = http.getString();
    http.end();

    response.trim();
    if (response.length() == 0 || response.equalsIgnoreCase("null"))
    {
        return false; // versão ausente, não zero
    }

    char* endPtr = nullptr;
    unsigned long parsed = strtoul(response.c_str(), &endPtr, 10);
    if (endPtr == response.c_str() || *endPtr != '\0')
    {
        return false; // corpo não-numérico não é versão
    }

    remoteVersion = (uint32_t)parsed;
    return true;
}

bool CloudSync::checkLastUpdateTime(const String &url, uint32_t localLastUpdate,
                                    const String &componentName, uint32_t *remoteOut)
{
    uint32_t remoteLastUpdate = 0;
    if (!fetchRemoteVersion(url, remoteLastUpdate))
    {
        // R20: versão ausente/inconsultável — política: baixa por segurança,
        // exceto quando não há config local (primeiro provisionamento)
        Serial.printf("⚠️ %s version unavailable - downloading for safety\n", componentName.c_str());
        if (remoteOut) *remoteOut = 0;
        return true;
    }

    // R20: sem config local, versão remota qualquer é mais nova (provisionamento)
    bool needsUpdate = (remoteLastUpdate > localLastUpdate) || (localLastUpdate == 0);

    Serial.printf("📋 %s version check: local=%lu, remote=%lu -> %s\n",
                  componentName.c_str(), (unsigned long)localLastUpdate,
                  (unsigned long)remoteLastUpdate, needsUpdate ? "UPDATE NEEDED" : "UP TO DATE");

    if (remoteOut) *remoteOut = remoteLastUpdate;
    return needsUpdate;
}

bool CloudSync::needsConfigUpdate(uint32_t *remoteVersion)
{
    const CentralConfig &config = configManager.getConfig();
    return checkLastUpdateTime(Endpoints::getConfigVersion(), config.last_update, "Config", remoteVersion);
}

bool CloudSync::needsBikeRegistryUpdate(uint32_t *remoteVersion)
{
    if (!LittleFS.exists(BIKE_REGISTRY_FILE))
    {
        Serial.println("📋 No local bike registry - needs download");
        if (remoteVersion) *remoteVersion = 0;
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

    return checkLastUpdateTime(Endpoints::getBikeRegistryVersion(), registry.last_update, "Bike registry", remoteVersion);
}

void CloudSync::updateConfigFromFirebase(const String &csvData, uint32_t remoteVersion)
{
    Serial.println("🔄 Updating config from Firebase CSV...");

    if (configManager.updateFromCSV(csvData, remoteVersion))
    {
        Serial.println("✅ Config updated from Firebase CSV and saved locally");
    }
    else
    {
        // R20: aplicação falhou (CSV inválido, sem versão ou sem persistência) —
        // o sync NÃO reporta sucesso de configuração
        Serial.println("❌ Failed to update config from Firebase CSV");
    }
}

bool CloudSync::downloadCentralConfig()
{
    uint32_t remoteVersion = 0;

    // Verificar se precisa atualizar antes de baixar
    if (!needsConfigUpdate(&remoteVersion))
    {
        Serial.println("📋 Config already up to date - skipping download");
        return true;
    }

    HTTPClient http;
    String url = Endpoints::getCentralConfig();

    Serial.println("🔄 Downloading central config from Firebase...");

    http.begin(url);
    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK)
    {
        Serial.printf("🚨 Central config download failed: HTTP %d (%s)\n",
                      httpCode, redactUrl(url).c_str());
        http.end();
        return false;
    }

    String jsonArray = http.getString();
    http.end();

    // R20: corpo "null" = config inexistente na nuvem — erro de contrato,
    // não "sucesso com valores vazios"
    jsonArray.trim();
    if (jsonArray.length() == 0 || jsonArray.equalsIgnoreCase("null"))
    {
        Serial.println("🚨 Central config absent in cloud (null) - contract error");
        return false;
    }

    // R20: sem versão remota válida a configuração não é aplicada
    // (não existe mais fallback para millis())
    if (remoteVersion == 0)
    {
        Serial.println("🚨 Central config without remote version - not applying");
        return false;
    }

    // Converter JSON array para CSV: [90,60,15,...] -> 90,60,15,...
    jsonArray.replace("[", "");
    jsonArray.replace("]", "");

    updateConfigFromFirebase(jsonArray, remoteVersion);

    // R20: downloadCentralConfig só sucede se a configuração foi aplicada E salva
    if (configManager.getConfig().last_update != remoteVersion)
    {
        return false;
    }

    Serial.printf("✅ Central config downloaded (version %lu)\n", (unsigned long)remoteVersion);
    Serial.printf("   Sync interval: %d seconds\n", configManager.getConfig().intervals.sync_sec);
    return true;
}

bool CloudSync::downloadBikeRegistryData()
{
    uint32_t remoteVersion = 0;

    if (!needsBikeRegistryUpdate(&remoteVersion))
    {
        Serial.println("🚲 Bike registry already up to date - skipping download");
        // F03: merge também no caminho "atualizado" (reboot precisa aplicar o arquivo local)
        BikeManager::mergeFromRegistry();
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

    BikeRegistryData registry;
    memset(&registry, 0, sizeof(registry));

    // F20: versão remota como last_update (era hora local do download)
    registry.last_update = remoteVersion;

    // Parse JSON array: ["bpr-XXXXXX",2,"AA:BB:CC:DD:EE:01",ts1,ts2, ...]
    csvData.replace("[", "");
    csvData.replace("]", "");
    csvData.replace("\"", "");
    csvData.replace(" ", "");

    int bikeIndex = 0;
    int startPos = 0;
    int fieldIndex = 0;

    while (startPos < (int)csvData.length() && bikeIndex < 10) {
        int commaPos = csvData.indexOf(',', startPos);
        String value = (commaPos == -1) ? csvData.substring(startPos) : csvData.substring(startPos, commaPos);

        switch (fieldIndex % 5) {
            case 0: // bike_id (formato bpr-XXXXXX hex)
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
                bikeIndex++;
                break;
        }

        if (commaPos == -1) break;
        startPos = commaPos + 1;
        fieldIndex++;
    }

    registry.bike_count = bikeIndex;

    File file = LittleFS.open(BIKE_REGISTRY_FILE, "w");
    if (file)
    {
        size_t written = file.write((uint8_t*)&registry, sizeof(registry));
        file.close();
        if (written != sizeof(registry))
        {
            Serial.println("❌ Bike registry write failed");
            LittleFS.remove(BIKE_REGISTRY_FILE);
            return false;
        }
        Serial.println("💾 Bike registry saved locally");
    }

    // F03: cadastro baixado alimenta a autorização operacional imediatamente
    BikeManager::mergeFromRegistry();

    Serial.printf("✅ Bike registry downloaded: %d bikes\n", bikeIndex);
    return true;
}

bool CloudSync::downloadBikeConfigs()
{
    Serial.println("🔄 Downloading bike configs individually...");

    // F03/F13: lista REAL de bikes autorizadas (registry já mesclado acima)
    if (!BikeManager::isLoaded())
    {
        // primeiro sync ocorre antes do pairing: carrega status/configs/registry agora
        BikeManager::init();
    }

    std::vector<String> allowedBikes = BikeManager::getAllowedBikeIds();
    if (allowedBikes.empty())
    {
        Serial.println("📝 No allowed bikes in registry - skipping config download");
        return true;
    }

    int successCount = 0;
    for (const String& bikeId : allowedBikes)
    {
        if (BikeManager::downloadSingleBikeConfig(bikeId)) {
            successCount++;
        }
        delay(100);
    }

    Serial.printf("✅ Downloaded/checked configs for %d/%d allowed bikes\n",
                  successCount, (int)allowedBikes.size());
    return true; // sem configs para uma bike não derruba o sync inteiro
}

// F19/R06: a identidade do lote é a chave estável calculada pelo BufferManager
// a partir da identidade dos itens — retries produzem a MESMA chave
bool CloudSync::uploadBufferData()
{
    int totalBatches = 0;
    const int maxBatches = MAX_TOTAL_ITEMS + 8; // guard contra loop se persistência falhar

    // F18/R05: sobe em lotes; cada lote é confirmado individualmente pela
    // identidade exata do seu manifesto
    while (totalBatches < maxBatches)
    {
        DynamicJsonDocument doc(BUFFER_PERSISTENCE_BUFFER);
        String batchKey;
        int items = bufferManager.prepareUploadBatch(doc, UPLOAD_BATCH_MAX_ITEMS, batchKey);
        if (items == 0)
        {
            break;
        }

        String jsonString;
        serializeJson(doc, jsonString);

        String url = Endpoints::getBufferDataBatch(batchKey);

        HTTPClient http;
        http.begin(url);
        http.addHeader("Content-Type", "application/json");

        int httpCode = http.PATCH(jsonString);
        http.end();

        if (httpCode != HTTP_CODE_OK)
        {
            Serial.printf("❌ Buffer upload failed: HTTP %d (batch %s)\n", httpCode, batchKey.c_str());
            Serial.printf("   URL: %s\n", redactUrl(url).c_str());
            // R05: devolve exatamente os itens deste lote ao estado pendente
            bufferManager.rollbackUpload(batchKey);
            return false;
        }

        Serial.printf("📤 Batch %s uploaded: %d items, %d bytes\n",
                      batchKey.c_str(), items, jsonString.length());

        // R05: confirmação por lote — remove somente os itens entregues
        if (!bufferManager.markAsConfirmed(batchKey))
        {
            // Confirm falhou (ex.: flash cheio): lote já subiu; manifesto
            // permanece e a reconciliação do boot devolve os itens como
            // pendentes — a nuvem deduplica pela chave estável
            Serial.println("⚠️ Batch delivered but local confirm failed - will reconcile at boot");
            return false;
        }
        totalBatches++;
    }

    if (totalBatches == 0)
    {
        Serial.println("📝 No buffer data to upload");
        return true;
    }

    Serial.printf("📤 Upload complete: %d batches\n", totalBatches);
    return true;
}

// R15: publica bikes pendentes descobertas localmente para gestão remota
bool CloudSync::uploadPendingBikes()
{
    DynamicJsonDocument doc(1024);
    if (!BikeManager::getPendingBikesForUpload(doc))
    {
        return true; // sem pendentes não é erro
    }

    String jsonString;
    serializeJson(doc, jsonString);

    String url = Endpoints::getPendingBikes();
    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    int httpCode = http.PATCH(jsonString);
    http.end();

    if (httpCode != HTTP_CODE_OK)
    {
        Serial.printf("⚠️ Pending bikes upload failed: HTTP %d (%s)\n",
                      httpCode, redactUrl(url).c_str());
        return false; // não derruba o sync; próxima rodada reenvia (idempotente)
    }

    Serial.println("📤 Pending bikes published for remote approval");
    return true;
}

bool CloudSync::uploadHeartbeat()
{
    HTTPClient http;

    String url = Endpoints::getHeartbeat();

    time_t now = time(nullptr);
    if (now < 1000000000L) now = 0; // F20: sem NTP, não inventa Unix time

    DynamicJsonDocument doc(HEARTBEAT_BUFFER);
    doc["timestamp"] = now;
    doc["timestamp_valid"] = (now > 0);
    doc["bikes_connected"] = BikeManager::getConnectedCount();
    doc["heap"] = ESP.getFreeHeap();
    doc["uptime"] = millis() / 1000;

    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    String jsonString;
    serializeJson(doc, jsonString);

    int httpCode = http.PUT(jsonString);
    http.end();

    bool success = (httpCode == HTTP_CODE_OK);

    if (success)
    {
        Serial.printf("💓 Heartbeat: Bikes: %d | Heap: %d\n",
                      BikeManager::getConnectedCount(), ESP.getFreeHeap());
    }
    else
    {
        Serial.printf("❌ Heartbeat falhou: HTTP %d (%s)\n", httpCode, redactUrl(url).c_str());
    }

    return success;
}
