#include "scanning.h"
#include <WiFi.h>
#include "power_manager.h"

extern PowerManager powerManager;

ScanningState::ScanningState(ConfigManager& configMgr, BufferManager& bufferMgr)
    : configManager(configMgr), bufferManager(bufferMgr), lastScan(0) {}

BikeState ScanningState::update() {
    BikeConfig& config = configManager.getConfig();

    if (millis() - lastScan > config.wifi.scan_interval_sec * 1000UL) {
        performWiFiScan();
        lastScan = millis();
    }

    // Check battery (fonte única: PowerManager)
    if (powerManager.isLowBattery(config) && !config.dev_mode) {
        return STATE_SLEEP;
    }

    return STATE_SCANNING;
}

void ScanningState::performWiFiScan() {
    BikeConfig& config = configManager.getConfig();
    Serial.printf("📡 Starting WiFi scan (timeout: %dms, max: %d networks)...\n",
                  config.wifi.scan_timeout_ms, config.wifi.max_networks);

    // F09: timeout de scan configurável respeitado
    int networks = WiFi.scanNetworks(true, true, false, config.wifi.scan_timeout_ms);

    // F09: acumula as redes DA varredura em um único registro de scan
    NetworkData found[min(MAX_NETWORKS_PER_SCAN, 32)];
    uint8_t savedCount = 0;

    for (int i = 0; i < networks && savedCount < config.wifi.max_networks; i++) {
        if (WiFi.RSSI(i) > config.wifi.rssi_threshold) {
            NetworkData& network = found[savedCount];

            String ssid = WiFi.SSID(i);
            strncpy(network.ssid, ssid.c_str(), sizeof(network.ssid) - 1);
            network.ssid[sizeof(network.ssid) - 1] = '\0';

            String bssid = WiFi.BSSIDstr(i);
            strncpy(network.bssid, bssid.c_str(), sizeof(network.bssid) - 1);
            network.bssid[sizeof(network.bssid) - 1] = '\0';

            network.rssi = WiFi.RSSI(i);
            network.channel = WiFi.channel(i);

            savedCount++;
            if (savedCount >= MAX_NETWORKS_PER_SCAN) break;
        }
    }
    WiFi.scanDelete();

    if (savedCount > 0) {
        bufferManager.addScan(millis(), found, savedCount);
        // F08: checkpoint após cada coleta
        bufferManager.save();
    }

    Serial.printf("📶 Found %d networks, saved %d in 1 scan record\n", networks, savedCount);

    // Radio coordination delay
    delay(config.power.radio_coordination_delay_ms);
}
