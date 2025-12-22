#include "power_manager.h"
#include "constants.h"
#include <esp_sleep.h>

PowerManager::PowerManager() : lastBatteryPercent(100), lastBatteryRead(0), 
                               emergencyMode(false), emergencyModeStart(0) {}

uint8_t PowerManager::getBatteryPercent() {
    float voltage = getBatteryVoltage();
    
    // USB power detection
    if (voltage >= 3.9) {
        return 100;
    }
    
    // Convert voltage to percentage (3.2V = 0%, 4.2V = 100%)
    float percent = ((voltage - 3.2) / (4.2 - 3.2)) * 100.0;
    
    // Clamp to 0-100%
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    
    return (uint8_t)percent;
}

float PowerManager::getBatteryVoltage() {
    if (millis() - lastBatteryRead < 5000) {
        return lastVoltage;
    }
    
    int adc = analogRead(BATTERY_PIN);
    lastBatteryRead = millis();
    
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

bool PowerManager::isBatteryCharging() {
    return getBatteryVoltage() >= 3.9; // USB power detected
}

BikeState PowerManager::checkBatteryState(BikeState currentState, const Config& config) {
    uint8_t battery = getBatteryPercent();
    
    // Transição para LOW_BATTERY
    if (battery <= config.battery_critical_percent && currentState != STATE_LOW_BATTERY) {
        logBatteryTransition(currentState, STATE_LOW_BATTERY, battery);
        enterEmergencyMode();
        return STATE_LOW_BATTERY;
    }
    
    // Saída de LOW_BATTERY (hysteresis)
    if (currentState == STATE_LOW_BATTERY && battery >= config.battery_recovery_percent) {
        logBatteryTransition(STATE_LOW_BATTERY, STATE_WAKE_CHECK, battery);
        exitEmergencyMode();
        return STATE_WAKE_CHECK;
    }
    
    return currentState; // Sem mudança
}

bool PowerManager::isLowBattery(const Config& config) {
    return getBatteryPercent() <= config.battery_low_percent;
}

bool PowerManager::isCriticalBattery(const Config& config) {
    return getBatteryPercent() <= config.battery_critical_percent;
}

bool PowerManager::hasBatteryRecovered(const Config& config) {
    return getBatteryPercent() >= config.battery_recovery_percent;
}

uint32_t PowerManager::getScanInterval(const Config& config) {
    uint8_t battery = getBatteryPercent();
    
    if (battery <= config.battery_critical_percent) {
        return config.scan_interval_critical_sec;     // 5min+ (economia extrema)
    } else if (battery <= config.battery_low_percent) {
        return config.scan_interval_low_battery_sec;  // 2min (economia)
    } else {
        return config.scan_interval_normal_sec;       // 25s (normal)
    }
}

uint32_t PowerManager::getCheckinInterval(const Config& config) {
    uint8_t battery = getBatteryPercent();
    
    if (battery <= config.battery_critical_percent) {
        return config.checkin_low_battery_sec * 2;    // 20min (muito esporádico)
    } else if (battery <= config.battery_low_percent) {
        return config.checkin_low_battery_sec;        // 10min (esporádico)
    } else {
        return config.checkin_interval_sec;           // 5min (normal)
    }
}

uint32_t PowerManager::getSleepDuration(const Config& config) {
    uint8_t battery = getBatteryPercent();
    
    if (battery <= config.battery_critical_percent) {
        return config.deep_sleep_critical_sec;        // 2h (economia extrema)
    } else {
        return config.deep_sleep_duration_sec;        // 1h (normal)
    }
}

void PowerManager::enterDeepSleep(uint32_t seconds) {
    Serial.printf("💤 Deep sleep for %d seconds\n", seconds);
    
    esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);
    esp_deep_sleep_start();
}

void PowerManager::scheduleWakeup(uint32_t base_interval, uint32_t extra_delay) {
    uint32_t total_sleep = base_interval + extra_delay;
    enterDeepSleep(total_sleep);
}

BikeState PowerManager::handleLowBatteryState(const Config& config) {
    uint8_t battery = getBatteryPercent();
    
    // Se bateria melhorou, sair do modo
    if (battery >= config.battery_recovery_percent) {
        return STATE_WAKE_CHECK;
    }
    
    // Sem base, dormir muito tempo
    enterDeepSleep(config.deep_sleep_critical_sec);
    return STATE_LOW_BATTERY;
}

bool PowerManager::tryEmergencyUpload() {
    // Esta função seria implementada com lógica específica
    // para tentar upload urgente se na base
    Serial.println("🚨 Attempting emergency upload...");
    return false; // Placeholder
}

void PowerManager::enterEmergencyMode() {
    if (!emergencyMode) {
        emergencyMode = true;
        emergencyModeStart = millis();
        Serial.println("🚨 EMERGENCY MODE ACTIVATED");
    }
}

void PowerManager::exitEmergencyMode() {
    if (emergencyMode) {
        emergencyMode = false;
        uint32_t duration = (millis() - emergencyModeStart) / 1000;
        Serial.printf("✅ Emergency mode ended after %d seconds\n", duration);
    }
}

void PowerManager::logBatteryStatus() {
    uint8_t percent = getBatteryPercent();
    float voltage = getBatteryVoltage();
    bool charging = isBatteryCharging();
    
    Serial.printf("🔋 Battery: %d%% (%.2fV) %s %s\n", 
                  percent, voltage, 
                  charging ? "⚡CHARGING" : "",
                  emergencyMode ? "🚨EMERGENCY" : "");
}

String PowerManager::getBatteryReport(const Config& config) {
    DynamicJsonDocument doc(256);
    doc["battery_percent"] = getBatteryPercent();
    doc["battery_voltage"] = getBatteryVoltage();
    doc["is_charging"] = isBatteryCharging();
    doc["emergency_mode"] = emergencyMode;
    doc["scan_interval_sec"] = getScanInterval(config);
    doc["checkin_interval_sec"] = getCheckinInterval(config);
    
    String result;
    serializeJson(doc, result);
    return result;
}

void PowerManager::logBatteryTransition(BikeState from, BikeState to, uint8_t batteryPercent) {
    Serial.printf("🔋 Battery transition: State %d → %d (Battery: %d%%)\n", 
                  from, to, batteryPercent);
}