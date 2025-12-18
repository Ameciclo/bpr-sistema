#include "config_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "constants.h"

ConfigManager::ConfigManager() {
    // Initialize with defaults
    strcpy(config.base_id, "central_default");
    
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
    config.timeouts.config_ap_min = 15;
    
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
    
    // Buffer defaults
    config.buffer.max_size = 50;
    config.buffer.sync_threshold_percent = 80;
    config.buffer.auto_save_interval = 5;
    config.buffer.max_item_size = 256;
    
    // Compression defaults
    config.compression.enabled = false;
    config.compression.min_size_bytes = 64;
    
    // Storage defaults
    config.storage.min_free_kb = 20;
    config.storage.warning_threshold_kb = 10;
    config.storage.aggressive_cleanup_multiplier = 0.5;
    
    // Backup defaults
    config.backup.enabled = true;
    config.backup.retention_hours = 24;
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
    if (doc["timeouts"]["config_ap_min"]) config.timeouts.config_ap_min = doc["timeouts"]["config_ap_min"];
    
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
    
    // Buffer config
    if (doc["buffer"]["max_size"]) config.buffer.max_size = doc["buffer"]["max_size"];
    if (doc["buffer"]["sync_threshold_percent"]) config.buffer.sync_threshold_percent = doc["buffer"]["sync_threshold_percent"];
    if (doc["buffer"]["auto_save_interval"]) config.buffer.auto_save_interval = doc["buffer"]["auto_save_interval"];
    if (doc["buffer"]["max_item_size"]) config.buffer.max_item_size = doc["buffer"]["max_item_size"];
    
    // Compression config
    if (doc["compression"]["enabled"]) config.compression.enabled = doc["compression"]["enabled"];
    if (doc["compression"]["min_size_bytes"]) config.compression.min_size_bytes = doc["compression"]["min_size_bytes"];
    
    // Storage config
    if (doc["storage"]["min_free_kb"]) config.storage.min_free_kb = doc["storage"]["min_free_kb"];
    if (doc["storage"]["warning_threshold_kb"]) config.storage.warning_threshold_kb = doc["storage"]["warning_threshold_kb"];
    if (doc["storage"]["aggressive_cleanup_multiplier"]) config.storage.aggressive_cleanup_multiplier = doc["storage"]["aggressive_cleanup_multiplier"];
    
    // Backup config
    if (doc["backup"]["enabled"]) config.backup.enabled = doc["backup"]["enabled"];
    if (doc["backup"]["retention_hours"]) config.backup.retention_hours = doc["backup"]["retention_hours"];
    
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
    doc["timeouts"]["config_ap_min"] = config.timeouts.config_ap_min;
    
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
    
    doc["buffer"]["max_size"] = config.buffer.max_size;
    doc["buffer"]["sync_threshold_percent"] = config.buffer.sync_threshold_percent;
    doc["buffer"]["auto_save_interval"] = config.buffer.auto_save_interval;
    doc["buffer"]["max_item_size"] = config.buffer.max_item_size;
    
    doc["compression"]["enabled"] = config.compression.enabled;
    doc["compression"]["min_size_bytes"] = config.compression.min_size_bytes;
    
    doc["storage"]["min_free_kb"] = config.storage.min_free_kb;
    doc["storage"]["warning_threshold_kb"] = config.storage.warning_threshold_kb;
    doc["storage"]["aggressive_cleanup_multiplier"] = config.storage.aggressive_cleanup_multiplier;
    
    doc["backup"]["enabled"] = config.backup.enabled;
    doc["backup"]["retention_hours"] = config.backup.retention_hours;
    
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
    if (firebaseConfig["timeouts"]["config_ap_min"]) config.timeouts.config_ap_min = firebaseConfig["timeouts"]["config_ap_min"];
    
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
    
    // Buffer config
    if (firebaseConfig["buffer"]["max_size"]) config.buffer.max_size = firebaseConfig["buffer"]["max_size"];
    if (firebaseConfig["buffer"]["sync_threshold_percent"]) config.buffer.sync_threshold_percent = firebaseConfig["buffer"]["sync_threshold_percent"];
    if (firebaseConfig["buffer"]["auto_save_interval"]) config.buffer.auto_save_interval = firebaseConfig["buffer"]["auto_save_interval"];
    if (firebaseConfig["buffer"]["max_item_size"]) config.buffer.max_item_size = firebaseConfig["buffer"]["max_item_size"];
    
    // Compression config
    if (firebaseConfig["compression"]["enabled"]) config.compression.enabled = firebaseConfig["compression"]["enabled"];
    if (firebaseConfig["compression"]["min_size_bytes"]) config.compression.min_size_bytes = firebaseConfig["compression"]["min_size_bytes"];
    
    // Storage config
    if (firebaseConfig["storage"]["min_free_kb"]) config.storage.min_free_kb = firebaseConfig["storage"]["min_free_kb"];
    if (firebaseConfig["storage"]["warning_threshold_kb"]) config.storage.warning_threshold_kb = firebaseConfig["storage"]["warning_threshold_kb"];
    if (firebaseConfig["storage"]["aggressive_cleanup_multiplier"]) config.storage.aggressive_cleanup_multiplier = firebaseConfig["storage"]["aggressive_cleanup_multiplier"];
    
    // Backup config
    if (firebaseConfig["backup"]["enabled"]) config.backup.enabled = firebaseConfig["backup"]["enabled"];
    if (firebaseConfig["backup"]["retention_hours"]) config.backup.retention_hours = firebaseConfig["backup"]["retention_hours"];
    
    saveConfig();
    Serial.println("🔄 Config atualizada e salva localmente");
    Serial.printf("   Sync interval: %d segundos\n", config.intervals.sync_sec);
    Serial.printf("   WiFi timeout: %d segundos\n", config.timeouts.wifi_sec);
    Serial.printf("   LED count interval: %d segundos\n", config.intervals.led_count_sec);
    Serial.printf("   Fallback: %d falhas ou %d min\n", config.fallback.max_failures, config.fallback.timeout_min);
}

