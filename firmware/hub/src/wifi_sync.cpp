#include "wifi_sync.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "constants.h"
#include "config_manager.h"
#include "buffer_manager.h"
#include "led_controller.h"
#include "state_machine.h"
#include "state_machine.h"

extern ConfigManager configManager;
extern BufferManager bufferManager;
extern LEDController ledController;
extern StateMachine stateMachine;

static uint32_t syncStartTime = 0;

void WiFiSync::enter() {
    Serial.println("📡 Entering WIFI_SYNC mode");
    syncStartTime = millis();
    ledController.syncPattern();
    
    if (!connectWiFi()) {
        Serial.println("❌ WiFi connection failed");
        stateMachine.handleEvent(EVENT_SYNC_COMPLETE);
        return;
    }
    
    syncTime();
    downloadConfig();
    uploadData();
    
    WiFi.disconnect(true);
    Serial.println("✅ Sync complete");
    stateMachine.handleEvent(EVENT_SYNC_COMPLETE);
}

void WiFiSync::update() {
    // Timeout check
    if (millis() - syncStartTime > configManager.getConfig().wifi.timeout_ms * 3) {
        Serial.println("⏰ Sync timeout");
        WiFi.disconnect(true);
        stateMachine.handleEvent(EVENT_SYNC_COMPLETE);
    }
}

void WiFiSync::exit() {
    WiFi.disconnect(true);
    Serial.println("🔚 Exiting WIFI_SYNC mode");
}

bool WiFiSync::connectWiFi() {
    const HubConfig& config = configManager.getConfig();
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(config.wifi.ssid, config.wifi.password);
    
    uint32_t startTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startTime > config.wifi.timeout_ms) {
            return false;
        }
        delay(500);
        Serial.print(".");
    }
    
    Serial.printf("\n📶 WiFi connected: %s\n", WiFi.localIP().toString().c_str());
    return true;
}

void WiFiSync::syncTime() {
    configTime(configManager.getConfig().timezone_offset, 0, configManager.getConfig().ntp_server);
    
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        Serial.printf("⏰ Time synced: %02d:%02d:%02d\n", 
                     timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }
}

void WiFiSync::downloadConfig() {
    HTTPClient http;
    const HubConfig& config = configManager.getConfig();
    
    String url = String(config.firebase.database_url) + 
                "/central_configs/" + config.base_id + ".json?auth=" + 
                config.firebase.api_key;
    
    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        DynamicJsonDocument doc(2048);
        
        if (deserializeJson(doc, payload) == DeserializationError::Ok) {
            configManager.updateFromFirebase(doc);
            Serial.println("Config updated from Firebase");
        }
    } else {
        Serial.printf("Config download failed: %d\n", httpCode);
    }
    
    http.end();
}

void WiFiSync::uploadData() {
    HTTPClient http;
    const HubConfig& config = configManager.getConfig();
    
    // Upload buffered data
    DynamicJsonDocument doc(4096);
    if (bufferManager.getDataForUpload(doc)) {
        String url = String(config.firebase.database_url) + 
                    "/hubs/" + config.base_id + "/data.json?auth=" + 
                    config.firebase.api_key;
        
        http.begin(url);
        http.addHeader("Content-Type", "application/json");
        
        String jsonString;
        serializeJson(doc, jsonString);
        
        int httpCode = http.PATCH(jsonString);
        
        if (httpCode == HTTP_CODE_OK) {
            bufferManager.markAsSent();
            Serial.printf("📤 Data uploaded: %d bytes\n", jsonString.length());
        } else {
            Serial.printf("❌ Upload failed: %d\n", httpCode);
        }
        
        http.end();
    }
    
    // Upload heartbeat
    uploadHeartbeat();
}

void WiFiSync::uploadHeartbeat() {
    HTTPClient http;
    const HubConfig& config = configManager.getConfig();
    
    String url = String(config.firebase.database_url) + 
                "/bases/" + config.base_id + "/last_heartbeat.json?auth=" + 
                config.firebase.api_key;
    
    DynamicJsonDocument doc(512);
    doc["timestamp"] = time(nullptr);
    doc["bikes_connected"] = bufferManager.getConnectedBikes();
    doc["heap"] = ESP.getFreeHeap();
    doc["uptime"] = millis() / 1000;
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    int httpCode = http.PUT(jsonString);
    
    if (httpCode == HTTP_CODE_OK) {
        Serial.println("💓 Heartbeat sent");
    } else {
        Serial.printf("❌ Heartbeat failed: %d\n", httpCode);
    }
    
    http.end();
}