#include <Arduino.h>
#include <NimBLEDevice.h>

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("🔵 Testing BLE on ESP32-C3...");
    
    try {
        NimBLEDevice::init("BPR_Test");
        Serial.println("✅ NimBLE initialized successfully");
        
        NimBLEDevice::setPower(ESP_PWR_LVL_P1); // Low power
        Serial.println("✅ Power set");
        
        NimBLEScan* pScan = NimBLEDevice::getScan();
        Serial.println("✅ Scan object created");
        
        pScan->setActiveScan(false); // Passive scan to save power
        pScan->setInterval(1349);
        pScan->setWindow(449);
        
        Serial.println("🔍 Starting 5 second scan...");
        NimBLEScanResults results = pScan->start(5, false);
        
        Serial.printf("📊 Scan complete. Found %d devices\n", results.getCount());
        
        for (int i = 0; i < results.getCount(); i++) {
            NimBLEAdvertisedDevice device = results.getDevice(i);
            Serial.printf("  Device %d: %s (RSSI: %d)\n", 
                         i, 
                         device.getName().c_str(), 
                         device.getRSSI());
        }
        
        Serial.println("✅ BLE test completed successfully");
        
    } catch (const std::exception& e) {
        Serial.printf("❌ BLE test failed: %s\n", e.what());
    }
}

void loop() {
    Serial.printf("[%lu] 📊 Heap: %d bytes\n", millis()/1000, esp_get_free_heap_size());
    delay(5000);
}