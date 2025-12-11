#include "config_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "constants.h"

ConfigManager::ConfigManager() {
    // Initialize with defaults
    strcpy(config.base_id, "hub_default");
    strcpy(config.wifi.ssid, "");
    strcpy(config.wifi.password, "");
    config.wifi.timeout_ms = WIFI_TIMEOUT_DEFAULT;
    
    strcpy(config.firebase.project_id, "");
    strcpy(config.firebase.database_url, "");
    strcpy(config.firebase.api_key, "");
    
    config.led.pin = LED_PIN;
    config.led.boot_ms = LED_BOOT_INTERVAL;
    config.led.ble_ms = LED_BLE_INTERVAL;
    config.led.sync_ms = LED_SYNC_INTERVAL;
    config.led.error_ms = LED_ERROR_INTERVAL;
    config.led.count_ms = LED_COUNT_INTERVAL;
    config.led.count_pause_ms = LED_COUNT_PAUSE;
    
    config.sync_interval_ms = SYNC_INTERVAL_DEFAULT;
    config.max_buffer_size = MAX_BUFFER_SIZE;
    strcpy(config.ntp_server, "pool.ntp.org");
    config.timezone_offset = -10800; // GMT-3
    config.auto_approve_bikes = true;
    config.max_bikes = MAX_BIKES;
}

bool ConfigManager::loadConfig() {
    if (!LittleFS.exists(CONFIG_FILE)) {
        Serial.println("📄 Config file not found, using defaults");
        return false;
    }
    
    File file = LittleFS.open(CONFIG_FILE, "r");
    if (!file) {
        Serial.println("❌ Failed to open config file");
        return false;
    }
    
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        Serial.printf("❌ Config parse error: %s\n", error.c_str());
        return false;
    }
    
    // Load config from JSON
    if (doc["base_id"]) strcpy(config.base_id, doc["base_id"]);
    if (doc["wifi"]["ssid"]) strcpy(config.wifi.ssid, doc["wifi"]["ssid"]);
    if (doc["wifi"]["password"]) strcpy(config.wifi.password, doc["wifi"]["password"]);
    if (doc["wifi"]["timeout_ms"]) config.wifi.timeout_ms = doc["wifi"]["timeout_ms"];
    
    if (doc["firebase"]["project_id"]) strcpy(config.firebase.project_id, doc["firebase"]["project_id"]);
    if (doc["firebase"]["database_url"]) strcpy(config.firebase.database_url, doc["firebase"]["database_url"]);
    if (doc["firebase"]["api_key"]) strcpy(config.firebase.api_key, doc["firebase"]["api_key"]);
    
    if (doc["sync_interval_ms"]) config.sync_interval_ms = doc["sync_interval_ms"];
    if (doc["max_buffer_size"]) config.max_buffer_size = doc["max_buffer_size"];
    if (doc["ntp_server"]) strcpy(config.ntp_server, doc["ntp_server"]);
    if (doc["timezone_offset"]) config.timezone_offset = doc["timezone_offset"];
    if (doc["auto_approve_bikes"]) config.auto_approve_bikes = doc["auto_approve_bikes"];
    if (doc["max_bikes"]) config.max_bikes = doc["max_bikes"];
    
    Serial.printf("✅ Config loaded for base: %s\n", config.base_id);
    return isConfigValid();
}

bool ConfigManager::saveConfig() {
    DynamicJsonDocument doc(2048);
    
    doc["base_id"] = config.base_id;
    doc["wifi"]["ssid"] = config.wifi.ssid;
    doc["wifi"]["password"] = config.wifi.password;
    doc["wifi"]["timeout_ms"] = config.wifi.timeout_ms;
    
    doc["firebase"]["project_id"] = config.firebase.project_id;
    doc["firebase"]["database_url"] = config.firebase.database_url;
    doc["firebase"]["api_key"] = config.firebase.api_key;
    
    doc["sync_interval_ms"] = config.sync_interval_ms;
    doc["max_buffer_size"] = config.max_buffer_size;
    doc["ntp_server"] = config.ntp_server;
    doc["timezone_offset"] = config.timezone_offset;
    doc["auto_approve_bikes"] = config.auto_approve_bikes;
    doc["max_bikes"] = config.max_bikes;
    
    File file = LittleFS.open(CONFIG_FILE, "w");
    if (!file) {
        Serial.println("❌ Failed to create config file");
        return false;
    }
    
    serializeJson(doc, file);
    file.close();
    
    Serial.println("💾 Config saved");
    return true;
}

bool ConfigManager::isConfigValid() {
    return strlen(config.base_id) > 0 && 
           strlen(config.wifi.ssid) > 0 && 
           strlen(config.firebase.project_id) > 0;
}

void ConfigManager::updateFromFirebase(const DynamicJsonDocument& firebaseConfig) {
    if (firebaseConfig["sync_interval_ms"]) {
        config.sync_interval_ms = firebaseConfig["sync_interval_ms"];
    }
    if (firebaseConfig["max_buffer_size"]) {
        config.max_buffer_size = firebaseConfig["max_buffer_size"];
    }
    if (firebaseConfig["auto_approve_bikes"]) {
        config.auto_approve_bikes = firebaseConfig["auto_approve_bikes"];
    }
    if (firebaseConfig["led"]["ble_ms"]) {
        config.led.ble_ms = firebaseConfig["led"]["ble_ms"];
    }
    if (firebaseConfig["led"]["sync_ms"]) {
        config.led.sync_ms = firebaseConfig["led"]["sync_ms"];
    }
    
    saveConfig();
    Serial.println("🔄 Config updated from Firebase");
}