String ConfigManager::getCentralConfigUrl() const {
    return String(config.firebase.database_url) + 
           "/bases/" + config.base_id + "/configs.json?auth=" + 
           config.firebase.api_key;
}

String ConfigManager::getBikeRegistryUrl() const {
    return String(config.firebase.database_url) + 
           "/bases/" + config.base_id + "/bikes.json?auth=" + 
           config.firebase.api_key;
}

String ConfigManager::getWiFiConfigUrl() const {
    return String(config.firebase.database_url) + 
           "/bases/" + config.base_id + "/configs/wifi.json?auth=" + 
           config.firebase.api_key;
}

String ConfigManager::getHeartbeatUrl() const {
    return String(config.firebase.database_url) + 
           "/bases/" + config.base_id + "/last_heartbeat.json?auth=" + 
           config.firebase.api_key;
}

String ConfigManager::getBufferDataUrl() const {
    return String(config.firebase.database_url) + 
           "/bases/" + config.base_id + "/data.json?auth=" + 
           config.firebase.api_key;
}

bool ConfigManager::updateFromJson(const String& json) {
    // Early return se JSON vazio
    if (json.length() < 100) {
        Serial.println("🚨 JSON too small - invalid config!");
        return false;
    }
    
    DynamicJsonDocument doc(2048);
    
    // Early return se parse falhar
    if (deserializeJson(doc, json) != DeserializationError::Ok) {
        Serial.println("🚨 JSON parse failed!");
        return false;
    }
    
    // Early return se validação falhar
    if (!isValidFirebaseConfig(doc)) {
        Serial.println("🚨 Config validation failed!");
        return false;
    }
    
    // Sucesso - atualizar config
    updateFromFirebase(doc);
    Serial.println("✅ Config updated successfully from JSON");
    return true;
}

bool ConfigManager::isValidFirebaseConfig(const DynamicJsonDocument& doc) const {
    Serial.println("🔍 Validating Firebase config fields...");
    
    // Lista de campos obrigatórios
    struct RequiredField {
        const char* path;
        const char* description;
    };
    
    RequiredField required[] = {
        {"intervals.sync_sec", "Sync interval"},
        {"timeouts.wifi_sec", "WiFi timeout"},
        {"led.ble_ready_ms", "LED BLE pattern"},
        {"limits.max_bikes", "Max bikes limit"},
        {"fallback.max_failures", "Max failures"}
    };
    
    // Early return se algum campo obrigatório faltar
    for (auto& field : required) {
        bool exists = false;
        
        if (strcmp(field.path, "intervals.sync_sec") == 0) exists = doc["intervals"]["sync_sec"];
        else if (strcmp(field.path, "timeouts.wifi_sec") == 0) exists = doc["timeouts"]["wifi_sec"];
        else if (strcmp(field.path, "led.ble_ready_ms") == 0) exists = doc["led"]["ble_ready_ms"];
        else if (strcmp(field.path, "limits.max_bikes") == 0) exists = doc["limits"]["max_bikes"];
        else if (strcmp(field.path, "fallback.max_failures") == 0) exists = doc["fallback"]["max_failures"];
        
        if (!exists) {
            Serial.printf("❌ Missing required field: %s (%s)\n", field.description, field.path);
            return false;
        }
        
        Serial.printf("✅ %s: OK\n", field.description);
    }
    
    Serial.println("✅ All required fields present - config valid!");
    return true;
}