#include <ArduinoJson.h>
#include <LittleFS.h>
#include "bike_manager.h"
#include "bpr_json_helper.h"
#include "buffer_manager.h"
#include "constants.h"

extern BufferManager bufferManager;

// Global JSON documents for bike data
static DynamicJsonDocument bikes(BIKE_REGISTRY_BUFFER);
static DynamicJsonDocument configVersions(CONFIG_VERSION_BUFFER);
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
        bikes.clear();
        dataLoaded = true;
        return saveData();
    }

    File file = LittleFS.open(BIKE_STATUS_FILE, "r");
    if (!file)
    {
        Serial.println("❌ Failed to open bike data");
        return false;
    }

    DeserializationError error = deserializeJson(bikes, file);
    file.close();

    if (error)
    {
        Serial.printf("❌ Data parse error: %s\n", error.c_str());
        return false;
    }

    dataLoaded = true;
    Serial.printf("✅ Bike data loaded: %d bikes\n", bikes.size());

    // Log bikes por status
    int allowed = 0, pending = 0, blocked = 0;
    JsonObject obj = bikes.as<JsonObject>();
    for (JsonPair bike : obj)
    {
        String status = bike.value()["status"] | "unknown";
        if (status == "allowed")
            allowed++;
        else if (status == "pending")
            pending++;
        else if (status == "blocked")
            blocked++;
    }
    Serial.printf("   Allowed: %d | Pending: %d | Blocked: %d\n", allowed, pending, blocked);

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

    serializeJson(bikes, file);
    file.close();

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

    if (!bikes.containsKey(bikeId))
    {
        Serial.printf("🆕 New bike detected: %s - allowing connection + adding as pending\n", bikeId.c_str());
        addPendingBike(bikeId);
        return true; // Permite conexão de bikes novas
    }

    String status = bikes[bikeId]["status"] | "unknown";
    bool canConnect = (status != "blocked");

    Serial.printf("🔍 Bike %s status: %s (%s)\n",
                  bikeId.c_str(), status.c_str(), canConnect ? "✅ Can connect" : "❌ Blocked");

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

    if (!bikes.containsKey(bikeId))
    {
        return false; // Bikes novas NÃO podem enviar dados (só pending)
    }

    String status = bikes[bikeId]["status"] | "unknown";
    return (status == "allowed"); // Só bikes ALLOWED podem enviar dados
}

void BikeManager::addPendingBike(const String &bikeId)
{
    time_t now = time(nullptr);
    struct tm timeinfo;
    getLocalTime(&timeinfo);

    char dateStr[64];
    strftime(dateStr, sizeof(dateStr), "%Y-%m-%d %H:%M:%S UTC-3", &timeinfo);

    bikes[bikeId]["status"] = "pending";
    bikes[bikeId]["first_seen"] = now;
    bikes[bikeId]["first_seen_human"] = dateStr;
    bikes[bikeId]["last_visit"] = now;
    bikes[bikeId]["last_visit_human"] = dateStr;
    bikes[bikeId]["visit_count"] = 1;
    bikes[bikeId]["last_heartbeat"] = nullptr;

    saveData();
    Serial.printf("📝 Bike %s added as pending (first seen: %s)\n", bikeId.c_str(), dateStr);
}

void BikeManager::updateHeartbeat(const String &bikeId, int battery, int heap)
{
    if (!dataLoaded || !bikes.containsKey(bikeId))
        return;

    time_t now = time(nullptr);
    struct tm timeinfo;
    getLocalTime(&timeinfo);

    char dateStr[64];
    strftime(dateStr, sizeof(dateStr), "%Y-%m-%d %H:%M:%S UTC-3", &timeinfo);

    bikes[bikeId]["last_heartbeat"]["timestamp"] = now;
    bikes[bikeId]["last_heartbeat"]["timestamp_human"] = dateStr;
    bikes[bikeId]["last_heartbeat"]["battery"] = battery;
    bikes[bikeId]["last_heartbeat"]["heap"] = heap;

    Serial.printf("💓 Heartbeat updated: %s (bat:%d%%, heap:%d)\n",
                  bikeId.c_str(), battery, heap);
}

