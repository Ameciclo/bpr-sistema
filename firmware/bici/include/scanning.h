#ifndef SCANNING_H
#define SCANNING_H

#include <Arduino.h>
#include "constants.h"
#include "config_manager.h"
#include "buffer_manager.h"

class ScanningState {
private:
    ConfigManager& configManager;
    BufferManager& bufferManager;
    unsigned long lastScan;
    
    void performWiFiScan();
    float getBatteryVoltage();

public:
    ScanningState(ConfigManager& configMgr, BufferManager& bufferMgr);
    BikeState update();
};

#endif