#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include "bike_manager.h"
#include "binary_structs.h"
#include "bpr_json_helper.h"
#include "buffer_manager.h"
#include "config_credentials.h"
#include "constants.h"

extern BufferManager bufferManager;
extern ConfigCredentials configCredentials;

// Global data for bike management
static BikeStatusData bikeStatus;
static BikeConfigData bikeConfigs;
static std::map<String, bool> configChanged;
static bool dataLoaded = false;
static bool configsLoaded = false;

bool BikeManager::init()
{
    Serial.println("💾 BikeManager::init() - Loading bike data and configs");
    
    if (!loadData()) {
        Serial.println("⚠️ Failed to load bike data, using defaults");
    }
    
    if (!loadBikeConfigs()) {
        Serial.println("⚠️ Failed to load bike configs, using defaults");
    }
    
    dataLoaded = true;
    return true;
}

bool BikeManager::loadData()
{
    if (!LittleFS.exists(BIKE_STATUS_FILE))
    {
        Serial.println("📄 Bike data not found, creating empty");
        memset(&bikeStatus, 0, sizeof(bikeStatus));
        dataLoaded = true;
        return saveData();
    }

    File file = LittleFS.open(BIKE_STATUS_FILE, "r");
    if (!file)
    {
        Serial.println("❌ Failed to open bike data");
        return false;
    }

    size_t bytesRead = file.readBytes((char*)&bikeStatus, sizeof(BikeStatusData));
    file.close();

    if (bytesRead != sizeof(BikeStatusData))
    {
        Serial.printf("❌ Data size mismatch: %d != %d\n", bytesRead, sizeof(BikeStatusData));
        return false;
    }

    dataLoaded = true;
    Serial.printf("✅ Bike data loaded: %d bikes\n", bikeStatus.bike_count);

    // Log bikes por status
    int allowed = 0, pending = 0, blocked = 0, unknown = 0;
    for (int i = 0; i < bikeStatus.bike_count; i++)
    {
        BikeStatus status = (BikeStatus)bikeStatus.statuses[i];
        switch (status) {
            case STATUS_ALLOWED: allowed++; break;
            case STATUS_PENDING: pending++; break;
            case STATUS_BLOCKED: blocked++; break;
            default: unknown++; break;
        }
    }
    Serial.printf("   Allowed: %d | Pending: %d | Blocked: %d | Unknown: %d\n", allowed, pending, blocked, unknown);

    return true;
}

bool BikeManager::saveData()
{
    File file = LittleFS.open(BIKE_STATUS_FILE, "w");
    if (!file)
    {
        Serial.println("❌ Failed to create bike data");
        return false;
    }

    size_t bytesWritten = file.write((uint8_t*)&bikeStatus, sizeof(BikeStatusData));
    file.close();

    if (bytesWritten != sizeof(BikeStatusData))
    {
        Serial.printf("❌ Failed to write bike data: %d != %d\n", bytesWritten, sizeof(BikeStatusData));
        return false;
    }

    Serial.println("💾 Bike data saved");
    return true;
}

bool BikeManager::canConnect(const String &bikeId)
{
    if (!dataLoaded)
        return false;

    // Verificar se é formato válido BPR
    if (!bikeId.startsWith("bpr-") && !bikeId.startsWith("bike") && !bikeId.startsWith("intenso"))
    {
        Serial.printf("❌ Invalid bike ID format: %s\n", bikeId.c_str());
        return false;
    }

    // Procurar bike na struct (só bikes aprovadas estão aqui)
    uint32_t bikeIntId = bikeIdToInt(bikeId);
    for (int i = 0; i < bikeStatus.bike_count; i++)
    {
        if (bikeStatus.bike_ids[i] == bikeIntId)
        {
            BikeStatus status = (BikeStatus)bikeStatus.statuses[i];
            bool canConnect = (status == STATUS_ALLOWED);
            
            Serial.printf("🔍 Bike %s found in registry: %s\n",
                          bikeId.c_str(), canConnect ? "✅ Can connect" : "❌ Blocked");
            
            return canConnect;
        }
    }

    // Bike não está no registry - rejeitar
    Serial.printf("❌ Bike %s not in registry - connection rejected\n", bikeId.c_str());
    return false;
}

