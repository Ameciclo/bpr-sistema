#include "bike_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "constants.h"
#include "config_manager.h"
#include "config_credentials.h"
#include <HTTPClient.h>

extern ConfigManager configManager;

// Global JSON documents for bike data
static DynamicJsonDocument bikes(JSON_LARGE_BUFFER);
static DynamicJsonDocument configVersions(JSON_SMALL_BUFFER);
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
    if (!LittleFS.exists(BIKE_DATA_FILE))
    {
        Serial.println("📄 Bike data not found, creating empty");
        bikes.clear();
        dataLoaded = true;
        return saveData();
    }

    File file = LittleFS.open(BIKE_DATA_FILE, "r");
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
    File file = LittleFS.open(BIKE_DATA_FILE, "w");
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

void BikeManager::updateFromFirebase(const DynamicJsonDocument &firebaseData)
{
    Serial.println("🔄 Updating bike data from Firebase...");

    bikes.clear();

    JsonObjectConst obj = firebaseData.as<JsonObjectConst>();
    for (JsonPairConst bike : obj)
    {
        String bikeId = bike.key().c_str();
        bikes[bikeId] = bike.value();

        String status = bike.value()["status"] | "unknown";
        Serial.printf("   %s: %s\n", bikeId.c_str(), status.c_str());
    }

    saveData();
    dataLoaded = true;

    Serial.printf("✅ Data updated: %d bikes from Firebase\n", bikes.size());
}

bool BikeManager::uploadToFirebase(DynamicJsonDocument &doc)
{
    if (!dataLoaded)
        return false;

    doc.clear();

    // Só enviar bikes NOVAS (que não existem no Firebase)
    JsonObject obj = bikes.as<JsonObject>();
    for (JsonPair bike : obj)
    {
        String bikeId = bike.key().c_str();
        String status = bike.value()["status"] | "";

        // Só incluir se é pending E foi adicionada recentemente (first_seen)
        if (status == "pending" && bike.value()["first_seen"])
        {
            uint32_t firstSeen = bike.value()["first_seen"] | 0;
            uint32_t now = time(nullptr);

            // Só enviar se foi vista pela primeira vez há menos de 5 minutos
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
            // Considerar conectada se heartbeat foi há menos de 5 minutos
            if ((now - lastSeen) < 300)
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

bool BikeManager::downloadBikeRegistry()
{
    HTTPClient http;
    String bikeUrl = configManager.getBikeRegistryUrl();

    Serial.println("🔄 Downloading bike registry from Firebase...");

    http.begin(bikeUrl);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK)
    {
        String payload = http.getString();

        if (payload != "null" && payload.length() > 10)
        {
            DynamicJsonDocument firebaseData(JSON_LARGE_BUFFER);
            if (deserializeJson(firebaseData, payload) == DeserializationError::Ok)
            {
                updateFromFirebase(firebaseData);
                http.end();
                return true;
            }
        }
    }
    
    http.end();
    return false;
}

bool BikeManager::downloadBikeConfigs()
{
    HTTPClient http;
    String configUrl = configManager.getBikeConfigsUrl();

    Serial.println("🔄 Downloading bike configs from Firebase...");

    http.begin(configUrl);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK)
    {
        String payload = http.getString();

        if (payload == "null" || payload.length() < 10)
        {
            Serial.println("📝 No bike configs in Firebase");
            http.end();
            return true;
        }

        DynamicJsonDocument newConfigs(JSON_LARGE_BUFFER);
        if (deserializeJson(newConfigs, payload) == DeserializationError::Ok)
        {
            JsonObjectConst obj = newConfigs.as<JsonObjectConst>();
            for (JsonPairConst bike : obj)
            {
                String bikeId = bike.key().c_str();
                bikes[bikeId]["config"] = bike.value();

                int newVersion = bike.value()["version"] | 1;
                int oldVersion = configVersions[bikeId]["version"] | 0;

                if (newVersion > oldVersion)
                {
                    configChanged[bikeId] = true;
                    configVersions[bikeId]["version"] = newVersion;
                    configVersions[bikeId]["last_update"] = time(nullptr);

                    Serial.printf("🔄 Config changed for %s: v%d → v%d\n",
                                  bikeId.c_str(), oldVersion, newVersion);
                }
            }

            saveData();
            Serial.printf("✅ Downloaded configs for %d bikes\n", newConfigs.size());
            http.end();
            return true;
        }
        else
        {
            Serial.println("❌ Failed to parse bike configs");
        }
    }
    else
    {
        Serial.printf("❌ Failed to download configs: HTTP %d\n", httpCode);
    }

    http.end();
    return false;
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
        Serial.printf("⚠️ No config found for %s, using defaults\n", bikeId.c_str());
        return generateDefaultConfig(bikeId);
    }

    DynamicJsonDocument response(JSON_MEDIUM_BUFFER);
    response["type"] = "config_push";
    response["bike_id"] = bikeId;
    response["config"] = bikes[bikeId]["config"];

    String result;
    serializeJson(response, result);
    return result;
}

