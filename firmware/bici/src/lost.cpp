#include "lost.h"

LostState::LostState(ConfigManager& configMgr, BufferManager& bufferMgr) 
    : configManager(configMgr), bufferManager(bufferMgr), searchStartTime(0) {}

BikeState LostState::update() {
    Serial.println("🔍 LOST - Searching for central");
    
    if (searchStartTime == 0) {
        searchStartTime = millis();
    }
    
    // Search for 60 seconds
    if (millis() - searchStartTime > 60000) {
        Serial.println("⏰ Search timeout - going to sleep");
        return STATE_SLEEP;
    }
    
    // Try to find base every 5 seconds
    static unsigned long lastSearch = 0;
    if (millis() - lastSearch > 5000) {
        // Simple BLE scan for base
        NimBLEScan* pScan = NimBLEDevice::getScan();
        pScan->setActiveScan(true);
        NimBLEScanResults results = pScan->start(2, false);
        
        for (int i = 0; i < results.getCount(); i++) {
            NimBLEAdvertisedDevice device = results.getDevice(i);
            if (device.getName().find("BPR Central") != std::string::npos) {
                Serial.println("✅ Found central - transitioning to AT_BASE");
                pScan->clearResults();
                searchStartTime = 0;
                return STATE_AT_BASE;
            }
        }
        
        pScan->clearResults();
        lastSearch = millis();
    }
    
    return STATE_LOST;
}