#include "firebase_sync.h"
#include <Firebase_ESP_Client.h>
#include <ArduinoJson.h>
#include "config.h"
#include "structs.h"
#include "wifi_manager.h"
#include "event_handler.h"
#include "config_loader.h"

extern QueueHandle_t eventQueue;

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

static GlobalConfig globalConfig;
static BaseConfig baseConfig;
static unsigned long lastSync = 0;
static unsigned long lastHeartbeat = 0;

void firebaseSyncTask(void *parameter) {
    // Initialize Firebase
    AppConfig appConfig = getAppConfig();
    config.host = appConfig.firebase_host;
    config.signer.tokens.legacy_token = appConfig.firebase_auth;
    
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
    
    while (true) {
        if (isWiFiConnected()) {
            // Sync configurations
            if (millis() - lastSync > FIREBASE_SYNC_INTERVAL_MS) {
                syncConfigurations();
                lastSync = millis();
            }
            
            // Send heartbeat
            if (millis() - lastHeartbeat > HEARTBEAT_INTERVAL_MS) {
                sendHeartbeat();
                lastHeartbeat = millis();
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void syncConfigurations() {
    // Read global config
    if (Firebase.RTDB.getJSON(&fbdo, "/config")) {
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, fbdo.stringData());
        
        globalConfig.version = doc["version"] | 3;
        globalConfig.wifi_scan_interval_sec = doc["wifi_scan_interval_sec"] | 25;
        globalConfig.wifi_scan_interval_low_batt_sec = doc["wifi_scan_interval_low_batt_sec"] | 60;
        globalConfig.deep_sleep_after_sec = doc["deep_sleep_after_sec"] | 300;
        globalConfig.ble_ping_interval_sec = doc["ble_ping_interval_sec"] | 5;
        globalConfig.min_battery_voltage = doc["min_battery_voltage"] | 3.45;
        globalConfig.update_timestamp = doc["update_timestamp"] | getCurrentTimestamp();
        
        Serial.printf("✅ Global config synced - v%d | WiFi scan: %ds | Sleep: %ds\n",
                     globalConfig.version, globalConfig.wifi_scan_interval_sec, globalConfig.deep_sleep_after_sec);
    }
    
    // Read base config
    AppConfig appConfig = getAppConfig();
    String basePath = "/bases/" + String(appConfig.base_id);
    if (Firebase.RTDB.getJSON(&fbdo, basePath)) {
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, fbdo.stringData());
        
        strncpy(baseConfig.name, doc["name"] | "Base Centro", sizeof(baseConfig.name));
        baseConfig.max_bikes = doc["max_bikes"] | 10;
        strncpy(baseConfig.wifi_ssid, doc["wifi_ssid"] | appConfig.wifi_ssid, sizeof(baseConfig.wifi_ssid));
        strncpy(baseConfig.wifi_password, doc["wifi_password"] | appConfig.wifi_password, sizeof(baseConfig.wifi_password));
        baseConfig.lat = doc["location"]["lat"] | -8.062;
        baseConfig.lng = doc["location"]["lng"] | -34.881;
        
        Serial.printf("✅ Base config synced - %s | Max bikes: %d\n",
                     baseConfig.name, baseConfig.max_bikes);
    }
}

void sendHeartbeat() {
    AppConfig appConfig = getAppConfig();
    String path = "/bases/" + String(appConfig.base_id) + "/last_sync";
    uint32_t timestamp = getCurrentTimestamp();
    
    if (Firebase.RTDB.setInt(&fbdo, path, timestamp)) {
        Serial.printf("💓 Heartbeat sent [%u]\n", timestamp);
    } else {
        Serial.printf("❌ Heartbeat failed: %s\n", fbdo.errorReason().c_str());
    }
}

void updateBikeStatus(const char* bikeId, float batteryVoltage, uint32_t lastBleContact) {
    String basePath = "/bikes/" + String(bikeId);
    
    DynamicJsonDocument doc(512);
    AppConfig appConfig = getAppConfig();
    doc["base_id"] = appConfig.base_id;
    doc["uid"] = bikeId;
    doc["battery_voltage"] = batteryVoltage;
    doc["status"] = "active";
    doc["last_ble_contact"] = lastBleContact;
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    if (Firebase.RTDB.setString(&fbdo, basePath, jsonString)) {
        Serial.printf("🚲 Bike %s status updated - Battery: %.2fV\n", bikeId, batteryVoltage);
    } else {
        Serial.printf("❌ Failed to update bike %s: %s\n", bikeId, fbdo.errorReason().c_str());
    }
}

void createAlert(const char* alertType, const char* bikeId) {
    String path = "/alerts/" + String(alertType) + "/" + String(bikeId);
    uint32_t timestamp = getCurrentTimestamp();
    
    if (Firebase.RTDB.setInt(&fbdo, path, timestamp)) {
        Serial.printf("Alert %s created for bike %s\n", alertType, bikeId);
    } else {
        Serial.printf("Failed to create alert %s for bike %s\n", alertType, bikeId);
    }
}

GlobalConfig getGlobalConfig() {
    return globalConfig;
}

BaseConfig getBaseConfig() {
    return baseConfig;
}