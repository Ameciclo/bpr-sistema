#include "scanning.h"
#include <WiFi.h>

ScanningState::ScanningState(ConfigManager& configMgr, BufferManager& bufferMgr) 
    : configManager(configMgr), bufferManager(bufferMgr), lastScan(0) {}

BikeState ScanningState::update() {
    Config& config = configManager.getConfig();
    
    if (millis() - lastScan > config.scan_interval_sec * 1000) {
        performWiFiScan();
        lastScan = millis();
    }
    
    // Check battery
    if (getBatteryVoltage() < config.min_battery_voltage && !config.dev_mode) {
        return STATE_SLEEP;
    }
    
    return STATE_SCANNING;
}

void ScanningState::performWiFiScan() {
    Config& config = configManager.getConfig();
    Serial.printf("📡 Starting WiFi scan (timeout: %dms, max: %d networks)...\n", 
                  config.wifi_scan_timeout_ms, config.wifi_max_networks);
    
    int networks = WiFi.scanNetworks();
    int savedCount = 0;
    
    for (int i = 0; i < networks && !bufferManager.isFull() && savedCount < config.wifi_max_networks; i++) {
        if (WiFi.RSSI(i) > config.wifi_rssi_threshold) {
            WiFiRecord record;
            record.timestamp = millis() / 1000;
            
            // Copy SSID (truncate if too long)
            String ssid = WiFi.SSID(i);
            strncpy(record.ssid, ssid.c_str(), sizeof(record.ssid) - 1);
            record.ssid[sizeof(record.ssid) - 1] = '\0';
            
            // Copy BSSID
            memcpy(record.bssid, WiFi.BSSID(i), 6);
            record.rssi = WiFi.RSSI(i);
            record.channel = WiFi.channel(i);
            
            bufferManager.addWiFiRecord(record);
            savedCount++;
        }
    }
    WiFi.scanDelete();
    
    Serial.printf("📶 Found %d networks, saved %d (buffer: %d/%d)\n", 
                  networks, savedCount, bufferManager.getCount(), config.wifi_max_networks);
    
    // Radio coordination delay
    Serial.printf("⏱️ Radio coordination delay: %dms\n", config.radio_coordination_delay_ms);
    delay(config.radio_coordination_delay_ms);
}

float ScanningState::getBatteryVoltage() {
    static unsigned long lastRead = 0;
    static float lastVoltage = 4.0;
    
    if (millis() - lastRead < 5000) {
        return lastVoltage;
    }
    
    int adc = analogRead(BATTERY_PIN);
    lastRead = millis();
    
    if (adc < 1500) {
        if (lastVoltage != 4.0) {
            Serial.printf("⚠️ ADC=%d - USB power (4.0V)\n", adc);
        }
        lastVoltage = 4.0;
        return 4.0;
    }
    
    lastVoltage = (adc / 4095.0) * 3.3 * 2.0;
    return lastVoltage;
}