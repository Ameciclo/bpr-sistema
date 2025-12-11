#include "state_machine.h"
#include "config_manager.h"
#include "led_controller.h"
#include "firebase_manager.h"
#include "ntp_manager.h"
#include "bike_discovery.h"
#include "bike_manager.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

CentralMode currentMode = MODE_BLE_ONLY;
String pendingData = "";
unsigned long lastSync = 0;
unsigned long modeStart = 0;

void handleBLEMode() {
    processPendingConfigs();
    checkPendingApprovals();
    
    static int lastBikeCount = -1;
    int currentBikeCount = getConnectedBikeCount();
    
    if (currentBikeCount != lastBikeCount && lastBikeCount >= 0) {
        if (currentBikeCount > lastBikeCount) {
            Serial.printf("🚲 Bike chegou! Total: %d\n", currentBikeCount);
            setLEDPattern(LED_BIKE_ARRIVED);
        } else {
            Serial.printf("🚲 Bike saiu! Total: %d\n", currentBikeCount);
            setLEDPattern(LED_BIKE_LEFT);
        }
    }
    lastBikeCount = currentBikeCount;
    
    static unsigned long lastCountShow = 0;
    if (millis() - lastCountShow > 30000 && currentBikeCount > 0) {
        setLEDPattern(LED_COUNT_BIKES, currentBikeCount);
        lastCountShow = millis();
    }
    
    static unsigned long lastCleanup = 0;
    if (millis() - lastCleanup > 60000) {
        cleanupOldConnections();
        lastCleanup = millis();
    }
    
    bool needsSync = false;
    
    if (pendingData.length() > 0) needsSync = true;
    
    if (isConfigValid() && millis() - lastSync > getSyncInterval() * 1000) {
        needsSync = true;
    }
    
    if (needsSync) {
        Serial.println("📶 Ativando WiFi para sync...");
        setLEDPattern(LED_WIFI_SYNC);
        currentMode = MODE_WIFI_SYNC;
        modeStart = millis();
    }
}

void handleWiFiMode() {
    static bool wifiConnected = false;
    
    if (!wifiConnected) {
        File configFile = LittleFS.open("/config.json", "r");
        if (configFile) {
            DynamicJsonDocument doc(512);
            deserializeJson(doc, configFile);
            configFile.close();
            
            String ssid = doc["wifi"]["ssid"];
            String pass = doc["wifi"]["password"];
            
            WiFi.begin(ssid.c_str(), pass.c_str());
            Serial.println("📶 Conectando WiFi...");
        }
        wifiConnected = true;
        return;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("✅ WiFi conectado - sincronizando...");
        
        if (!ntpSynced) {
            syncNTP();
            sendNTPToBike();
        }
        
        Serial.println("📥 Verificando config da central...");
        if (!downloadConfigs()) {
            File localConfig = LittleFS.open("/config.json", "r");
            CentralConfigCache config = getCentralConfig();
            String baseName = String(config.base_id);
            if (localConfig) {
                DynamicJsonDocument doc(256);
                if (deserializeJson(doc, localConfig) == DeserializationError::Ok) {
                    baseName = doc["base_name"] | config.base_id;
                }
                localConfig.close();
            }
            
            // createNewBase(config.base_id, baseName); // TODO: implement
        }
        
        sendHeartbeat();
        checkPendingApprovals();
        
        if (uploadPendingData(pendingData)) {
            Serial.println("✅ Dados sincronizados");
        }
        
        lastSync = millis();
        currentMode = MODE_SHUTDOWN;
        
    } else if (millis() - modeStart > 30000) {
        Serial.println("⚠️ WiFi timeout");
        currentMode = MODE_SHUTDOWN;
    }
}

void handleShutdownMode() {
    Serial.println("🔴 Desligando WiFi...");
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    
    currentMode = MODE_BLE_ONLY;
    setLEDPattern(LED_BLE_READY);
    Serial.println("✅ Voltando ao modo BLE");
}