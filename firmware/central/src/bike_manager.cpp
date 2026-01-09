#include <ArduinoJson.h>
#include <LittleFS.h>
#include "bike_manager.h"
#include "binary_structs.h"
#include "bpr_json_helper.h"
#include "buffer_manager.h"
#include "constants.h"

extern BufferManager bufferManager;

// Global data for bike management
static BikeStatusData bikeStatus;
static std::map<String, bool> configChanged;
static bool dataLoaded = false;

bool BikeManager::init()
{
    Serial.println("💾 BikeManager::init() - DISABLED FOR TEST");
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
    if (!bikeId.startsWith("bpr-") || bikeId.length() != 10)
    {
        Serial.printf("❌ Invalid bike ID format: %s\n", bikeId.c_str());
        return false;
    }

    // Procurar bike na struct
    int bikeIndex = -1;
    for (int i = 0; i < bikeStatus.bike_count; i++)
    {
        if (String(bikeStatus.bike_ids[i]) == bikeId)
        {
            bikeIndex = i;
            break;
        }
    }

    if (bikeIndex == -1)
    {
        Serial.printf("🆕 New bike detected: %s - allowing connection + adding as pending\n", bikeId.c_str());
        addPendingBike(bikeId);
        return true; // Permite conexão de bikes novas
    }

    BikeStatus status = (BikeStatus)bikeStatus.statuses[bikeIndex];
    bool canConnect = (status != STATUS_BLOCKED);

    Serial.printf("🔍 Bike %s status: %d (%s)\n",
                  bikeId.c_str(), status, canConnect ? "✅ Can connect" : "❌ Blocked");

    return canConnect;
}

bool BikeManager::isAllowed(const String &bikeId)
{
    if (!dataLoaded)
        return false;

    // Verificar se é formato válido BPR
    if (!bikeId.startsWith("bpr-") || bikeId.length() != 10)
    {
        return false;
    }

    // Procurar bike na struct
    for (int i = 0; i < bikeStatus.bike_count; i++)
    {
        if (String(bikeStatus.bike_ids[i]) == bikeId)
        {
            BikeStatus status = (BikeStatus)bikeStatus.statuses[i];
            return (status == STATUS_ALLOWED); // Só bikes ALLOWED podem enviar dados
        }
    }

    return false; // Bikes novas NÃO podem enviar dados (só pending)
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
    strcpy(bikeStatus.bike_ids[index], bikeId.c_str());
    bikeStatus.statuses[index] = STATUS_PENDING;
    bikeStatus.first_seen[index] = now;
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
    for (int i = 0; i < bikeStatus.bike_count; i++)
    {
        if (String(bikeStatus.bike_ids[i]) == bikeId)
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
        if (status == STATUS_PENDING && bikeStatus.first_seen[i] > 0)
        {
            uint32_t firstSeen = bikeStatus.first_seen[i];
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
    for (int i = 0; i < bikeStatus.bike_count; i++)
    {
        if (String(bikeStatus.bike_ids[i]) == bikeId)
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
        String bikeId = String(bikeStatus.bike_ids[i]);
        JsonObject bikeData = bikes_array.createNestedObject();

        bikeData["id"] = bikeId;
        bikeData["status"] = bikeStatus.statuses[i];
        bikeData["last_seen"] = bikeStatus.last_contacts[i];
        bikeData["battery_last"] = bikeStatus.battery_levels[i];
        bikeData["first_seen"] = bikeStatus.first_seen[i];

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
    Serial.printf("⚠️ No config system implemented for %s\n", bikeId.c_str());
    return ""; // Retorna vazio - config system não implementado
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
    // Config system não implementado - sempre retorna false
    return false;
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
        if (String(bikeStatus.bike_ids[i]) == bikeId)
        {
            return bikeStatus.battery_levels[i] <= 25; // Bateria crítica <= 25%
        }
    }
    
    return false;
}