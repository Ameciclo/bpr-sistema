#include "config_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "constants.h"

ConfigManager::ConfigManager() {
    // Initialize with defaults
    strcpy(config.base_id, "hub_default");
    
    config.location.lat = 0.0;
    config.location.lng = 0.0;
    
    strcpy(config.wifi.ssid, "");
    strcpy(config.wifi.password, "");
    config.wifi.timeout_ms = WIFI_TIMEOUT_DEFAULT;
    
    strcpy(config.firebase.project_id, "");
    strcpy(config.firebase.database_url, "");
    strcpy(config.firebase.api_key, "");
    
    config.intervals.sync_sec = 300;
    config.intervals.cleanup_sec = 60;
    config.intervals.log_sec = 15;
    config.intervals.led_count_sec = 30;
    
    config.timeouts.wifi_sec = 30;
    config.timeouts.firebase_ms = 10000;
    
    config.led.pin = LED_PIN;
    config.led.boot_ms = LED_BOOT_INTERVAL;
    config.led.ble_ms = LED_BLE_INTERVAL;
    config.led.sync_ms = LED_SYNC_INTERVAL;
    config.led.error_ms = LED_ERROR_INTERVAL;
    config.led.count_ms = LED_COUNT_INTERVAL;
    config.led.count_pause_ms = LED_COUNT_PAUSE;
    config.led.bike_arrived_ms = 150;
    config.led.bike_left_ms = 800;
    
    config.limits.max_bikes = MAX_BIKES;
    config.limits.batch_size = MAX_BUFFER_SIZE;
    
    config.fallback.max_failures = MAX_SYNC_FAILURES;
    config.fallback.timeout_min = SYNC_FAILURE_TIMEOUT_MS / 60000;
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
    
    if (doc["location"]["lat"]) config.location.lat = doc["location"]["lat"];
    if (doc["location"]["lng"]) config.location.lng = doc["location"]["lng"];
    
    if (doc["wifi"]["ssid"]) strcpy(config.wifi.ssid, doc["wifi"]["ssid"]);
    if (doc["wifi"]["password"]) strcpy(config.wifi.password, doc["wifi"]["password"]);
    
    if (doc["firebase"]["project_id"]) strcpy(config.firebase.project_id, doc["firebase"]["project_id"]);
    if (doc["firebase"]["database_url"]) strcpy(config.firebase.database_url, doc["firebase"]["database_url"]);
    if (doc["firebase"]["api_key"]) strcpy(config.firebase.api_key, doc["firebase"]["api_key"]);
    
    if (doc["intervals"]["sync_sec"]) config.intervals.sync_sec = doc["intervals"]["sync_sec"];
    if (doc["intervals"]["cleanup_sec"]) config.intervals.cleanup_sec = doc["intervals"]["cleanup_sec"];
    if (doc["intervals"]["log_sec"]) config.intervals.log_sec = doc["intervals"]["log_sec"];
    if (doc["intervals"]["led_count_sec"]) config.intervals.led_count_sec = doc["intervals"]["led_count_sec"];
    
    if (doc["timeouts"]["wifi_sec"]) config.timeouts.wifi_sec = doc["timeouts"]["wifi_sec"];
    if (doc["timeouts"]["firebase_ms"]) config.timeouts.firebase_ms = doc["timeouts"]["firebase_ms"];
    
    if (doc["led"]["boot_ms"]) config.led.boot_ms = doc["led"]["boot_ms"];
    if (doc["led"]["ble_ready_ms"]) config.led.ble_ms = doc["led"]["ble_ready_ms"];
    if (doc["led"]["wifi_sync_ms"]) config.led.sync_ms = doc["led"]["wifi_sync_ms"];
    if (doc["led"]["bike_arrived_ms"]) config.led.bike_arrived_ms = doc["led"]["bike_arrived_ms"];
    if (doc["led"]["bike_left_ms"]) config.led.bike_left_ms = doc["led"]["bike_left_ms"];
    if (doc["led"]["count_ms"]) config.led.count_ms = doc["led"]["count_ms"];
    if (doc["led"]["count_pause_ms"]) config.led.count_pause_ms = doc["led"]["count_pause_ms"];
    if (doc["led"]["error_ms"]) config.led.error_ms = doc["led"]["error_ms"];
    
    if (doc["limits"]["max_bikes"]) config.limits.max_bikes = doc["limits"]["max_bikes"];
    if (doc["limits"]["batch_size"]) config.limits.batch_size = doc["limits"]["batch_size"];
    
    if (doc["fallback"]["max_failures"]) config.fallback.max_failures = doc["fallback"]["max_failures"];
    if (doc["fallback"]["timeout_min"]) config.fallback.timeout_min = doc["fallback"]["timeout_min"];
    
    Serial.printf("✅ Config carregada do arquivo:\n");
    Serial.printf("   Base ID: %s\n", config.base_id);
    Serial.printf("   WiFi: %s\n", config.wifi.ssid);
    Serial.printf("   Firebase: %s\n", config.firebase.database_url);
    
    return isConfigValid();
}