bool BikeManager::isAllowed(const String &bikeId)
{
    if (!dataLoaded)
        return false;

    // Verificar se é formato válido
    if (!bikeId.startsWith("bpr-") && !bikeId.startsWith("bike") && !bikeId.startsWith("intenso"))
    {
        return false;
    }

    // Procurar bike na struct (só bikes aprovadas)
    uint32_t bikeIntId = bikeIdToInt(bikeId);
    for (int i = 0; i < bikeStatus.bike_count; i++)
    {
        if (bikeStatus.bike_ids[i] == bikeIntId)
        {
            BikeStatus status = (BikeStatus)bikeStatus.statuses[i];
            return (status == STATUS_ALLOWED);
        }
    }

    return false; // Bike não está no registry
}

void BikeManager::addPendingBike(const String &bikeId)
{
    if (bikeStatus.bike_count >= 10) {
        Serial.println("❌ Max bikes reached, cannot add new bike");
        return;
    }

    time_t now = time(nullptr);
    struct tm timeinfo;
    getLocalTime(&timeinfo);

    char dateStr[64];
    strftime(dateStr, sizeof(dateStr), "%Y-%m-%d %H:%M:%S UTC-3", &timeinfo);

    int index = bikeStatus.bike_count;
    bikeStatus.bike_ids[index] = bikeIdToInt(bikeId);
    bikeStatus.statuses[index] = STATUS_PENDING;
    bikeStatus.created_at[index] = now;
    bikeStatus.last_contacts[index] = now;
    bikeStatus.battery_levels[index] = 0;
    bikeStatus.bike_count++;

    saveData();
    Serial.printf("📝 Bike %s added as pending (first seen: %s)\n", bikeId.c_str(), dateStr);
}

void BikeManager::updateHeartbeat(const String &bikeId, int battery, int heap, 
                                   uint16_t sessions, uint32_t bytes, uint32_t oldestTs, uint8_t bufferPercent)
{
    if (!dataLoaded)
        return;

    // Encontrar bike na struct
    int bikeIndex = -1;
    uint32_t bikeIntId = bikeIdToInt(bikeId);
    for (int i = 0; i < bikeStatus.bike_count; i++)
    {
        if (bikeStatus.bike_ids[i] == bikeIntId)
        {
            bikeIndex = i;
            break;
        }
    }

    if (bikeIndex == -1)
        return;

    time_t now = time(nullptr);
    bikeStatus.last_contacts[bikeIndex] = now;
    bikeStatus.battery_levels[bikeIndex] = battery;

    Serial.printf("💓 Heartbeat updated: %s (bat:%d%%, heap:%d, pending:%d sessions/%d bytes)\n",
                  bikeId.c_str(), battery, heap, sessions, bytes);
}

bool BikeManager::getPendingBikesForUpload(DynamicJsonDocument &doc)
{
    if (!dataLoaded)
        return false;

    doc.clear();

    // Só incluir bikes NOVAS (pending recentes)
    for (int i = 0; i < bikeStatus.bike_count; i++)
    {
        BikeStatus status = (BikeStatus)bikeStatus.statuses[i];
        if (status == STATUS_PENDING && bikeStatus.created_at[i] > 0)
        {
            uint32_t firstSeen = bikeStatus.created_at[i];
            uint32_t now = time(nullptr);

            // Só incluir se foi vista há menos de 5 minutos
            if ((now - firstSeen) < 300)
            {
                String bikeId = String(bikeStatus.bike_ids[i]);
                JsonObject bikeObj = doc.createNestedObject(bikeId);
                bikeObj["status"] = status;
                bikeObj["first_seen"] = firstSeen;
                bikeObj["last_contact"] = bikeStatus.last_contacts[i];
            }
        }
    }

    return doc.size() > 0;
}

int BikeManager::getAllowedCount()
{
    if (!dataLoaded)
        return 0;

    int count = 0;
    for (int i = 0; i < bikeStatus.bike_count; i++)
    {
        BikeStatus status = (BikeStatus)bikeStatus.statuses[i];
        if (status == STATUS_ALLOWED)
            count++;
    }
    return count;
}

