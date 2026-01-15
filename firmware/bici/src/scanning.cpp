#include "scanning.h"
#include <WiFi.h>

ScanningState::ScanningState(ConfigManager& configMgr, BufferManager& bufferMgr) 
    : configManager(configMgr), bufferManager(bufferMgr), lastScan(0) {}

BikeState ScanningState::update() {
    BikeConfig& config = configManager.getConfig();
    
    if (millis() - lastScan > config.wifi.scan_interval_sec * 1000) {
        performWiFiScan();
        lastScan = millis();
    }
    
    // Check battery
    if (getBatteryVoltage() < config.battery.low_voltage && !config.dev_mode) {
        return STATE_SLEEP;
    }
    
    return STATE_SCANNING;
}

void ScanningState::performWiFiScan() {
    BikeConfig& config = configManager.getConfig();
    Serial.printf("📡 Starting WiFi scan (timeout: %dms, max: %d networks)...\n", 
                  config.wifi.scan_timeout_ms, config.wifi.max_networks);
    
    int networks = WiFi.scanNetworks();
    int savedCount = 0;
    
    for (int i = 0; i < networks && savedCount < config.wifi.max_networks; i++) {
        if (WiFi.RSSI(i) > config.wifi.rssi_threshold) {
            NetworkData network;
            
            // Copy SSID (truncate if too long)
            String ssid = WiFi.SSID(i);
            strncpy(network.ssid, ssid.c_str(), sizeof(network.ssid) - 1);
            network.ssid[sizeof(network.ssid) - 1] = '\0';
            
            // Copy BSSID as string
            String bssid = WiFi.BSSIDstr(i);
            strncpy(network.bssid, bssid.c_str(), sizeof(network.bssid) - 1);
            network.bssid[sizeof(network.bssid) - 1] = '\0';
            
            network.rssi = WiFi.RSSI(i);
            network.channel = WiFi.channel(i);
            
            // Add to current session
            NetworkData networks[1] = {network};
            bufferManager.addScan(millis(), networks, 1);
            savedCount++;
        }
    }
    WiFi.scanDelete();
    
    Serial.printf("📶 Found %d networks, saved %d\n", networks, savedCount);
    
    // Radio coordination delay
    Serial.printf("⏱️ Radio coordination delay: %dms\n", config.power.radio_coordination_delay_ms);
    delay(config.power.radio_coordination_delay_ms);
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