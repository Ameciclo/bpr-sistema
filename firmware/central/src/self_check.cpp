#include <LittleFS.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include "constants.h"
#include "self_check.h"

SelfCheck::SelfCheck() {}

bool SelfCheck::systemCheck() {
    Serial.println("🔧 Starting system self-check...");
    
    bool allOk = true;
    
    // Check memory
    if (!checkMemory()) {
        Serial.println("❌ Memory check failed");
        allOk = false;
    }
    
    // Check LittleFS
    if (!checkFileSystem()) {
        Serial.println("❌ FileSystem check failed");
        allOk = false;
    }
    
    // Check LED
    if (!checkLED()) {
        Serial.println("❌ LED check failed");
        allOk = false;
    }
    
    // Check WiFi capability - DISABLED to avoid conflicts
    // if (!checkWiFi()) {
    //     Serial.println("❌ WiFi check failed");
    //     allOk = false;
    // }
    Serial.println("📶 WiFi check skipped (avoiding conflicts)");
    
    // Check BLE capability
    // if (!checkBLE()) {
    //     Serial.println("❌ BLE check failed");
    //     allOk = false;
    // }
    Serial.println("🔵 BLE check skipped (temporary)");
    
    if (allOk) {
        Serial.println("✅ All system checks passed");
    } else {
        Serial.println("⚠️ Some system checks failed");
    }
    
    return allOk;
}

bool SelfCheck::checkMemory() {
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t minHeap = 50000; // 50KB minimum
    
    Serial.printf("💾 Free heap: %d bytes\n", freeHeap);
    
    if (freeHeap < minHeap) {
        Serial.printf("❌ Low memory: %d < %d\n", freeHeap, minHeap);
        return false;
    }
    
    return true;
}

bool SelfCheck::checkFileSystem() {
    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes = LittleFS.usedBytes();
    
    Serial.printf("📁 LittleFS: %d/%d bytes used\n", usedBytes, totalBytes);
    
    if (totalBytes == 0) {
        Serial.println("❌ LittleFS not mounted");
        return false;
    }
    
    // Criar diretórios essenciais
    if (!LittleFS.exists("/buffer")) {
        if (LittleFS.mkdir("/buffer")) {
            Serial.println("📁 Created /buffer directory");
        } else {
            Serial.println("❌ Failed to create /buffer directory");
            return false;
        }
    }
    
    if (!LittleFS.exists("/backup")) {
        if (LittleFS.mkdir("/backup")) {
            Serial.println("📁 Created /backup directory");
        } else {
            Serial.println("❌ Failed to create /backup directory");
            return false;
        }
    }
    
    // Test write/read
    File testFile = LittleFS.open("/test.txt", "w");
    if (!testFile) {
        Serial.println("❌ Cannot create test file");
        return false;
    }
    
    testFile.println("test");
    testFile.close();
    
    testFile = LittleFS.open("/test.txt", "r");
    if (!testFile) {
        Serial.println("❌ Cannot read test file");
        return false;
    }
    
    String content = testFile.readString();
    testFile.close();
    LittleFS.remove("/test.txt");
    
    if (!content.startsWith("test")) {
        Serial.println("❌ File content mismatch");
        return false;
    }
    
    return true;
}

bool SelfCheck::checkLED() {
    pinMode(LED_PIN, OUTPUT);
    
    // Quick blink test
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    
    Serial.printf("💡 LED test on pin %d\n", LED_PIN);
    return true;
}

bool SelfCheck::checkWiFi() {
    WiFi.mode(WIFI_STA);
    delay(100);
    
    // Just check if WiFi can be initialized
    if (WiFi.getMode() != WIFI_STA) {
        Serial.println("❌ WiFi mode setting failed");
        WiFi.mode(WIFI_OFF);  // Clean shutdown
        return false;
    }
    
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);  // Properly turn off WiFi
    delay(100);
    Serial.println("📶 WiFi capability OK");
    return true;
}

bool SelfCheck::checkBLE() {
    // Initialize BLE briefly to test
    NimBLEDevice::init("BPR_TEST");
    
    // Check if BLE was initialized properly
    if (!NimBLEDevice::getInitialized()) {
        Serial.println("❌ BLE initialization failed");
        return false;
    }
    
    NimBLEDevice::deinit(false);
    Serial.println("🔵 BLE capability OK");
    return true;
}