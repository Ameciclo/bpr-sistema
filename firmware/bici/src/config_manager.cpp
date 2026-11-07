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

    if (config.bike_id[sizeof(config.bike_id) - 1] != '\0' ||
        config.wifi.scan_interval_sec == 0 || config.wifi.scan_interval_sec > 86400 ||
        config.wifi.max_networks == 0 || config.wifi.max_networks > MAX_NETWORKS_PER_SCAN) {
        Serial.println("❌ Config content invalid - using defaults");
        ConfigManager fresh;
        config = fresh.config;
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
    // F07: só gera quando chamado (bike_id vazio após load); salva identidade
    String bikeId = generateBikeId();
    strncpy(config.bike_id, bikeId.c_str(), sizeof(config.bike_id) - 1);
    config.bike_id[sizeof(config.bike_id) - 1] = '\0';
    Serial.printf("🆔 Generated bike ID: %s\n", config.bike_id);
    save();
}

bool ConfigManager::processUpdate(const String& configJson) {
    Serial.println("⚙️ Processing config update");

    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, configJson) != DeserializationError::Ok) {
        Serial.println("❌ Invalid config JSON");
        return false;
    }

    // Aceita envelope {type, config:{...}} ou objeto direto
    JsonObject cfg = doc.as<JsonObject>();
    if (doc["config"].is<JsonObject>()) {
        cfg = doc["config"];
    }

    bool configChanged = false;

    // F06: containsKey/is<> em vez de truthiness — false e 0 explícitos são aplicados
    if (cfg["dev_mode"].is<bool>() && cfg["dev_mode"].as<bool>() != config.dev_mode) {
        config.dev_mode = cfg["dev_mode"].as<bool>();
        configChanged = true;
    }
    if (cfg["bike_name"].is<const char*>()) {
        strncpy(config.bike_name, cfg["bike_name"], sizeof(config.bike_name) - 1);
        config.bike_name[sizeof(config.bike_name) - 1] = '\0';
        configChanged = true;
    }

    if (cfg["wifi"].is<JsonObject>()) {
        JsonObject wifi = cfg["wifi"];
        if (wifi["scan_interval_sec"].is<uint16_t>() && wifi["scan_interval_sec"].as<uint16_t>() >= 5) {
            config.wifi.scan_interval_sec = wifi["scan_interval_sec"];
            configChanged = true;
        }
        if (wifi["scan_timeout_ms"].is<uint16_t>() && wifi["scan_timeout_ms"].as<uint16_t>() >= 100) {
            config.wifi.scan_timeout_ms = wifi["scan_timeout_ms"];
            configChanged = true;
        }
        if (wifi["max_networks"].is<uint8_t>() && wifi["max_networks"].as<uint8_t>() >= 1) {
            config.wifi.max_networks = wifi["max_networks"];
            configChanged = true;
        }
        if (wifi["rssi_threshold"].is<int8_t>()) {
            config.wifi.rssi_threshold = wifi["rssi_threshold"];
            configChanged = true;
        }
    }

    if (cfg["ble"].is<JsonObject>()) {
        JsonObject ble = cfg["ble"];
        if (ble["base_name"].is<const char*>()) {
            strncpy(config.ble.base_name, ble["base_name"], sizeof(config.ble.base_name) - 1);
            config.ble.base_name[sizeof(config.ble.base_name) - 1] = '\0';
            configChanged = true;
        }
        if (ble["scan_time_sec"].is<uint16_t>() && ble["scan_time_sec"].as<uint16_t>() >= 1) {
            config.ble.scan_time_sec = ble["scan_time_sec"];
            configChanged = true;
        }
        if (ble["connection_timeout_ms"].is<uint16_t>() && ble["connection_timeout_ms"].as<uint16_t>() >= 1000) {
            config.ble.connection_timeout_ms = ble["connection_timeout_ms"];
            configChanged = true;
        }
    }

    if (cfg["power"].is<JsonObject>()) {
        JsonObject power = cfg["power"];
        if (power["deep_sleep_duration_sec"].is<uint32_t>() && power["deep_sleep_duration_sec"].as<uint32_t>() >= 60) {
            config.power.deep_sleep_duration_sec = power["deep_sleep_duration_sec"];
            configChanged = true;
        }
    }

    if (cfg["battery"].is<JsonObject>()) {
        JsonObject battery = cfg["battery"];
        if (battery["critical_voltage"].is<float>() && battery["critical_voltage"].as<float>() > 3.0 &&
            battery["critical_voltage"].as<float>() < 4.5) {
            config.battery.critical_voltage = battery["critical_voltage"];
            configChanged = true;
        }
        if (battery["low_voltage"].is<float>() && battery["low_voltage"].as<float>() > 3.0 &&
            battery["low_voltage"].as<float>() < 4.5) {
            config.battery.low_voltage = battery["low_voltage"];
            configChanged = true;
        }
    }

    if (!configChanged) {
        Serial.println("ℹ️ Config update with no applicable changes");
        return false;
    }

    if (!isValid()) {
        Serial.println("❌ Config update rejected: invalid resulting config");
        return false;
    }

    // Versão remota, se fornecida no envelope, prevalece sobre incremento local
    if (doc["config_version"].is<uint32_t>()) {
        uint32_t remoteVersion = doc["config_version"];
        if (remoteVersion > config.version) {
            config.version = remoteVersion;
        } else {
            config.version++;
        }
    } else {
        config.version++;
    }

    Serial.printf("✅ Config updated: %s v%d (dev_mode=%s)\n",
                  config.bike_name, config.version, config.dev_mode ? "ON" : "OFF");
    save();
    return true;
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
