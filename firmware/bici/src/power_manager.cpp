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

BikeState PowerManager::checkBatteryState(BikeState currentState, const BikeConfig& config) {
    float voltage = getBatteryVoltage();
    
    // Critical battery check
    if (voltage <= config.battery.critical_voltage && !config.dev_mode) {
        if (currentState != STATE_SLEEP) {
            Serial.printf("🔋 Critical battery: %.2fV - entering sleep\n", voltage);
            return STATE_SLEEP;
        }
    }
    
    return currentState; // No change
}

bool PowerManager::isLowBattery(const BikeConfig& config) {
    return getBatteryVoltage() <= config.battery.low_voltage;
}

bool PowerManager::isCriticalBattery(const BikeConfig& config) {
    return getBatteryVoltage() <= config.battery.critical_voltage;
}

void PowerManager::enterDeepSleep(uint32_t seconds) {
    Serial.printf("💤 Deep sleep for %d seconds\n", seconds);
    
    esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);
    esp_deep_sleep_start();
}

void PowerManager::logBatteryStatus() {
    uint8_t percent = getBatteryPercent();
    float voltage = getBatteryVoltage();
    bool charging = isBatteryCharging();
    
    Serial.printf("🔋 Battery: %d%% (%.2fV) %s\n", 
                  percent, voltage, 
                  charging ? "⚡CHARGING" : "");
}

String PowerManager::getBatteryReport(const BikeConfig& config) {
    DynamicJsonDocument doc(256);
    doc["battery_percent"] = getBatteryPercent();
    doc["battery_voltage"] = getBatteryVoltage();
    doc["is_charging"] = isBatteryCharging();
    doc["heap"] = ESP.getFreeHeap();
    
    String result;
    serializeJson(doc, result);
    return result;
}