bool ConfigManager::saveConfig() {
    DynamicJsonDocument doc(2048);
    
    doc["base_id"] = config.base_id;
    
    doc["location"]["lat"] = config.location.lat;
    doc["location"]["lng"] = config.location.lng;
    
    doc["wifi"]["ssid"] = config.wifi.ssid;
    doc["wifi"]["password"] = config.wifi.password;
    
    doc["firebase"]["project_id"] = config.firebase.project_id;
    doc["firebase"]["database_url"] = config.firebase.database_url;
    doc["firebase"]["api_key"] = config.firebase.api_key;
    
    doc["intervals"]["sync_sec"] = config.intervals.sync_sec;
    doc["intervals"]["cleanup_sec"] = config.intervals.cleanup_sec;
    doc["intervals"]["log_sec"] = config.intervals.log_sec;
    doc["intervals"]["led_count_sec"] = config.intervals.led_count_sec;
    
    doc["timeouts"]["wifi_sec"] = config.timeouts.wifi_sec;
    doc["timeouts"]["firebase_ms"] = config.timeouts.firebase_ms;
    
    doc["led"]["boot_ms"] = config.led.boot_ms;
    doc["led"]["ble_ready_ms"] = config.led.ble_ms;
    doc["led"]["wifi_sync_ms"] = config.led.sync_ms;
    doc["led"]["bike_arrived_ms"] = config.led.bike_arrived_ms;
    doc["led"]["bike_left_ms"] = config.led.bike_left_ms;
    doc["led"]["count_ms"] = config.led.count_ms;
    doc["led"]["count_pause_ms"] = config.led.count_pause_ms;
    doc["led"]["error_ms"] = config.led.error_ms;
    
    doc["limits"]["max_bikes"] = config.limits.max_bikes;
    doc["limits"]["batch_size"] = config.limits.batch_size;
    
    doc["fallback"]["max_failures"] = config.fallback.max_failures;
    doc["fallback"]["timeout_min"] = config.fallback.timeout_min;
    
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
    bool valid = strlen(config.base_id) > 0 && 
                strlen(config.wifi.ssid) > 0 && 
                strlen(config.firebase.database_url) > 0 &&
                strlen(config.firebase.api_key) > 0;
    
    Serial.printf("🔍 Validação da config: %s\n", valid ? "✅ VÁLIDA" : "❌ INVÁLIDA");
    Serial.printf("   Base ID: %s\n", strlen(config.base_id) > 0 ? "✅" : "❌");
    Serial.printf("   WiFi SSID: %s\n", strlen(config.wifi.ssid) > 0 ? "✅" : "❌");
    Serial.printf("   Firebase URL: %s\n", strlen(config.firebase.database_url) > 0 ? "✅" : "❌");
    Serial.printf("   Firebase Key: %s\n", strlen(config.firebase.api_key) > 0 ? "✅" : "❌");
    
    return valid;
}

