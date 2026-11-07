#include "config_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

ConfigManager::ConfigManager() {
    resetToDefaults();
}

void ConfigManager::resetToDefaults() {
    // Initialize with FSD defaults
    config.version = 2;
    strcpy(config.bike_id, "");
    strcpy(config.bike_name, "");
    // O05: produção como padrão — proteção de bateria ativa salvo override
    config.dev_mode = false;

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
    if (!LittleFS.exists("/config.bin")) {
        Serial.println("❌ Config file not found");
        resetToDefaults();
        return false;
    }

    File file = LittleFS.open("/config.bin", "r");
    if (!file) {
        Serial.println("❌ Failed to open config file");
        resetToDefaults();
        return false;
    }

    // R20: lê em candidata; objeto ativo só é tocado após validação completa
    BikeConfig candidate;
    size_t bytesRead = file.readBytes((char*)&candidate, sizeof(BikeConfig));
    file.close();

    if (bytesRead != sizeof(BikeConfig)) {
        Serial.printf("❌ Config size mismatch: %d != %d\n", bytesRead, sizeof(BikeConfig));
        resetToDefaults();
        return false;
    }

    if (candidate.bike_id[sizeof(candidate.bike_id) - 1] != '\0' ||
        candidate.wifi.scan_interval_sec == 0 || candidate.wifi.scan_interval_sec > 86400 ||
        candidate.wifi.max_networks == 0 || candidate.wifi.max_networks > MAX_NETWORKS_PER_SCAN) {
        Serial.println("❌ Config content invalid - using defaults");
        resetToDefaults();
        return false;
    }

    config = candidate;
    Serial.printf("✅ Binary config loaded: %s v%d\n", config.bike_id, config.version);
    return true;
}

bool ConfigManager::save() {
    Serial.println("💾 Saving binary config...");
    config.timestamp = millis() / 1000;

    // R04/R09: temporário + rename; falha preserva a última config confirmada
    File file = LittleFS.open("/config.tmp", "w");
    if (!file) {
        Serial.println("❌ Failed to open config file");
        return false;
    }

    size_t bytesWritten = file.write((uint8_t*)&config, sizeof(BikeConfig));
    file.close();

    if (bytesWritten != sizeof(BikeConfig)) {
        Serial.printf("❌ Config write failed: %d/%d bytes\n", bytesWritten, sizeof(BikeConfig));
        LittleFS.remove("/config.tmp");
        return false;
    }

    LittleFS.remove("/config.bin");
    if (!LittleFS.rename("/config.tmp", "/config.bin")) {
        Serial.println("❌ Config rename failed");
        LittleFS.remove("/config.tmp");
        return false;
    }

    Serial.printf("✅ Binary config saved (%d bytes)\n", bytesWritten);
    return true;
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

    // R07: política de versão explícita ANTES de aplicar nada
    // - versão remota > local: aplica e assume a versão remota
    // - versão remota == local: reenvio idempotente (ACK ok, sem reincrementar)
    // - versão remota < local: configuração antiga, rejeitada
    bool hasRemoteVersion = doc["config_version"].is<uint32_t>();
    uint32_t remoteVersion = hasRemoteVersion ? doc["config_version"].as<uint32_t>() : 0;
    if (hasRemoteVersion && remoteVersion < config.version) {
        Serial.printf("❌ Stale config rejected: remote v%lu <= local v%lu\n",
                      (unsigned long)remoteVersion, (unsigned long)config.version);
        return false;
    }

    // R07: aplica em CANDIDATA; config ativa só troca após persistir
    BikeConfig candidate = config;
    bool configChanged = applyUpdate(cfg, candidate);

    if (hasRemoteVersion && remoteVersion == config.version) {
        if (!configChanged) {
            // R07: mesma versão, mesmo conteúdo — reenvio idempotente
            Serial.println("ℹ️ Duplicate config update (same version) - idempotent ACK");
            return true;
        }
        // R07: mesma versão com conteúdo DIVERGENTE é conflito explícito,
        // não uma atualização silenciosa
        Serial.println("❌ Config conflict: same version with different content - rejected");
        return false;
    }

    if (!configChanged) {
        Serial.println("ℹ️ Config update with no applicable changes");
        return false;
    }

    if (!isValidConfig(candidate)) {
        Serial.println("❌ Config update rejected: invalid resulting config");
        return false;
    }

    // Só chega aqui com versão remota maior que a local (ou sem versão)
    if (hasRemoteVersion) {
        // R07: a revisão remota atravessa sem mutação local
        candidate.version = remoteVersion;
    } else {
        candidate.version++;
    }

    BikeConfig previous = config;
    config = candidate;
    if (!save()) {
        // R09: falha de persistência restaura o estado anterior e nega o ACK
        config = previous;
        Serial.println("❌ Config not persisted - keeping previous config");
        return false;
    }

    Serial.printf("✅ Config updated: %s v%d (dev_mode=%s)\n",
                  config.bike_name, config.version, config.dev_mode ? "ON" : "OFF");
    return true;
}

