#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "constants.h"
#include "config_manager.h"

class PowerManager {
public:
    PowerManager();
    
    // === MEDIÇÃO DE BATERIA ===
    uint8_t getBatteryPercent();           // Converte voltage para % (0-100)
    float getBatteryVoltage();             // Voltage bruto do ADC
    bool isBatteryCharging();              // Detecta se está carregando (USB)
    
    // === ESTADOS DE BATERIA ===
    BikeState checkBatteryState(BikeState currentState, const Config& config);  // Decide próximo estado
    bool isLowBattery(const Config& config);                   // < 25%
    bool isCriticalBattery(const Config& config);              // < 15%
    bool hasBatteryRecovered(const Config& config);            // > 30% (hysteresis)
    
    // === INTERVALOS DINÂMICOS (baseados na bateria) ===
    uint32_t getScanInterval(const Config& config);            // WiFi scan interval
    uint32_t getCheckinInterval(const Config& config);         // "Dar oi" interval
    uint32_t getSleepDuration(const Config& config);           // Deep sleep duration
    
    // === SLEEP MANAGEMENT ===
    void enterDeepSleep(uint32_t seconds);
    void scheduleWakeup(uint32_t base_interval, uint32_t extra_delay);
    
    // === LOW BATTERY BEHAVIOR ===
    BikeState handleLowBatteryState(const Config& config);     // Lógica específica do estado LOW_BATTERY
    bool tryEmergencyUpload();             // Upload urgente se na base
    void enterEmergencyMode();             // Configurações de emergência
    void exitEmergencyMode();              // Volta ao normal
    
    // === REPORTING ===
    void logBatteryStatus();               // Log detalhado da bateria
    String getBatteryReport(const Config& config);             // JSON para enviar à central
    
    // === GETTERS ===
    bool isEmergencyMode() const { return emergencyMode; }
    
private:
    uint8_t lastBatteryPercent;
    uint32_t lastBatteryRead;
    float lastVoltage;
    bool emergencyMode;
    uint32_t emergencyModeStart;
    
    void logBatteryTransition(BikeState from, BikeState to, uint8_t batteryPercent);
};

#endif