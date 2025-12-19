#include "led_controller.h"
#include "constants.h"
#include "config_manager.h"

extern ConfigManager configManager;

LEDController::LEDController() : 
    currentPattern(PATTERN_OFF), 
    patternStartTime(0), 
    ledState(false), 
    blinkCount(0), 
    targetBlinks(0) {}

void LEDController::begin() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
}

void LEDController::update() {
    uint32_t now = millis();
    uint32_t elapsed = now - patternStartTime;
    
    switch (currentPattern) {
        case PATTERN_OFF:
            digitalWrite(LED_PIN, LOW);
            break;
            
        case PATTERN_BOOT:
            if (elapsed % LED_BOOT_INTERVAL < LED_BOOT_INTERVAL / 2) {
                digitalWrite(LED_PIN, HIGH);
            } else {
                digitalWrite(LED_PIN, LOW);
            }
            break;
            
        case PATTERN_CONFIG:
            // Fast blink for config mode
            if (elapsed % 200 < 100) {
                digitalWrite(LED_PIN, HIGH);
            } else {
                digitalWrite(LED_PIN, LOW);
            }
            break;
            
        case PATTERN_BLE_READY:
            if (elapsed % configManager.getConfig().led.ble_ms < configManager.getConfig().led.ble_ms / 10) {
                digitalWrite(LED_PIN, HIGH);
            } else {
                digitalWrite(LED_PIN, LOW);
            }
            break;
            
        case PATTERN_SYNC:
            if (elapsed % configManager.getConfig().led.sync_ms < configManager.getConfig().led.sync_ms / 2) {
                digitalWrite(LED_PIN, HIGH);
            } else {
                digitalWrite(LED_PIN, LOW);
            }
            break;
            
        case PATTERN_ERROR:
            if (elapsed % configManager.getConfig().led.error_ms < configManager.getConfig().led.error_ms / 2) {
                digitalWrite(LED_PIN, HIGH);
            } else {
                digitalWrite(LED_PIN, LOW);
            }
            break;
            
        case PATTERN_BIKE_ARRIVED:
            // 3 quick blinks
            updateBlinkPattern(elapsed, 3, 150, 100);
            break;
            
        case PATTERN_BIKE_LEFT:
            // 1 long blink
            if (elapsed < 1000) {
                digitalWrite(LED_PIN, HIGH);
            } else {
                digitalWrite(LED_PIN, LOW);
                if (elapsed > 1200) {
                    setPattern(PATTERN_BLE_READY);
                }
            }
            break;
            
        case PATTERN_COUNT:
            updateBlinkPattern(elapsed, targetBlinks, configManager.getConfig().led.count_ms, configManager.getConfig().led.count_ms / 2);
            break;
    }
}

void LEDController::setPattern(LEDPattern pattern) {
    currentPattern = pattern;
    patternStartTime = millis();
    blinkCount = 0;
    ledState = false;
}

void LEDController::bootPattern() {
    setPattern(PATTERN_BOOT);
}

void LEDController::configPattern() {
    setPattern(PATTERN_CONFIG);
}

void LEDController::bikePairingPattern() {
    setPattern(PATTERN_BLE_READY);
}

void LEDController::syncPattern() {
    setPattern(PATTERN_SYNC);
}

void LEDController::errorPattern() {
    setPattern(PATTERN_ERROR);
}

void LEDController::bikeArrivedPattern() {
    setPattern(PATTERN_BIKE_ARRIVED);
}

void LEDController::bikeLeftPattern() {
    setPattern(PATTERN_BIKE_LEFT);
}

void LEDController::countPattern(uint8_t count) {
    targetBlinks = count;
    setPattern(PATTERN_COUNT);
}

void LEDController::off() {
    setPattern(PATTERN_OFF);
}

void LEDController::updateBlinkPattern(uint32_t elapsed, uint8_t maxBlinks, uint16_t onTime, uint16_t offTime) {
    uint16_t cycleTime = onTime + offTime;
    uint32_t currentCycle = elapsed / cycleTime;
    uint32_t cyclePosition = elapsed % cycleTime;
    
    if (currentCycle < maxBlinks) {
        if (cyclePosition < onTime) {
            digitalWrite(LED_PIN, HIGH);
        } else {
            digitalWrite(LED_PIN, LOW);
        }
    } else {
        digitalWrite(LED_PIN, LOW);
        // After count pattern, return to BLE ready
        if (currentPattern == PATTERN_COUNT && elapsed > (maxBlinks * cycleTime + configManager.getConfig().led.count_pause_ms)) {
            setPattern(PATTERN_BLE_READY);
        }
        // After bike arrived pattern, return to BLE ready
        if (currentPattern == PATTERN_BIKE_ARRIVED && elapsed > (maxBlinks * cycleTime + 500)) {
            setPattern(PATTERN_BLE_READY);
        }
    }
}