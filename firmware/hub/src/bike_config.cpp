#include "bike_config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config_manager.h"
#include "buffer_manager.h"

extern ConfigManager configManager;
extern BufferManager bufferManager;

static DynamicJsonDocument whitelist(1024);
static bool whitelistLoaded = false;

void BikeConfigManager::init() {
    loadWhitelist();
}

bool BikeConfigManager::handleConfigRequest(const String& bikeId, String& response) {
    Serial.printf("🚲 Config request from: %s\n", bikeId.c_str());
    
    // Check authorization
    if (!isBikeAuthorized(bikeId)) {
        Serial.printf("❌ Bike %s not authorized\n", bikeId.c_str());
        logConfigAttempt(bikeId, false);
        response = "{\"error\":\"not_authorized\"}";
        return false;
    }
    
    // Get config (Firebase or default)
    BikeConfigData config = getDefaultConfig(bikeId);
    
    // Try to get specific config from Firebase
    if (!checkFirebaseConfig(bikeId, config)) {
        Serial.printf("⚠️ Using default config for %s\n", bikeId.c_str());
    }
    
    response = generateConfigJson(config);
    logConfigAttempt(bikeId, true);
    
    Serial.printf("✅ Config sent to %s\n", bikeId.c_str());
    return true;
}

bool BikeConfigManager::isBikeAuthorized(const String& bikeId) {
    if (!whitelistLoaded) {
        loadWhitelist();
    }
    
    // Check whitelist
    if (whitelist.containsKey("bikes")) {
        JsonArray bikes = whitelist["bikes"];
        for (JsonVariant bike : bikes) {
            if (bike.as<String>() == bikeId) {
                return true;
            }
        }
    }
    
    // Check auto-approval for BPR prefix
    if (bikeId.startsWith("bpr-") || bikeId.startsWith("BPR-")) {
        bool autoApprove = whitelist["auto_approve_bpr"] | false;
        return autoApprove;
    }
    
    return false;
}

void BikeConfigManager::logConfigAttempt(const String& bikeId, bool authorized) {
    bufferManager.addConfigLog(bikeId, authorized);
}

BikeConfigData BikeConfigManager::getDefaultConfig(const String& bikeId) {
    BikeConfigData config;
    
    // Basic info
    strncpy(config.bike_id, bikeId.c_str(), sizeof(config.bike_id) - 1);
    snprintf(config.bike_name, sizeof(config.bike_name), "Bike %s", bikeId.c_str());
    config.version = 1;
    config.dev_mode = false;
    
    // WiFi settings
    config.wifi.scan_interval_sec = 300;
    config.wifi.scan_timeout_ms = 5000;
    
    // BLE settings
    strncpy(config.ble.base_name, "BPR Hub Station", sizeof(config.ble.base_name) - 1);
    config.ble.scan_time_sec = 5;
    
    // Power settings
    config.power.deep_sleep_duration_sec = 3600;
    
    // Battery settings
    config.battery.critical_voltage = 3.2;
    config.battery.low_voltage = 3.45;
    
    return config;
}

String BikeConfigManager::generateConfigJson(const BikeConfigData& config) {
    DynamicJsonDocument doc(1024);
    
    doc["bike_id"] = config.bike_id;
    doc["bike_name"] = config.bike_name;
    doc["version"] = config.version;
    doc["dev_mode"] = config.dev_mode;
    
    doc["wifi"]["scan_interval_sec"] = config.wifi.scan_interval_sec;
    doc["wifi"]["scan_timeout_ms"] = config.wifi.scan_timeout_ms;
    
    doc["ble"]["base_name"] = config.ble.base_name;
    doc["ble"]["scan_time_sec"] = config.ble.scan_time_sec;
    
    doc["power"]["deep_sleep_duration_sec"] = config.power.deep_sleep_duration_sec;
    
    doc["battery"]["critical_voltage"] = config.battery.critical_voltage;
    doc["battery"]["low_voltage"] = config.battery.low_voltage;
    
    String result;
    serializeJson(doc, result);
    return result;
}

bool BikeConfigManager::loadWhitelist() {
    if (!LittleFS.exists("/whitelist.json")) {
        // Create default whitelist
        whitelist["auto_approve_bpr"] = true;
        whitelist["bikes"] = JsonArray();
        
        File file = LittleFS.open("/whitelist.json", "w");
        if (file) {
            serializeJson(whitelist, file);
            file.close();
        }
        
        whitelistLoaded = true;
        Serial.println("📋 Created default whitelist");
        return true;
    }
    
    File file = LittleFS.open("/whitelist.json", "r");
    if (!file) {
        Serial.println("❌ Failed to open whitelist");
        return false;
    }
    
    DeserializationError error = deserializeJson(whitelist, file);
    file.close();
    
    if (error) {
        Serial.printf("❌ Whitelist parse error: %s\n", error.c_str());
        return false;
    }
    
    whitelistLoaded = true;
    Serial.println("✅ Whitelist loaded");
    return true;
}

bool BikeConfigManager::checkFirebaseConfig(const String& bikeId, BikeConfigData& config) {
    // This would fetch specific bike config from Firebase
    // Path: /bike_configs/{bike_id}
    // For now, return false to use defaults
    // TODO: Implement Firebase fetch during WiFi sync
    Serial.printf("🔍 Checking Firebase config for %s (not implemented)\n", bikeId.c_str());
    return false;
}