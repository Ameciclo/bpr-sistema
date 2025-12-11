#pragma once
#include <Arduino.h>

enum LEDPattern {
    LED_OFF,
    LED_BOOT,
    LED_SETUP_MODE,
    LED_BLE_READY,
    LED_BIKE_ARRIVED,
    LED_BIKE_LEFT,
    LED_WIFI_SYNC,
    LED_COUNT_BIKES,
    LED_ERROR
};

void initLED();
void setLEDPattern(LEDPattern pattern, int count = 0);
void updateLED();