bool BikeManager::getPendingBikesForUpload(DynamicJsonDocument &doc)
{
    if (!dataLoaded)
        return false;

    doc.clear();

    // Só incluir bikes NOVAS (pending recentes)
    JsonObject obj = bikes.as<JsonObject>();
    for (JsonPair bike : obj)
    {
        String bikeId = bike.key().c_str();
        String status = bike.value()["status"] | "";

        if (status == "pending" && bike.value()["first_seen"])
        {
            uint32_t firstSeen = bike.value()["first_seen"] | 0;
            uint32_t now = time(nullptr);

            // Só incluir se foi vista há menos de 5 minutos
            if ((now - firstSeen) < 300)
            {
                doc[bikeId] = bike.value();
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
    JsonObject obj = bikes.as<JsonObject>();
    for (JsonPair bike : obj)
    {
        String status = bike.value()["status"] | "";
        if (status == "allowed")
            count++;
    }
    return count;
}

void BikeManager::recordPendingVisit(const String &bikeId)
{
    if (!dataLoaded || !bikes.containsKey(bikeId))
        return;

    String status = bikes[bikeId]["status"] | "";
    if (status != "pending")
        return;

    time_t now = time(nullptr);
    struct tm timeinfo;
    getLocalTime(&timeinfo);

    char dateStr[64];
    strftime(dateStr, sizeof(dateStr), "%Y-%m-%d %H:%M:%S UTC-3", &timeinfo);

    bikes[bikeId]["last_visit"] = now;
    bikes[bikeId]["last_visit_human"] = dateStr;

    int visitCount = bikes[bikeId]["visit_count"] | 0;
    bikes[bikeId]["visit_count"] = visitCount + 1;

    saveData();
    Serial.printf("📝 Pending bike %s visited (count: %d, time: %s)\n",
                  bikeId.c_str(), visitCount + 1, dateStr);
}

int BikeManager::getPendingCount()
{
    if (!dataLoaded)
        return 0;

    int count = 0;
    JsonObject obj = bikes.as<JsonObject>();
    for (JsonPair bike : obj)
    {
        String status = bike.value()["status"] | "";
        if (status == "pending")
            count++;
    }
    return count;
}

void BikeManager::logConfigEvent(const String &bikeId, const String &event, bool success)
{
    time_t now = time(nullptr);
    struct tm timeinfo;
    getLocalTime(&timeinfo);

    char dateStr[64];
    strftime(dateStr, sizeof(dateStr), "%Y-%m-%d %H:%M:%S UTC-3", &timeinfo);

    // Criar entrada no log de configuração
    JsonArray configLog;
    if (bikes[bikeId]["config_log"].isNull())
    {
        configLog = bikes[bikeId].createNestedArray("config_log");
    }
    else
    {
        configLog = bikes[bikeId]["config_log"];
    }

    JsonObject logEntry = configLog.createNestedObject();
    logEntry["timestamp"] = now;
    logEntry["timestamp_human"] = dateStr;
    logEntry["event"] = event;
    logEntry["success"] = success;

    // Manter apenas os últimos 10 logs
    while (configLog.size() > 10)
    {
        configLog.remove(0);
    }

    saveData();
    Serial.printf("📝 Config event logged: %s - %s (%s)\n",
                  bikeId.c_str(), event.c_str(), success ? "SUCCESS" : "FAILED");
}

int BikeManager::getConnectedCount()
{
    if (!dataLoaded)
        return 0;

    int count = 0;
    time_t now = time(nullptr);

    JsonObject obj = bikes.as<JsonObject>();
    for (JsonPair bike : obj)
    {
        JsonObject heartbeat = bike.value()["last_heartbeat"];
        if (!heartbeat.isNull())
        {
            uint32_t lastSeen = heartbeat["timestamp"] | 0;

            // Considerar conectada se heartbeat foi há menos de 2 minutos
            if ((now - lastSeen) < 120)
            {
                count++;
            }
        }
    }
    return count;
}

void BikeManager::populateHeartbeatData(JsonArray &bikes_array)
{
    if (!dataLoaded)
        return;

    time_t now = time(nullptr);
    JsonObject obj = bikes.as<JsonObject>();

    for (JsonPair bike : obj)
    {
        String bikeId = bike.key().c_str();
        JsonObject bikeData = bikes_array.createNestedObject();

        bikeData["id"] = bikeId;
        bikeData["status"] = bike.value()["status"] | "unknown";

        // Dados do último heartbeat
        JsonObject heartbeat = bike.value()["last_heartbeat"];
        if (!heartbeat.isNull())
        {
            bikeData["last_seen"] = heartbeat["timestamp"] | 0;
            bikeData["battery_last"] = heartbeat["battery"] | 0;
            bikeData["heap_last"] = heartbeat["heap"] | 0;

            uint32_t lastSeen = heartbeat["timestamp"] | 0;
            uint32_t timeSince = now - lastSeen;
            bikeData["seconds_since_contact"] = timeSince;
            bikeData["is_recent"] = (timeSince < 300); // < 5min
        }
        else
        {
            bikeData["last_seen"] = 0;
            bikeData["battery_last"] = 0;
            bikeData["heap_last"] = 0;
            bikeData["seconds_since_contact"] = 999999;
            bikeData["is_recent"] = false;
        }

        // Dados de visitas (para bikes pending)
        bikeData["visit_count"] = bike.value()["visit_count"] | 0;
        bikeData["first_seen"] = bike.value()["first_seen"] | 0;
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
    if (!dataLoaded || !bikes.containsKey(bikeId) || bikes[bikeId]["config"].isNull())
    {
        Serial.printf("⚠️ No config found for %s, no config available\n", bikeId.c_str());
        return ""; // Retorna vazio se não tem config
    }

    DynamicJsonDocument response(BIKE_CONFIG_BUFFER);
    BPRJsonHelper::addBikeResponse(response, "config_push", bikeId);
    response["config"] = bikes[bikeId]["config"];

    String result;
    serializeJson(response, result);
    return result;
}

String BikeManager::confirmDataUpload(const String &bikeId)
{
    DynamicJsonDocument response(BIKE_HEARTBEAT_BUFFER);
    BPRJsonHelper::addBikeResponse(response, "upload_confirmed", bikeId);
    response["can_clear_buffer"] = true;

    String result;
    serializeJson(response, result);

    Serial.printf("✅ Data upload confirmed for %s - can clear buffer\n", bikeId.c_str());
    return result;
}