void BikeManager::recordPendingVisit(const String &bikeId)
{
    if (!dataLoaded)
        return;

    // Encontrar bike na struct
    int bikeIndex = -1;
    uint32_t bikeIntId = bikeIdToInt(bikeId);
    for (int i = 0; i < bikeStatus.bike_count; i++)
    {
        if (bikeStatus.bike_ids[i] == bikeIntId)
        {
            bikeIndex = i;
            break;
        }
    }

    if (bikeIndex == -1 || bikeStatus.statuses[bikeIndex] != STATUS_PENDING)
        return;

    time_t now = time(nullptr);
    bikeStatus.last_contacts[bikeIndex] = now;
    
    saveData();
    Serial.printf("📝 Pending bike %s visited\n", bikeId.c_str());
}

int BikeManager::getPendingCount()
{
    if (!dataLoaded)
        return 0;

    int count = 0;
    for (int i = 0; i < bikeStatus.bike_count; i++)
    {
        BikeStatus status = (BikeStatus)bikeStatus.statuses[i];
        if (status == STATUS_PENDING)
            count++;
    }
    return count;
}

void BikeManager::logConfigEvent(const String &bikeId, const String &event, bool success)
{
    Serial.printf("📝 Config event: %s - %s (%s)\n",
                  bikeId.c_str(), event.c_str(), success ? "SUCCESS" : "FAILED");
}

int BikeManager::getConnectedCount()
{
    if (!dataLoaded)
        return 0;

    int count = 0;
    time_t now = time(nullptr);

    for (int i = 0; i < bikeStatus.bike_count; i++)
    {
        uint32_t lastSeen = bikeStatus.last_contacts[i];
        // Considerar conectada se heartbeat foi há menos de 2 minutos
        if ((now - lastSeen) < 120)
        {
            count++;
        }
    }
    return count;
}

void BikeManager::populateHeartbeatData(JsonArray &bikes_array)
{
    if (!dataLoaded)
        return;

    time_t now = time(nullptr);

    for (int i = 0; i < bikeStatus.bike_count; i++)
    {
        String bikeId = intToBikeId(bikeStatus.bike_ids[i]);
        JsonObject bikeData = bikes_array.createNestedObject();

        bikeData["id"] = bikeId;
        bikeData["status"] = bikeStatus.statuses[i];
        bikeData["last_seen"] = bikeStatus.last_contacts[i];
        bikeData["battery_last"] = bikeStatus.battery_levels[i];
        bikeData["first_seen"] = bikeStatus.created_at[i];

        uint32_t lastSeen = bikeStatus.last_contacts[i];
        uint32_t timeSince = now - lastSeen;
        bikeData["seconds_since_contact"] = timeSince;
        bikeData["is_recent"] = (timeSince < 300); // < 5min
    }
}

bool BikeManager::hasConfigUpdate(const String &bikeId)
{
    return configChanged.find(bikeId) != configChanged.end() && configChanged[bikeId];
}

void BikeManager::markConfigSent(const String &bikeId)
{
    configChanged[bikeId] = false;
    Serial.printf("✅ Config marked as sent for %s\n", bikeId.c_str());
}

String BikeManager::getConfigForBike(const String &bikeId)
{
    if (!configsLoaded) {
        Serial.printf("⚠️ Configs not loaded for %s\n", bikeId.c_str());
        return "";
    }
    
    // Encontrar bike nas configurações
    for (int i = 0; i < bikeConfigs.bike_count; i++) {
        if (bikeConfigs.bike_ids[i] == bikeIdToInt(bikeId)) {
            DynamicJsonDocument doc(512);
            doc["wifi"]["scan_interval_sec"] = bikeConfigs.scan_intervals[i];
            doc["wifi"]["scan_timeout_ms"] = bikeConfigs.scan_timeouts[i];
            doc["wifi"]["max_networks"] = bikeConfigs.max_networks[i];
            doc["wifi"]["rssi_threshold"] = bikeConfigs.rssi_thresholds[i];
            doc["power"]["deep_sleep_duration_sec"] = bikeConfigs.sleep_durations[i];
            doc["ble"]["scan_time_sec"] = bikeConfigs.ble_scan_times[i];
            doc["dev_mode"] = (bikeConfigs.dev_modes[i] == 1);
            
            String result;
            serializeJson(doc, result);
            return result;
        }
    }
    
    Serial.printf("⚠️ No config found for %s\n", bikeId.c_str());
    return "";
}

