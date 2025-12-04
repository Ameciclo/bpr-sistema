#include "config_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

static ConfigCache configCache = {0};

bool downloadFromFirebase(String path, String& result) {
    File config = LittleFS.open("/config.json", "r");
    if (!config) return false;
    
    DynamicJsonDocument doc(512);
    deserializeJson(doc, config);
    config.close();
    
    String firebaseUrl = doc["firebase"]["database_url"];
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(10000);
    
    String url = firebaseUrl;
    url.replace("https://", "");
    int slashIndex = url.indexOf('/');
    String host = url.substring(0, slashIndex);
    
    String fullPath = path + ".json";
    
    if (client.connect(host.c_str(), 443)) {
        String request = "GET " + fullPath + " HTTP/1.1\r\n";
        request += "Host: " + host + "\r\n";
        request += "Connection: close\r\n\r\n";
        
        client.print(request);
        
        String response = "";
        unsigned long start = millis();
        bool headersPassed = false;
        
        while (client.connected() && millis() - start < 10000) {
            if (client.available()) {
                String line = client.readStringUntil('\n');
                
                if (!headersPassed) {
                    if (line == "\r") {
                        headersPassed = true;
                    }
                } else {
                    result += line;
                }
            }
            delay(1);
        }
        client.stop();
        
        return result.length() > 0 && result != "null";
    }
    return false;
}

bool downloadConfigs() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ WiFi não conectado para download");
        return false;
    }
    
    Serial.println("📥 Baixando configurações...");
    
    // Download config global
    String globalJson = "";
    if (downloadFromFirebase("/config", globalJson)) {
        Serial.println("✅ Config global baixada");
        
        DynamicJsonDocument doc(1024);
        if (deserializeJson(doc, globalJson) == DeserializationError::Ok) {
            configCache.global.version = doc["version"];
            configCache.global.wifi_scan_interval_sec = doc["wifi_scan_interval_sec"];
            configCache.global.wifi_scan_interval_low_batt_sec = doc["wifi_scan_interval_low_batt_sec"];
            configCache.global.deep_sleep_after_sec = doc["deep_sleep_after_sec"];
            configCache.global.ble_ping_interval_sec = doc["ble_ping_interval_sec"];
            configCache.global.min_battery_voltage = doc["min_battery_voltage"];
            configCache.global.update_timestamp = doc["update_timestamp"];
        }
    } else {
        Serial.println("⚠️ Config global não encontrada - usando padrões");
        // Valores padrão
        configCache.global.version = 1;
        configCache.global.wifi_scan_interval_sec = 25;
        configCache.global.wifi_scan_interval_low_batt_sec = 60;
        configCache.global.deep_sleep_after_sec = 300;
        configCache.global.ble_ping_interval_sec = 5;
        configCache.global.min_battery_voltage = 3.45;
        configCache.global.update_timestamp = millis() / 1000;
    }
    
    // Download config da base
    String baseJson = "";
    if (downloadFromFirebase("/bases/ameciclo", baseJson)) {
        Serial.println("✅ Config base baixada");
        
        DynamicJsonDocument doc(1024);
        if (deserializeJson(doc, baseJson) == DeserializationError::Ok) {
            strncpy(configCache.base.name, doc["name"], 31);
            configCache.base.max_bikes = doc["max_bikes"];
            strncpy(configCache.base.wifi_ssid, doc["wifi_ssid"], 31);
            strncpy(configCache.base.wifi_password, doc["wifi_password"], 63);
            configCache.base.lat = doc["location"]["lat"];
            configCache.base.lng = doc["location"]["lng"];
            configCache.base.last_sync = doc["last_sync"];
        }
    } else {
        Serial.println("⚠️ Config base não encontrada - usando padrões");
        strncpy(configCache.base.name, "Ameciclo", 31);
        configCache.base.max_bikes = 10;
        strncpy(configCache.base.wifi_ssid, "BPR_Base", 31);
        strncpy(configCache.base.wifi_password, "botaprarodar6", 63);
        configCache.base.lat = -8.062;
        configCache.base.lng = -34.881;
        configCache.base.last_sync = millis() / 1000;
    }
    
    configCache.lastUpdate = millis() / 1000;
    configCache.valid = true;
    
    // Salvar cache local
    saveConfigCache();
    
    Serial.printf("✅ Configs atualizadas - Global v%d, Base: %s\n", 
                  configCache.global.version, configCache.base.name);
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
    
    // Carregar config global
    JsonObject global = doc["global"];
    configCache.global.version = global["version"];
    configCache.global.wifi_scan_interval_sec = global["wifi_scan_interval_sec"];
    configCache.global.wifi_scan_interval_low_batt_sec = global["wifi_scan_interval_low_batt_sec"];
    configCache.global.deep_sleep_after_sec = global["deep_sleep_after_sec"];
    configCache.global.ble_ping_interval_sec = global["ble_ping_interval_sec"];
    configCache.global.min_battery_voltage = global["min_battery_voltage"];
    configCache.global.update_timestamp = global["update_timestamp"];
    
    // Carregar config base
    JsonObject base = doc["base"];
    strncpy(configCache.base.name, base["name"], 31);
    configCache.base.max_bikes = base["max_bikes"];
    strncpy(configCache.base.wifi_ssid, base["wifi_ssid"], 31);
    strncpy(configCache.base.wifi_password, base["wifi_password"], 63);
    configCache.base.lat = base["location"]["lat"];
    configCache.base.lng = base["location"]["lng"];
    configCache.base.last_sync = base["last_sync"];
    
    configCache.lastUpdate = doc["lastUpdate"];
    configCache.valid = true;
    
    Serial.println("✅ Cache de config carregado");
    return true;
}

bool saveConfigCache() {
    File file = LittleFS.open("/config_cache.json", "w");
    if (!file) return false;
    
    DynamicJsonDocument doc(2048);
    
    // Salvar config global
    JsonObject global = doc.createNestedObject("global");
    global["version"] = configCache.global.version;
    global["wifi_scan_interval_sec"] = configCache.global.wifi_scan_interval_sec;
    global["wifi_scan_interval_low_batt_sec"] = configCache.global.wifi_scan_interval_low_batt_sec;
    global["deep_sleep_after_sec"] = configCache.global.deep_sleep_after_sec;
    global["ble_ping_interval_sec"] = configCache.global.ble_ping_interval_sec;
    global["min_battery_voltage"] = configCache.global.min_battery_voltage;
    global["update_timestamp"] = configCache.global.update_timestamp;
    
    // Salvar config base
    JsonObject base = doc.createNestedObject("base");
    base["name"] = configCache.base.name;
    base["max_bikes"] = configCache.base.max_bikes;
    base["wifi_ssid"] = configCache.base.wifi_ssid;
    base["wifi_password"] = configCache.base.wifi_password;
    JsonObject location = base.createNestedObject("location");
    location["lat"] = configCache.base.lat;
    location["lng"] = configCache.base.lng;
    base["last_sync"] = configCache.base.last_sync;
    
    doc["lastUpdate"] = configCache.lastUpdate;
    
    serializeJson(doc, file);
    file.close();
    
    return true;
}

GlobalConfig getGlobalConfig() {
    return configCache.global;
}

BaseConfig getBaseConfig() {
    return configCache.base;
}

bool isConfigValid() {
    // Config válida por 1 hora
    return configCache.valid && (millis()/1000 - configCache.lastUpdate < 3600);
}

void invalidateConfig() {
    configCache.valid = false;
}