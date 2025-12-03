#include <Arduino.h>
#include <WiFi.h>
#include "ble_simple.h"
#include "firebase_client.h"

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("\n🚲 BPR Central - WiFi + Firebase + BLE");
    Serial.println("=====================================");
    
    Serial.printf("📊 Free heap: %d bytes\n", esp_get_free_heap_size());
    Serial.printf("🔋 Chip model: %s\n", ESP.getChipModel());
    
    // Wait a bit before initializing BLE
    delay(1000);
    
    // Initialize Firebase + WiFi
    if (initFirebase()) {
        Serial.println("✅ Firebase + WiFi initialized");
    } else {
        Serial.println("⚠️ Firebase failed, continuing BLE-only");
    }
    
    // Try to initialize BLE
    if (initBLESimple()) {
        Serial.println("✅ BLE initialized successfully");
    } else {
        Serial.println("❌ BLE initialization failed");
    }
    
    Serial.printf("📊 Free heap after init: %d bytes\n", esp_get_free_heap_size());
    Serial.println("✅ Setup complete");
}

void loop() {
    static unsigned long lastLog = 0;
    
    if (Serial.available()) {
        String cmd = Serial.readString();
        cmd.trim();
        
        if (cmd == "status") {
            Serial.printf("📊 Heap: %d | BLE: %s | Firebase: %s | Clients: %d | Uptime: %lus\n", 
                         esp_get_free_heap_size(), 
                         isBLEReady() ? "Ready" : "Not Ready",
                         getFirebaseStatus().c_str(),
                         getConnectedClients(),
                         millis()/1000);
        } else if (cmd == "scan") {
            bleScanOnce();
        } else if (cmd == "server") {
            startBLEServer();
        } else if (cmd == "restart") {
            Serial.println("🔄 Restarting...");
            ESP.restart();
        }
    }
    
    if (millis() - lastLog > 15000) {
        Serial.printf("[%lu] 📊 Heap: %d | BLE: %s | Firebase: %s\n", 
                     millis()/1000, 
                     esp_get_free_heap_size(),
                     isBLEReady() ? "OK" : "FAIL",
                     getFirebaseStatus().c_str());
        lastLog = millis();
    }
    
    delay(100);
}