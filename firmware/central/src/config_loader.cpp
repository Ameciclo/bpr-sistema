#include "config_loader.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "config.h"

static AppConfig appConfig;
static bool configLoaded = false;

bool loadConfiguration() {
    if (!SPIFFS.begin()) {
        Serial.println("Failed to mount SPIFFS");
        return false;
    }
    
    File file = SPIFFS.open(CONFIG_FILE_PATH, "r");
    if (!file) {
        Serial.println("Failed to open config file");
        return false;
    }
    
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        Serial.println("Failed to parse config file");
        return false;
    }
    
    // Load configuration
    strncpy(appConfig.firebase_host, doc["firebase_host"] | "", sizeof(appConfig.firebase_host) - 1);
    strncpy(appConfig.firebase_auth, doc["firebase_auth"] | "", sizeof(appConfig.firebase_auth) - 1);
    strncpy(appConfig.base_id, doc["base_id"] | "base01", sizeof(appConfig.base_id) - 1);
    strncpy(appConfig.base_name, doc["base_name"] | "Base BPR", sizeof(appConfig.base_name) - 1);
    strncpy(appConfig.wifi_ssid, doc["wifi_ssid"] | "", sizeof(appConfig.wifi_ssid) - 1);
    strncpy(appConfig.wifi_password, doc["wifi_password"] | "", sizeof(appConfig.wifi_password) - 1);
    
    configLoaded = true;
    Serial.println("Configuration loaded successfully");
    return true;
}

AppConfig getAppConfig() {
    if (!configLoaded) {
        loadConfiguration();
    }
    return appConfig;
}