bool ConfigManager::applyUpdate(JsonObject cfg, BikeConfig& candidate) {
    bool configChanged = false;

    // R07: "changed" significa VALOR DIVERGENTE, não campo presente —
    // reenvio idêntico não conta como mudança (idempotência)

    // F06: containsKey/is<> em vez de truthiness — false e 0 explícitos são aplicados
    if (cfg["dev_mode"].is<bool>() && cfg["dev_mode"].as<bool>() != candidate.dev_mode) {
        candidate.dev_mode = cfg["dev_mode"].as<bool>();
        configChanged = true;
    }
    if (cfg["bike_name"].is<const char*>()) {
        const char* name = cfg["bike_name"];
        if (strncmp(candidate.bike_name, name, sizeof(candidate.bike_name)) != 0) {
            strncpy(candidate.bike_name, name, sizeof(candidate.bike_name) - 1);
            candidate.bike_name[sizeof(candidate.bike_name) - 1] = '\0';
            configChanged = true;
        }
    }

    if (cfg["wifi"].is<JsonObject>()) {
        JsonObject wifi = cfg["wifi"];
        if (wifi["scan_interval_sec"].is<uint16_t>()) {
            uint16_t v = wifi["scan_interval_sec"];
            if (v >= 5 && v != candidate.wifi.scan_interval_sec) {
                candidate.wifi.scan_interval_sec = v;
                configChanged = true;
            }
        }
        if (wifi["scan_timeout_ms"].is<uint16_t>()) {
            uint16_t v = wifi["scan_timeout_ms"];
            if (v >= 100 && v != candidate.wifi.scan_timeout_ms) {
                candidate.wifi.scan_timeout_ms = v;
                configChanged = true;
            }
        }
        if (wifi["max_networks"].is<uint8_t>()) {
            uint8_t v = wifi["max_networks"];
            if (v >= 1 && v != candidate.wifi.max_networks) {
                candidate.wifi.max_networks = v;
                configChanged = true;
            }
        }
        if (wifi["rssi_threshold"].is<int8_t>()) {
            int8_t v = wifi["rssi_threshold"];
            if (v != candidate.wifi.rssi_threshold) {
                candidate.wifi.rssi_threshold = v;
                configChanged = true;
            }
        }
    }

    if (cfg["ble"].is<JsonObject>()) {
        JsonObject ble = cfg["ble"];
        if (ble["base_name"].is<const char*>()) {
            const char* base = ble["base_name"];
            if (strncmp(candidate.ble.base_name, base, sizeof(candidate.ble.base_name)) != 0) {
                strncpy(candidate.ble.base_name, base, sizeof(candidate.ble.base_name) - 1);
                candidate.ble.base_name[sizeof(candidate.ble.base_name) - 1] = '\0';
                configChanged = true;
            }
        }
        if (ble["scan_time_sec"].is<uint16_t>()) {
            uint16_t v = ble["scan_time_sec"];
            if (v >= 1 && v != candidate.ble.scan_time_sec) {
                candidate.ble.scan_time_sec = v;
                configChanged = true;
            }
        }
        if (ble["connection_timeout_ms"].is<uint16_t>()) {
            uint16_t v = ble["connection_timeout_ms"];
            if (v >= 1000 && v != candidate.ble.connection_timeout_ms) {
                candidate.ble.connection_timeout_ms = v;
                configChanged = true;
            }
        }
    }

    if (cfg["power"].is<JsonObject>()) {
        JsonObject power = cfg["power"];
        if (power["deep_sleep_duration_sec"].is<uint32_t>()) {
            uint32_t v = power["deep_sleep_duration_sec"];
            if (v >= 60 && v != candidate.power.deep_sleep_duration_sec) {
                candidate.power.deep_sleep_duration_sec = v;
                configChanged = true;
            }
        }
    }

    if (cfg["battery"].is<JsonObject>()) {
        JsonObject battery = cfg["battery"];
        if (battery["critical_voltage"].is<float>()) {
            float v = battery["critical_voltage"];
            if (v > 3.0 && v < 4.5 && v != candidate.battery.critical_voltage) {
                candidate.battery.critical_voltage = v;
                configChanged = true;
            }
        }
        if (battery["low_voltage"].is<float>()) {
            float v = battery["low_voltage"];
            if (v > 3.0 && v < 4.5 && v != candidate.battery.low_voltage) {
                candidate.battery.low_voltage = v;
                configChanged = true;
            }
        }
    }

    return configChanged;
}

bool ConfigManager::isValidConfig(const BikeConfig& cfg) {
    return (strlen(cfg.bike_id) > 0 &&
            cfg.version > 0 &&
            cfg.wifi.scan_interval_sec > 0 &&
            cfg.wifi.scan_timeout_ms > 0 &&
            cfg.battery.critical_voltage < cfg.battery.low_voltage);
}

bool ConfigManager::isValid() {
    return isValidConfig(config);
}

BikeConfig& ConfigManager::getConfig() {
    return config;
}