String BikeManager::generateDefaultConfig(const String &bikeId)
{
    DynamicJsonDocument response(JSON_MEDIUM_BUFFER);
    response["type"] = "config_push";
    response["bike_id"] = bikeId;

    // Config padrão
    response["config"]["version"] = 1;
    response["config"]["bike_name"] = "Bike " + bikeId;
    response["config"]["dev_mode"] = false;

    response["config"]["wifi"]["scan_interval_sec"] = 300;
    response["config"]["wifi"]["scan_timeout_ms"] = 5000;

    response["config"]["ble"]["base_name"] = "BPR Central";
    response["config"]["ble"]["scan_time_sec"] = 5;

    response["config"]["power"]["deep_sleep_duration_sec"] = 3600;

    response["config"]["battery"]["critical_voltage"] = 3.2;
    response["config"]["battery"]["low_voltage"] = 3.45;

    String result;
    serializeJson(response, result);
    return result;
}

// === HEARTBEAT RESPONSE MANAGEMENT ===
String BikeManager::processHeartbeat(const String &bikeId, const JsonObject &heartbeatData)
{
    if (!dataLoaded)
        return "";

    // Extract heartbeat data
    int batteryPercent = heartbeatData["battery_percent"] | 0;
    int heap = heartbeatData["heap"] | 0;
    bool hasData = heartbeatData["has_data"] | false;
    int configVersion = heartbeatData["config_version"] | 0;

    // Update heartbeat
    updateHeartbeat(bikeId, batteryPercent, heap);

    // Prepare response
    DynamicJsonDocument response(JSON_SMALL_BUFFER);
    response["type"] = "heartbeat_response";
    response["bike_id"] = bikeId;
    response["timestamp"] = time(nullptr);

    // Check if config update needed
    if (hasConfigUpdate(bikeId))
    {
        response["config_update"] = true;
        Serial.printf("⚙️ Config update available for %s\n", bikeId.c_str());
    }

    // Suggest next checkin interval based on battery
    uint32_t nextCheckin = 300; // Default 5min
    if (batteryPercent <= 15)
    {
        nextCheckin = 1200; // 20min for critical battery
    }
    else if (batteryPercent <= 25)
    {
        nextCheckin = 600; // 10min for low battery
    }
    response["next_checkin_sec"] = nextCheckin;

    // Data upload confirmation
    if (hasData)
    {
        response["ready_for_upload"] = true;
        Serial.printf("📤 %s ready for data upload\n", bikeId.c_str());
    }

    String result;
    serializeJson(response, result);
    return result;
}

String BikeManager::confirmDataUpload(const String &bikeId)
{
    DynamicJsonDocument response(JSON_SMALL_BUFFER);
    response["type"] = "upload_confirmed";
    response["bike_id"] = bikeId;
    response["timestamp"] = time(nullptr);
    response["can_clear_buffer"] = true;

    String result;
    serializeJson(response, result);

    Serial.printf("✅ Data upload confirmed for %s - can clear buffer\n", bikeId.c_str());
    return result;
}