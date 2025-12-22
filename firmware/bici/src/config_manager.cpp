#include "config_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

ConfigManager::ConfigManager() {
    // Initialize with defaults
    strcpy(config.bike_id, "");
    strcpy(config.bike_name, "");
    strcpy(config.base_ble_name, CENTRAL_BLE_NAME);
    config.version = 1;
    config.dev_mode = true;
    
    // WiFi defaults
    config.scan_interval_sec = 300;
    config.scan_interval_low_batt_sec = 900;
    config.wifi_scan_timeout_ms = 5000;
    config.wifi_max_networks = 20;
    config.wifi_rssi_threshold = -90;
    
    // BLE defaults
    config.ble_scan_time_sec = 5;
    config.ble_connection_timeout_ms = 10000;
    
    // Power defaults
    config.radio_coordination_delay_ms = 300;
    config.light_sleep_duration_ms = 1000;
    config.deep_sleep_sec = 3600;
    config.max_time_without_base_sec = 7200;
    
    // Battery defaults
    config.battery_critical_voltage = 3.2;
    config.min_battery_voltage = 3.45;
    config.battery_full_voltage = 4.2;
    
    // Timing defaults
    config.status_report_interval_ms = 30000;
    config.emergency_button_hold_ms = 3000;
    
    // Buffer defaults
    config.max_wifi_records = 100;
}

bool ConfigManager::load() {
    Serial.println("📂 Loading config from LittleFS...");
    File file = LittleFS.open("/config.json", "r");
    if (!file) {
        Serial.println("❌ Config file not found");
        return false;
    }
    
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        Serial.printf("❌ JSON parse error: %s\n", error.c_str());
        return false;
    }
    
    // Basic
    strcpy(config.bike_id, doc["bike_id"] | "bici_001");
    strcpy(config.bike_name, doc["bike_name"] | "");
    config.version = doc["version"] | 1;
    config.dev_mode = doc["dev_mode"] | true;
    
    // WiFi
    JsonObject wifi = doc["wifi"];
    config.scan_interval_sec = wifi["scan_interval_sec"] | 300;
    config.scan_interval_low_batt_sec = wifi["scan_interval_low_batt_sec"] | 900;
    config.wifi_scan_timeout_ms = wifi["scan_timeout_ms"] | 5000;
    config.wifi_max_networks = wifi["max_networks"] | 20;
    config.wifi_rssi_threshold = wifi["rssi_threshold"] | -90;
    
    // BLE
    JsonObject ble = doc["ble"];
    strcpy(config.base_ble_name, ble["base_name"] | CENTRAL_BLE_NAME);
    config.ble_scan_time_sec = ble["scan_time_sec"] | 5;
    config.ble_connection_timeout_ms = ble["connection_timeout_ms"] | 10000;
    
    // Power
    JsonObject power = doc["power"];
    config.radio_coordination_delay_ms = power["radio_coordination_delay_ms"] | 300;
    config.light_sleep_duration_ms = power["light_sleep_duration_ms"] | 1000;
    config.deep_sleep_sec = power["deep_sleep_duration_sec"] | 3600;
    config.max_time_without_base_sec = power["max_time_without_base_sec"] | 7200;
    
    // Battery
    JsonObject battery = doc["battery"];
    config.battery_critical_voltage = battery["critical_voltage"] | 3.2;
    config.min_battery_voltage = battery["low_voltage"] | 3.45;
    config.battery_full_voltage = battery["full_voltage"] | 4.2;
    
    // Timing
    JsonObject timing = doc["timing"];
    config.status_report_interval_ms = timing["status_report_interval_ms"] | 30000;
    config.emergency_button_hold_ms = timing["emergency_button_hold_ms"] | 3000;
    
    // Buffers
    JsonObject buffers = doc["buffers"];
    config.max_wifi_records = buffers["max_wifi_records"] | 100;
    
    Serial.printf("✅ Config loaded: %s v%d\n", config.bike_id, config.version);
    return true;
}