String BikeManager::generateSystemHeartbeat()
{
    if (!dataLoaded)
        return "";
        
    DynamicJsonDocument heartbeat(CENTRAL_HEARTBEAT_BUFFER);
    
    uint32_t currentTime = millis() / 1000;
    heartbeat["timestamp"] = currentTime;
    heartbeat["uptime_sec"] = currentTime;
    heartbeat["heap_free"] = ESP.getFreeHeap();
    
    // Popular dados das bikes
    JsonArray bikes_array = heartbeat.createNestedArray("bikes");
    populateHeartbeatData(bikes_array);
    
    // Estatísticas calculadas
    heartbeat["total_bikes"] = bikes_array.size();
    heartbeat["bikes_allowed"] = getAllowedCount();
    heartbeat["bikes_pending"] = getPendingCount();
    heartbeat["bikes_with_recent_contact"] = getConnectedCount();
    
    String result;
    serializeJson(heartbeat, result);
    return result;
}

void BikeManager::saveSystemHeartbeat()
{
    String heartbeatData = generateSystemHeartbeat();
    
    if (!heartbeatData.isEmpty()) {
        bufferManager.addConfigData("system_heartbeat", heartbeatData);
        Serial.printf("💓 System heartbeat saved: %d allowed, %d pending, %d connected\n", 
                      getAllowedCount(), getPendingCount(), getConnectedCount());
    }
}

bool BikeManager::needsConfigUpdate(const String &bikeId, uint32_t bikeLastUpdate)
{
    if (!configsLoaded) return false;
    
    // Compara com last_update geral das configs
    return bikeConfigs.last_update > bikeLastUpdate;
}

String BikeManager::confirmDataUpload(const String &bikeId)
{
    String result = BPRJsonHelper::createProceedResponse();
    Serial.printf("✅ Data upload confirmed for %s - can clear buffer\n", bikeId.c_str());
    return result;
}
// Getters simplificados para BikePairing
uint32_t BikeManager::getPendingBytes(const String &bikeId)
{
    return 0; // Simplificado - não implementado
}

uint16_t BikeManager::getPendingSessions(const String &bikeId)
{
    return 0; // Simplificado - não implementado
}

uint8_t BikeManager::getBufferUsage(const String &bikeId)
{
    return 0; // Simplificado - não implementado
}

bool BikeManager::isBatteryLow(const String &bikeId)
{
    if (!dataLoaded)
        return false;
    
    // Encontrar bike na struct
    for (int i = 0; i < bikeStatus.bike_count; i++)
    {
        if (bikeStatus.bike_ids[i] == bikeIdToInt(bikeId))
        {
            return bikeStatus.battery_levels[i] <= 25; // Bateria crítica <= 25%
        }
    }
    
    return false;
}

bool BikeManager::loadBikeConfigs()
{
    if (!LittleFS.exists(BIKE_CONFIGS_FILE))
    {
        Serial.println("📄 Bike configs not found, creating defaults");
        memset(&bikeConfigs, 0, sizeof(bikeConfigs));
        bikeConfigs.last_update = time(nullptr);
        bikeConfigs.bike_count = 0;
        configsLoaded = true;
        return saveBikeConfigs();
    }

    File file = LittleFS.open(BIKE_CONFIGS_FILE, "r");
    if (!file)
    {
        Serial.println("❌ Failed to open bike configs");
        return false;
    }

    size_t bytesRead = file.readBytes((char*)&bikeConfigs, sizeof(BikeConfigData));
    file.close();

    if (bytesRead != sizeof(BikeConfigData))
    {
        Serial.printf("❌ Config size mismatch: %d != %d\n", bytesRead, sizeof(BikeConfigData));
        return false;
    }

    configsLoaded = true;
    Serial.printf("✅ Bike configs loaded: %d bikes\n", bikeConfigs.bike_count);
    return true;
}

bool BikeManager::saveBikeConfigs()
{
    File file = LittleFS.open(BIKE_CONFIGS_FILE, "w");
    if (!file)
    {
        Serial.println("❌ Failed to create bike configs");
        return false;
    }

    size_t bytesWritten = file.write((uint8_t*)&bikeConfigs, sizeof(BikeConfigData));
    file.close();

    if (bytesWritten != sizeof(BikeConfigData))
    {
        Serial.printf("❌ Failed to write bike configs: %d != %d\n", bytesWritten, sizeof(BikeConfigData));
        return false;
    }

    Serial.println("💾 Bike configs saved");
    return true;
}

