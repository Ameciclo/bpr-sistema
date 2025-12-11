#include "wifi_manager.h"
#include <WiFi.h>
#include <time.h>
#include "config.h"
#include "structs.h"
#include "event_handler.h"
#include "config_manager.h"
#include "ntp_manager.h"

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
    Serial.printf("📶 Connecting to WiFi: %s\n", getWifiSSID());
    
    WiFi.begin(getWifiSSID(), getWifiPassword());
    
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



bool isWiFiConnected() {
    return wifiConnected && WiFi.status() == WL_CONNECTED;
}