void ConfigManager::save() {
    Serial.println("💾 Saving config to LittleFS...");
    DynamicJsonDocument doc(2048);
    
    // Basic
    doc["bike_id"] = config.bike_id;
    doc["bike_name"] = config.bike_name;
    doc["version"] = config.version;
    doc["dev_mode"] = config.dev_mode;
    
    // WiFi
    JsonObject wifi = doc.createNestedObject("wifi");
    wifi["scan_interval_sec"] = config.scan_interval_sec;
    wifi["scan_interval_low_batt_sec"] = config.scan_interval_low_batt_sec;
    wifi["scan_timeout_ms"] = config.wifi_scan_timeout_ms;
    wifi["max_networks"] = config.wifi_max_networks;
    wifi["rssi_threshold"] = config.wifi_rssi_threshold;
    
    // BLE
    JsonObject ble = doc.createNestedObject("ble");
    ble["base_name"] = config.base_ble_name;
    ble["scan_time_sec"] = config.ble_scan_time_sec;
    ble["connection_timeout_ms"] = config.ble_connection_timeout_ms;
    
    // Power
    JsonObject power = doc.createNestedObject("power");
    power["radio_coordination_delay_ms"] = config.radio_coordination_delay_ms;
    power["light_sleep_duration_ms"] = config.light_sleep_duration_ms;
    power["deep_sleep_duration_sec"] = config.deep_sleep_sec;
    power["max_time_without_base_sec"] = config.max_time_without_base_sec;
    
    // Battery
    JsonObject battery = doc.createNestedObject("battery");
    battery["critical_voltage"] = config.battery_critical_voltage;
    battery["low_voltage"] = config.min_battery_voltage;
    battery["full_voltage"] = config.battery_full_voltage;
    
    // Timing
    JsonObject timing = doc.createNestedObject("timing");
    timing["status_report_interval_ms"] = config.status_report_interval_ms;
    timing["emergency_button_hold_ms"] = config.emergency_button_hold_ms;
    
    // Buffers
    JsonObject buffers = doc.createNestedObject("buffers");
    buffers["max_wifi_records"] = config.max_wifi_records;
    
    doc["timestamp"] = millis() / 1000;
    
    File file = LittleFS.open("/config.json", "w");
    if (file) {
        serializeJson(doc, file);
        file.close();
        Serial.println("✅ Config saved successfully");
    } else {
        Serial.println("❌ Failed to save config");
    }
}

void ConfigManager::generateUniqueId() {
    if (strlen(config.bike_id) == 0) {
        String bikeId = generateBikeId();
        strcpy(config.bike_id, bikeId.c_str());
        Serial.printf("🆔 Generated unique ID: %s\n", config.bike_id);
        save();
    } else {
        Serial.printf("🆔 Using existing ID: %s\n", config.bike_id);
    }
}

bool ConfigManager::processUpdate(const String& configJson) {
    Serial.printf("⚙️ Processing config update: %s\n", configJson.c_str());
    
    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, configJson) != DeserializationError::Ok) {
        Serial.println("❌ Invalid config JSON");
        return false;
    }
    
    // Check if this config is for us
    if (doc["target_bike"] && doc["target_bike"] != config.bike_id) {
        Serial.printf("🚫 Config not for us (target: %s, us: %s)\n", 
                      doc["target_bike"].as<String>().c_str(), config.bike_id);
        return false;
    }
    
    JsonObject configData = doc["config"];
    if (!configData) {
        Serial.println("❌ No config data found");
        return false;
    }
    
    bool configChanged = false;
    
    // Update basic config
    if (configData["bike_name"]) {
        strcpy(config.bike_name, configData["bike_name"]);
        configChanged = true;
    }
    if (configData["version"]) {
        config.version = configData["version"];
        configChanged = true;
    }
    if (configData["dev_mode"]) {
        config.dev_mode = configData["dev_mode"];
        configChanged = true;
    }
    
    // Update WiFi config
    if (configData["wifi"]["scan_interval_sec"]) {
        config.scan_interval_sec = configData["wifi"]["scan_interval_sec"];
        configChanged = true;
    }
    if (configData["wifi"]["scan_timeout_ms"]) {
        config.wifi_scan_timeout_ms = configData["wifi"]["scan_timeout_ms"];
        configChanged = true;
    }
    if (configData["wifi"]["max_networks"]) {
        config.wifi_max_networks = configData["wifi"]["max_networks"];
        configChanged = true;
    }
    if (configData["wifi"]["rssi_threshold"]) {
        config.wifi_rssi_threshold = configData["wifi"]["rssi_threshold"];
        configChanged = true;
    }
    
    // Update BLE config
    if (configData["ble"]["base_name"]) {
        strcpy(config.base_ble_name, configData["ble"]["base_name"]);
        configChanged = true;
    }
    if (configData["ble"]["scan_time_sec"]) {
        config.ble_scan_time_sec = configData["ble"]["scan_time_sec"];
        configChanged = true;
    }
    
    // Update power config
    if (configData["power"]["deep_sleep_duration_sec"]) {
        config.deep_sleep_sec = configData["power"]["deep_sleep_duration_sec"];
        configChanged = true;
    }
    if (configData["power"]["radio_coordination_delay_ms"]) {
        config.radio_coordination_delay_ms = configData["power"]["radio_coordination_delay_ms"];
        configChanged = true;
    }
    
    // Update battery config
    if (configData["battery"]["critical_voltage"]) {
        config.battery_critical_voltage = configData["battery"]["critical_voltage"];
        configChanged = true;
    }
    if (configData["battery"]["low_voltage"]) {
        config.min_battery_voltage = configData["battery"]["low_voltage"];
        configChanged = true;
    }
    
    if (configChanged) {
        Serial.printf("✅ Config updated: %s v%d\n", config.bike_name, config.version);
        save();
        return true;
    } else {
        Serial.println("📝 No config changes detected");
        return false;
    }
}

Config& ConfigManager::getConfig() {
    return config;
}