#include "config_manager.h"
#include "firebase_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFi.h>

static CentralConfigCache configCache = {0};

bool checkConfigUpdates() {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }
    
    CentralConfigCache config = getCentralConfig();
    String timestampPath = "/central_configs/" + String(config.base_id) + "/last_modified";
    String timestampJson = "";
    
    if (downloadFromFirebase(timestampPath, timestampJson)) {
        unsigned long remoteTimestamp = timestampJson.toInt();
        
        if (remoteTimestamp > configCache.configTimestamp) {
            Serial.printf("🔄 Config mudou: %lu > %lu\n", remoteTimestamp, configCache.configTimestamp);
            return true;
        }
        
        Serial.println("✅ Config atualizada - sem mudanças");
        return false;
    }
    
    return false;
}

bool downloadConfigs() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ WiFi não conectado para download");
        return false;
    }
    
    Serial.println("📥 Baixando configurações do Firebase...");
    
    CentralConfigCache config = getCentralConfig();
    String configPath = "/central_configs/" + String(config.base_id);
    String configJson = "";
    
    DynamicJsonDocument doc(2048);
    if (downloadFromFirebase(configPath, configJson)) {
        Serial.println("✅ Config baixada do Firebase");
        
        if (deserializeJson(doc, configJson) != DeserializationError::Ok) {
            Serial.println("❌ ERRO: JSON inválido");
            return false;
        }
    } else {
        Serial.println("❌ ERRO: Config não encontrada no Firebase");
        return false;
    }
    
    // Mapear TODOS os campos (sem fallbacks)
    strncpy(configCache.base_id, doc["base_id"], 31);
    configCache.sync_interval_sec = doc["sync_interval_sec"];
    configCache.wifi_timeout_sec = doc["wifi_timeout_sec"];
    configCache.led_pin = doc["led_pin"];
    configCache.firebase_batch_size = doc["firebase_batch_size"];
    
    // Central info
    strncpy(configCache.central_name, doc["central"]["name"], 31);
    configCache.central_max_bikes = doc["central"]["max_bikes"];
    configCache.central_lat = doc["central"]["location"]["lat"];
    configCache.central_lng = doc["central"]["location"]["lng"];
    
    // WiFi
    strncpy(configCache.wifi_ssid, doc["wifi"]["ssid"], 31);
    strncpy(configCache.wifi_password, doc["wifi"]["password"], 63);
    
    // LED timings
    configCache.led_boot_ms = doc["led"]["boot_ms"];
    configCache.led_ble_ready_ms = doc["led"]["ble_ready_ms"];
    configCache.led_wifi_sync_ms = doc["led"]["wifi_sync_ms"];
    
    // Timestamp da configuração
    configCache.configTimestamp = doc["last_modified"];
    
    configCache.lastUpdate = millis() / 1000;
    configCache.valid = true;
    
    // Salvar cache local
    saveConfigCache();
    
    Serial.printf("✅ Config atualizada - %s (timestamp: %lu)\n", 
                  configCache.central_name, configCache.configTimestamp);
    return true;
}

bool loadConfigCache() {
    File file = LittleFS.open("/config_cache.json", "r");
    if (!file) {
        Serial.println("⚠️ Cache de config não encontrado");
        return false;
    }
    
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, file) != DeserializationError::Ok) {
        file.close();
        return false;
    }
    file.close();
    
    // Carregar todas as configurações
    strncpy(configCache.base_id, doc["base_id"], 31);
    configCache.sync_interval_sec = doc["sync_interval_sec"];
    configCache.wifi_timeout_sec = doc["wifi_timeout_sec"];
    configCache.led_pin = doc["led_pin"];
    configCache.firebase_batch_size = doc["firebase_batch_size"];
    strncpy(configCache.central_name, doc["central_name"], 31);
    configCache.central_max_bikes = doc["central_max_bikes"];
    strncpy(configCache.wifi_ssid, doc["wifi_ssid"], 31);
    strncpy(configCache.wifi_password, doc["wifi_password"], 63);
    configCache.central_lat = doc["central_lat"];
    configCache.central_lng = doc["central_lng"];
    configCache.led_boot_ms = doc["led_boot_ms"];
    configCache.led_ble_ready_ms = doc["led_ble_ready_ms"];
    configCache.led_wifi_sync_ms = doc["led_wifi_sync_ms"];
    configCache.configTimestamp = doc["configTimestamp"];
    
    configCache.lastUpdate = doc["lastUpdate"];
    configCache.valid = true;
    
    Serial.println("✅ Cache de config carregado");
    return true;
}

bool saveConfigCache() {
    File file = LittleFS.open("/config_cache.json", "w");
    if (!file) return false;
    
    DynamicJsonDocument doc(2048);
    
    // Salvar todas as configurações
    doc["base_id"] = configCache.base_id;
    doc["sync_interval_sec"] = configCache.sync_interval_sec;
    doc["wifi_timeout_sec"] = configCache.wifi_timeout_sec;
    doc["led_pin"] = configCache.led_pin;
    doc["firebase_batch_size"] = configCache.firebase_batch_size;
    doc["central_name"] = configCache.central_name;
    doc["central_max_bikes"] = configCache.central_max_bikes;
    doc["wifi_ssid"] = configCache.wifi_ssid;
    doc["wifi_password"] = configCache.wifi_password;
    doc["central_lat"] = configCache.central_lat;
    doc["central_lng"] = configCache.central_lng;
    doc["led_boot_ms"] = configCache.led_boot_ms;
    doc["led_ble_ready_ms"] = configCache.led_ble_ready_ms;
    doc["led_wifi_sync_ms"] = configCache.led_wifi_sync_ms;
    doc["configTimestamp"] = configCache.configTimestamp;
    doc["lastUpdate"] = configCache.lastUpdate;
    
    serializeJson(doc, file);
    file.close();
    
    return true;
}

CentralConfigCache getCentralConfig() {
    return configCache;
}

int getSyncInterval() {
    return configCache.sync_interval_sec;
}

int getLedPin() {
    return configCache.led_pin;
}

const char* getWifiSSID() {
    return configCache.wifi_ssid;
}

const char* getWifiPassword() {
    return configCache.wifi_password;
}

bool isConfigValid() {
    // Config válida por 1 hora
    return configCache.valid && (millis()/1000 - configCache.lastUpdate < 3600);
}

void invalidateConfig() {
    configCache.valid = false;
}