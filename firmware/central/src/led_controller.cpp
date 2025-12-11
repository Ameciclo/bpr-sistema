#include "led_controller.h"
#include "config_manager.h"

LEDPattern currentLEDPattern = LED_OFF;
unsigned long ledLastChange = 0;
bool ledState = false;
int ledCounter = 0;
int bikesToShow = 0;

void initLED() {
    pinMode(getLedPin(), OUTPUT);
    setLEDPattern(LED_BOOT);
}

void setLEDPattern(LEDPattern pattern, int count) {
    currentLEDPattern = pattern;
    ledCounter = 0;
    ledLastChange = millis();
    bikesToShow = count;
    
    String patternName[] = {"OFF", "BOOT", "SETUP_MODE", "BLE_READY", "BIKE_ARRIVED", "BIKE_LEFT", "WIFI_SYNC", "COUNT_BIKES", "ERROR"};
    Serial.printf("💡 LED: %s%s\n", patternName[pattern].c_str(), count > 0 ? (" (" + String(count) + ")").c_str() : "");
}

void updateLED() {
    unsigned long now = millis();
    
    switch (currentLEDPattern) {
        case LED_OFF:
            digitalWrite(getLedPin(), LOW);
            break;
            
        case LED_BOOT:
            if (now - ledLastChange > 100) {
                ledState = !ledState;
                digitalWrite(getLedPin(), ledState);
                ledLastChange = now;
            }
            break;
            
        case LED_SETUP_MODE:
            if (now - ledLastChange > 1000) {
                ledState = !ledState;
                digitalWrite(getLedPin(), ledState);
                ledLastChange = now;
            }
            break;
            
        case LED_BLE_READY:
            if (now - ledLastChange > 2000) {
                ledState = !ledState;
                digitalWrite(getLedPin(), ledState);
                ledLastChange = now;
            }
            break;
            
        case LED_BIKE_ARRIVED:
            if (ledCounter < 6) {
                if (now - ledLastChange > 200) {
                    ledState = !ledState;
                    digitalWrite(getLedPin(), ledState);
                    ledLastChange = now;
                    ledCounter++;
                }
            } else {
                currentLEDPattern = LED_BLE_READY;
                ledCounter = 0;
            }
            break;
            
        case LED_BIKE_LEFT:
            if (ledCounter == 0) {
                digitalWrite(getLedPin(), HIGH);
                ledLastChange = now;
                ledCounter = 1;
            } else if (now - ledLastChange > 1000) {
                digitalWrite(getLedPin(), LOW);
                currentLEDPattern = LED_BLE_READY;
                ledCounter = 0;
            }
            break;
            
        case LED_WIFI_SYNC:
            if (now - ledLastChange > 500) {
                ledState = !ledState;
                digitalWrite(getLedPin(), ledState);
                ledLastChange = now;
            }
            break;
            
        case LED_COUNT_BIKES:
            if (ledCounter < bikesToShow * 2) {
                if (now - ledLastChange > 300) {
                    ledState = !ledState;
                    digitalWrite(getLedPin(), ledState);
                    ledLastChange = now;
                    ledCounter++;
                }
            } else if (now - ledLastChange > 2000) {
                currentLEDPattern = LED_BLE_READY;
                ledCounter = 0;
            }
            break;
            
        case LED_ERROR:
            if (now - ledLastChange > 50) {
                ledState = !ledState;
                digitalWrite(getLedPin(), ledState);
                ledLastChange = now;
            }
            break;
    }
}