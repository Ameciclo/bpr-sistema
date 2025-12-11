#pragma once
#include <Arduino.h>

enum LEDPattern {
    PATTERN_OFF,
    PATTERN_BOOT,
    PATTERN_CONFIG,
    PATTERN_BLE_READY,
    PATTERN_SYNC,
    PATTERN_ERROR,
    PATTERN_BIKE_ARRIVED,
    PATTERN_BIKE_LEFT,
    PATTERN_COUNT
};

class LEDController {
public:
    LEDController();
    void begin();
    void update();
    
    void setPattern(LEDPattern pattern);
    void bootPattern();
    void configPattern();
    void bleReadyPattern();
    void syncPattern();
    void errorPattern();
    void bikeArrivedPattern();
    void bikeLeftPattern();
    void countPattern(uint8_t count);
    void off();

private:
    LEDPattern currentPattern;
    uint32_t patternStartTime;
    bool ledState;
    uint8_t blinkCount;
    uint8_t targetBlinks;
    
    void updateBlinkPattern(uint32_t elapsed, uint8_t maxBlinks, uint16_t onTime, uint16_t offTime);
};