bool BikeManager::downloadSingleBikeConfig(const String& bikeId)
{
    HTTPClient http;
    
    // 1. Verificar se precisa atualizar
    String timestampUrl = "/bases/" + configCredentials.getBaseId() + "/bike_configs/last_update/" + bikeId + ".json?auth=" + configCredentials.getFirebaseKey();
    String fullTimestampUrl = configCredentials.getFirebaseURL() + timestampUrl;
    
    http.begin(fullTimestampUrl);
    int timestampCode = http.GET();
    
    if (timestampCode == HTTP_CODE_OK) {
        uint32_t remoteTimestamp = http.getString().toInt();
        http.end();
        
        // Se já está atualizado, não baixar
        if (remoteTimestamp <= bikeConfigs.last_update) {
            Serial.printf("⏭️ Config %s already up to date\n", bikeId.c_str());
            return true;
        }
    } else {
        http.end();
        Serial.printf("⚠️ No timestamp for %s, downloading anyway\n", bikeId.c_str());
    }
    
    // 2. Baixar configuração
    String configUrl = "/bases/" + configCredentials.getBaseId() + "/bike_configs/" + bikeId + ".json?auth=" + configCredentials.getFirebaseKey();
    String fullConfigUrl = configCredentials.getFirebaseURL() + configUrl;
    
    http.begin(fullConfigUrl);
    int configCode = http.GET();
    
    if (configCode != HTTP_CODE_OK) {
        Serial.printf("❌ Failed to download config for %s: HTTP %d\n", bikeId.c_str(), configCode);
        http.end();
        return false;
    }
    
    String arrayData = http.getString();
    http.end();
    
    // 3. Converter array JSON para CSV
    // "[300,5000,20,-85,3600,5,0]" → "300,5000,20,-85,3600,5,0"
    arrayData.replace("[", "");
    arrayData.replace("]", "");
    
    // 4. Aplicar configuração
    bool success = updateBikeConfigFromCSV(bikeId, arrayData);
    
    if (success) {
        Serial.printf("✅ Config downloaded for %s: %s\n", bikeId.c_str(), arrayData.c_str());
    }
    
    return success;
}

bool BikeManager::updateBikeConfigFromCSV(const String& bikeId, const String& csvData)
{
    if (!configsLoaded) {
        Serial.println("⚠️ Configs not loaded, cannot update");
        return false;
    }
    
    // Encontrar slot para a bike ou criar novo
    int bikeIndex = -1;
    for (int i = 0; i < bikeConfigs.bike_count; i++) {
        if (String(bikeConfigs.bike_ids[i]) == bikeId) {
            bikeIndex = i;
            break;
        }
    }
    
    // Se não encontrou e tem espaço, criar novo slot
    if (bikeIndex == -1 && bikeConfigs.bike_count < 10) {
        bikeIndex = bikeConfigs.bike_count;
        bikeConfigs.bike_ids[bikeIndex] = bikeIdToInt(bikeId);
        bikeConfigs.bike_count++;
    }
    
    if (bikeIndex == -1) {
        Serial.printf("❌ No space for bike %s\n", bikeId.c_str());
        return false;
    }
    
    // Parse CSV: "300,5000,20,-85,3600,5,0"
    int index = 0;
    int startPos = 0;
    
    while (startPos < csvData.length() && index < 7) {
        int commaPos = csvData.indexOf(',', startPos);
        String value = (commaPos == -1) ? csvData.substring(startPos) : csvData.substring(startPos, commaPos);
        
        switch (index) {
            case 0: bikeConfigs.scan_intervals[bikeIndex] = value.toInt(); break;
            case 1: bikeConfigs.scan_timeouts[bikeIndex] = value.toInt(); break;
            case 2: bikeConfigs.max_networks[bikeIndex] = value.toInt(); break;
            case 3: bikeConfigs.rssi_thresholds[bikeIndex] = value.toInt(); break;
            case 4: bikeConfigs.sleep_durations[bikeIndex] = value.toInt(); break;
            case 5: bikeConfigs.ble_scan_times[bikeIndex] = value.toInt(); break;
            case 6: bikeConfigs.dev_modes[bikeIndex] = value.toInt(); break;
        }
        
        if (commaPos == -1) break;
        startPos = commaPos + 1;
        index++;
    }
    
    // Atualizar timestamp
    bikeConfigs.last_update = time(nullptr);
    
    Serial.printf("✅ Config updated for %s from CSV\n", bikeId.c_str());
    return saveBikeConfigs();
}