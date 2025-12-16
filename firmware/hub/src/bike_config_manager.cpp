#include "bike_config_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include "constants.h"
#include "config_manager.h"

extern ConfigManager configManager;

static DynamicJsonDocument bikeConfigs(4096);
static DynamicJsonDocument configVersions(1024);
static std::map<String, bool> configChanged;
static bool configsLoaded = false;

bool BikeConfigManager::init() {
    return loadConfigCache();
}

bool BikeConfigManager::loadConfigCache() {
    // Carregar cache de versões
    if (LittleFS.exists(BIKE_CONFIG_CACHE_FILE)) {
        File file = LittleFS.open(BIKE_CONFIG_CACHE_FILE, "r");
        if (file) {
            deserializeJson(configVersions, file);
            file.close();
            Serial.printf("✅ Bike config versions loaded: %d bikes\n", configVersions.size());
        }
    }
    
    // Carregar configs completas
    if (LittleFS.exists(BIKE_CONFIGS_FILE)) {
        File file = LittleFS.open(BIKE_CONFIGS_FILE, "r");
        if (file) {
            deserializeJson(bikeConfigs, file);
            file.close();
            Serial.printf("✅ Bike configs loaded: %d bikes\n", bikeConfigs.size());
        }
    }
    
    configsLoaded = true;
    return true;
}

bool BikeConfigManager::saveConfigCache() {
    // Salvar versões
    File file = LittleFS.open(BIKE_CONFIG_CACHE_FILE, "w");
    if (file) {
        serializeJson(configVersions, file);
        file.close();
    }
    
    // Salvar configs completas
    file = LittleFS.open(BIKE_CONFIGS_FILE, "w");
    if (file) {
        serializeJson(bikeConfigs, file);
        file.close();
    }
    
    Serial.println("💾 Bike configs saved to cache");
    return true;
}

bool BikeConfigManager::downloadConfigsFromFirebase() {
    HTTPClient http;
    const HubConfig& config = configManager.getConfig();
    
    String url = String(config.firebase.database_url) + 
                "/bike_configs.json?auth=" + config.firebase.api_key;
    
    Serial.println("🔄 Downloading bike configs from Firebase...");
    
    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        
        if (payload == "null" || payload.length() < 10) {
            Serial.println("📝 No bike configs in Firebase");
            http.end();
            return true;
        }
        
        DynamicJsonDocument newConfigs(4096);
        if (deserializeJson(newConfigs, payload) == DeserializationError::Ok) {
            checkForConfigChanges(newConfigs);
            bikeConfigs = newConfigs;
            saveConfigCache();
            
            Serial.printf("✅ Downloaded configs for %d bikes\n", bikeConfigs.size());
            http.end();
            return true;
        } else {
            Serial.println("❌ Failed to parse bike configs");
        }
    } else {
        Serial.printf("❌ Failed to download configs: HTTP %d\n", httpCode);
    }
    
    http.end();
    return false;
}

void BikeConfigManager::checkForConfigChanges(const DynamicJsonDocument& newConfigs) {
    JsonObjectConst obj = newConfigs.as<JsonObjectConst>();
    for (JsonPairConst bike : obj) {
        String bikeId = bike.key().c_str();
        int newVersion = bike.value()["version"] | 1;
        int oldVersion = configVersions[bikeId]["version"] | 0;
        
        if (newVersion > oldVersion) {
            configChanged[bikeId] = true;
            configVersions[bikeId]["version"] = newVersion;
            configVersions[bikeId]["last_update"] = time(nullptr);
            
            Serial.printf("🔄 Config changed for %s: v%d → v%d\n", 
                         bikeId.c_str(), oldVersion, newVersion);
        }
    }
}

bool BikeConfigManager::hasConfigUpdate(const String& bikeId) {
    return configChanged.find(bikeId) != configChanged.end() && configChanged[bikeId];
}

void BikeConfigManager::markConfigSent(const String& bikeId) {
    configChanged[bikeId] = false;
    Serial.printf("✅ Config marked as sent for %s\n", bikeId.c_str());
}

String BikeConfigManager::getConfigForBike(const String& bikeId) {
    if (!configsLoaded || !bikeConfigs.containsKey(bikeId)) {
        Serial.printf("⚠️ No config found for %s, using defaults\n", bikeId.c_str());
        return generateDefaultConfig(bikeId);
    }
    
    DynamicJsonDocument response(1024);
    response["type"] = "config_push";
    response["bike_id"] = bikeId;
    response["config"] = bikeConfigs[bikeId];
    
    String result;
    serializeJson(response, result);
    return result;
}

String BikeConfigManager::generateDefaultConfig(const String& bikeId) {
    DynamicJsonDocument response(1024);
    response["type"] = "config_push";
    response["bike_id"] = bikeId;
    
    // Config padrão
    response["config"]["version"] = 1;
    response["config"]["bike_name"] = "Bike " + bikeId;
    response["config"]["dev_mode"] = false;
    
    response["config"]["wifi"]["scan_interval_sec"] = 300;
    response["config"]["wifi"]["scan_timeout_ms"] = 5000;
    
    response["config"]["ble"]["base_name"] = "BPR Hub Station";
    response["config"]["ble"]["scan_time_sec"] = 5;
    
    response["config"]["power"]["deep_sleep_duration_sec"] = 3600;
    
    response["config"]["battery"]["critical_voltage"] = 3.2;
    response["config"]["battery"]["low_voltage"] = 3.45;
    
    String result;
    serializeJson(response, result);
    return result;
}

void BikeConfigManager::pushConfigToBike(const String& bikeId, NimBLECharacteristic* pConfigChar) {
    if (!hasConfigUpdate(bikeId)) return;
    
    String config = getConfigForBike(bikeId);
    pConfigChar->setValue(config.c_str());
    pConfigChar->notify();
    
    markConfigSent(bikeId);
    Serial.printf("📤 Config pushed to %s\n", bikeId.c_str());
}

std::vector<String> BikeConfigManager::getBikesWithUpdates() {
    std::vector<String> bikes;
    for (auto& pair : configChanged) {
        if (pair.second) {
            bikes.push_back(pair.first);
        }
    }
    return bikes;
}