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
    bool scanInFlight;          // R01: scan assíncrono em andamento
    unsigned long scanStart;    // R01: origem do prazo global do scan
    bool saturationLogged;      // R17: diagnóstico de saturação una vez

    void performWiFiScan();
    void consumeScanResults(int networks, const BikeConfig& config);

public:
    ScanningState(ConfigManager& configMgr, BufferManager& bufferMgr);
    BikeState update();
};

#endif