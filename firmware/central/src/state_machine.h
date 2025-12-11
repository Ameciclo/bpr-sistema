#pragma once
#include <Arduino.h>

enum CentralMode {
    MODE_SETUP_AP,
    MODE_BLE_ONLY,
    MODE_WIFI_SYNC,
    MODE_SHUTDOWN
};

extern CentralMode currentMode;
extern String pendingData;
extern unsigned long lastSync;
extern unsigned long modeStart;

void handleBLEMode();
void handleWiFiMode();
void handleShutdownMode();