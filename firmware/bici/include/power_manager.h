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
    // X01: retorno verificável — falha ao armar o wakeup NÃO dorme
    bool enterDeepSleep(uint32_t seconds);
    
    // Reporting
    void logBatteryStatus();
    // Patch de horários (§5): o heartbeat carrega o trecho atual, o contador
    // local e a última referência de hora efetivamente guardada
    String getHeartbeatReport(const BikeConfig& config, bool hasPendingData,
                              uint16_t pendingScans = 0, size_t pendingBytes = 0,
                              uint16_t bootId = 0, uint32_t counterMs = 0,
                              uint32_t lastRefId = 0);
    
private:
    uint8_t lastBatteryPercent;
    uint32_t lastBatteryRead;
    float lastVoltage;
    bool emergencyMode;
    uint32_t emergencyModeStart;
};

#endif