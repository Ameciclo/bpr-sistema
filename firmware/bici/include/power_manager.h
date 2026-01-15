#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "constants.h"
#include "config_manager.h"

class PowerManager {
public:
    PowerManager();
    
    // Battery measurement
    uint8_t getBatteryPercent();
    float getBatteryVoltage();
    bool isBatteryCharging();
    
    // Battery states
    BikeState checkBatteryState(BikeState currentState, const BikeConfig& config);
    bool isLowBattery(const BikeConfig& config);
    bool isCriticalBattery(const BikeConfig& config);
    
    // Sleep management
    void enterDeepSleep(uint32_t seconds);
    
    // Reporting
    void logBatteryStatus();
    String getBatteryReport(const BikeConfig& config);
    
private:
    uint8_t lastBatteryPercent;
    uint32_t lastBatteryRead;
    float lastVoltage;
    bool emergencyMode;
    uint32_t emergencyModeStart;
};

#endif