void ConfigManager::updateFromFirebase(const DynamicJsonDocument& firebaseConfig) {
    // Atualizar todos os campos do Firebase
    if (firebaseConfig["location"]["lat"]) config.location.lat = firebaseConfig["location"]["lat"];
    if (firebaseConfig["location"]["lng"]) config.location.lng = firebaseConfig["location"]["lng"];
    
    if (firebaseConfig["wifi"]["ssid"]) strcpy(config.wifi.ssid, firebaseConfig["wifi"]["ssid"]);
    if (firebaseConfig["wifi"]["password"]) strcpy(config.wifi.password, firebaseConfig["wifi"]["password"]);
    
    if (firebaseConfig["intervals"]["sync_sec"]) config.intervals.sync_sec = firebaseConfig["intervals"]["sync_sec"];
    if (firebaseConfig["intervals"]["cleanup_sec"]) config.intervals.cleanup_sec = firebaseConfig["intervals"]["cleanup_sec"];
    if (firebaseConfig["intervals"]["log_sec"]) config.intervals.log_sec = firebaseConfig["intervals"]["log_sec"];
    if (firebaseConfig["intervals"]["led_count_sec"]) config.intervals.led_count_sec = firebaseConfig["intervals"]["led_count_sec"];
    
    if (firebaseConfig["timeouts"]["wifi_sec"]) {
        config.timeouts.wifi_sec = firebaseConfig["timeouts"]["wifi_sec"];
        config.wifi.timeout_ms = config.timeouts.wifi_sec * 1000;
    }
    if (firebaseConfig["timeouts"]["firebase_ms"]) config.timeouts.firebase_ms = firebaseConfig["timeouts"]["firebase_ms"];
    
    if (firebaseConfig["led"]["boot_ms"]) config.led.boot_ms = firebaseConfig["led"]["boot_ms"];
    if (firebaseConfig["led"]["ble_ready_ms"]) config.led.ble_ms = firebaseConfig["led"]["ble_ready_ms"];
    if (firebaseConfig["led"]["wifi_sync_ms"]) config.led.sync_ms = firebaseConfig["led"]["wifi_sync_ms"];
    if (firebaseConfig["led"]["bike_arrived_ms"]) config.led.bike_arrived_ms = firebaseConfig["led"]["bike_arrived_ms"];
    if (firebaseConfig["led"]["bike_left_ms"]) config.led.bike_left_ms = firebaseConfig["led"]["bike_left_ms"];
    if (firebaseConfig["led"]["count_ms"]) config.led.count_ms = firebaseConfig["led"]["count_ms"];
    if (firebaseConfig["led"]["count_pause_ms"]) config.led.count_pause_ms = firebaseConfig["led"]["count_pause_ms"];
    if (firebaseConfig["led"]["error_ms"]) config.led.error_ms = firebaseConfig["led"]["error_ms"];
    
    if (firebaseConfig["limits"]["max_bikes"]) config.limits.max_bikes = firebaseConfig["limits"]["max_bikes"];
    if (firebaseConfig["limits"]["batch_size"]) config.limits.batch_size = firebaseConfig["limits"]["batch_size"];
    
    if (firebaseConfig["fallback"]["max_failures"]) config.fallback.max_failures = firebaseConfig["fallback"]["max_failures"];
    if (firebaseConfig["fallback"]["timeout_min"]) config.fallback.timeout_min = firebaseConfig["fallback"]["timeout_min"];
    
    saveConfig();
    Serial.println("🔄 Config atualizada e salva localmente");
    Serial.printf("   Sync interval: %d segundos\n", config.intervals.sync_sec);
    Serial.printf("   WiFi timeout: %d segundos\n", config.timeouts.wifi_sec);
    Serial.printf("   LED count interval: %d segundos\n", config.intervals.led_count_sec);
    Serial.printf("   Fallback: %d falhas ou %d min\n", config.fallback.max_failures, config.fallback.timeout_min);
}