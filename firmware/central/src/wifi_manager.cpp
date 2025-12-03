#include "wifi_manager.h"
#include <WiFi.h>
#include <time.h>
#include "config.h"
#include "structs.h"
#include "event_handler.h"
#include "config_loader.h"

extern QueueHandle_t eventQueue;

static bool wifiConnected = false;
static unsigned long lastNTPSync = 0;

void wifiManagerTask(void *parameter) {
    WiFi.mode(WIFI_STA);
    
    while (true) {
        if (!wifiConnected) {
            connectToWiFi();
        } else {
            // Check connection health
            if (WiFi.status() != WL_CONNECTED) {
                wifiConnected = false;
                SystemEvent event = {EVENT_WIFI_DISCONNECTED, "", 0, 0, 0};
                xQueueSend(eventQueue, &event, 0);
                Serial.println("WiFi disconnected");
            } else {
                // Sync NTP periodically
                if (millis() - lastNTPSync > NTP_SYNC_INTERVAL_MS) {
                    syncNTP();
                    lastNTPSync = millis();
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void connectToWiFi() {
    AppConfig config = getAppConfig();
    Serial.printf("📶 Connecting to WiFi: %s\n", config.wifi_ssid);
    
    WiFi.begin(config.wifi_ssid, config.wifi_password);
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < WIFI_TIMEOUT_MS) {
        delay(500);
        Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.println();
        Serial.printf("✅ WiFi connected! IP: %s | RSSI: %d dBm\n", 
                     WiFi.localIP().toString().c_str(), WiFi.RSSI());
        
        // Sync NTP immediately
        syncNTP();
        
        SystemEvent event = {EVENT_WIFI_CONNECTED, "", 0, 0, 0};
        xQueueSend(eventQueue, &event, 0);
    } else {
        Serial.println();
        Serial.println("❌ WiFi connection failed, retrying...");
        delay(WIFI_RETRY_DELAY_MS);
    }
}

void syncNTP() {
    Serial.println("🕰️ Syncing NTP...");
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        Serial.printf("✅ NTP synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                     timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                     timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        Serial.println("❌ NTP sync failed");
    }
}

bool isWiFiConnected() {
    return wifiConnected && WiFi.status() == WL_CONNECTED;
}

uint32_t getCurrentTimestamp() {
    time_t now;
    time(&now);
    return (uint32_t)now;
}