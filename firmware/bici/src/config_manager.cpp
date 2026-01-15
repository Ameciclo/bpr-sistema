#include "config_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

ConfigManager::ConfigManager() {
    // Initialize with FSD defaults
    config.version = 2;
    strcpy(config.bike_id, "");
    strcpy(config.bike_name, "");
    config.dev_mode = true;
    
    // WiFi defaults
    config.wifi.scan_interval_sec = 300;
    config.wifi.scan_timeout_ms = 5000;
    config.wifi.max_networks = 20;
    config.wifi.rssi_threshold = -90;
    
    // BLE defaults
    strcpy(config.ble.base_name, CENTRAL_BLE_NAME);
    config.ble.scan_time_sec = 5;
    config.ble.connection_timeout_ms = 10000;
    
    // Power defaults
    config.power.deep_sleep_duration_sec = 3600;
    config.power.radio_coordination_delay_ms = 300;
    
    // Battery defaults
    config.battery.critical_voltage = 3.2;
    config.battery.low_voltage = 3.45;
    
    config.timestamp = 0;
}

bool ConfigManager::load() {
    Serial.println("📂 Loading binary config...");
    File file = LittleFS.open("/config.bin", "r");
    if (!file) {
        Serial.println("❌ Config file not found");
        return false;
    }
    
    size_t bytesRead = file.readBytes((char*)&config, sizeof(BikeConfig));
    file.close();
    
    if (bytesRead != sizeof(BikeConfig)) {
        Serial.printf("❌ Config size mismatch: %d != %d\n", bytesRead, sizeof(BikeConfig));
        return false;
    }
    
    Serial.printf("✅ Binary config loaded: %s v%d\n", config.bike_id, config.version);
    return true;
}

void ConfigManager::save() {
    Serial.println("💾 Saving binary config...");
    config.timestamp = millis() / 1000;
    
    File file = LittleFS.open("/config.bin", "w");
    if (file) {
        size_t bytesWritten = file.write((uint8_t*)&config, sizeof(BikeConfig));
        file.close();
        
        if (bytesWritten == sizeof(BikeConfig)) {
            Serial.printf("✅ Binary config saved (%d bytes)\n", bytesWritten);
        } else {
            Serial.printf("❌ Config write failed: %d/%d bytes\n", bytesWritten, sizeof(BikeConfig));
        }
    } else {
        Serial.println("❌ Failed to open config file");
    }
}

void ConfigManager::generateUniqueId() {
    String bikeId = generateBikeId();
    strncpy(config.bike_id, bikeId.c_str(), 15);
    config.bike_id[15] = '\0';
    Serial.printf("🆔 Generated bike ID: %s\n", config.bike_id);
    save();
}

bool ConfigManager::processUpdate(const String& configJson) {
    Serial.printf("⚙️ Processing config update: %s\n", configJson.c_str());
    
    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, configJson) != DeserializationError::Ok) {
        Serial.println("❌ Invalid config JSON");
        return false;
    }
    
    bool configChanged = false;
    
    // Update WiFi config
    if (doc["wifi"]["scan_interval_sec"]) {
        config.wifi.scan_interval_sec = doc["wifi"]["scan_interval_sec"];
        configChanged = true;
    }
    if (doc["wifi"]["scan_timeout_ms"]) {
        config.wifi.scan_timeout_ms = doc["wifi"]["scan_timeout_ms"];
        configChanged = true;
    }
    if (doc["wifi"]["max_networks"]) {
        config.wifi.max_networks = doc["wifi"]["max_networks"];
        configChanged = true;
    }
    if (doc["wifi"]["rssi_threshold"]) {
        config.wifi.rssi_threshold = doc["wifi"]["rssi_threshold"];
        configChanged = true;
    }
    
    // Update BLE config
    if (doc["ble"]["base_name"]) {
        strncpy(config.ble.base_name, doc["ble"]["base_name"], 31);
        config.ble.base_name[31] = '\0';
        configChanged = true;
    }
    if (doc["ble"]["scan_time_sec"]) {
        config.ble.scan_time_sec = doc["ble"]["scan_time_sec"];
        configChanged = true;
    }
    
    // Update power config
    if (doc["power"]["deep_sleep_duration_sec"]) {
        config.power.deep_sleep_duration_sec = doc["power"]["deep_sleep_duration_sec"];
        configChanged = true;
    }
    
    // Update battery config
    if (doc["battery"]["critical_voltage"]) {
        config.battery.critical_voltage = doc["battery"]["critical_voltage"];
        configChanged = true;
    }
    if (doc["battery"]["low_voltage"]) {
        config.battery.low_voltage = doc["battery"]["low_voltage"];
        configChanged = true;
    }
    
    if (configChanged) {
        config.version++;
        Serial.printf("✅ Config updated: %s v%d\n", config.bike_name, config.version);
        save();
        return true;
    }
    
    return false;
}

bool ConfigManager::isValid() {
    return (strlen(config.bike_id) > 0 && 
            config.version > 0 &&
            config.wifi.scan_interval_sec > 0 &&
            config.wifi.scan_timeout_ms > 0);
}

BikeConfig& ConfigManager::getConfig() {
    return config;
}