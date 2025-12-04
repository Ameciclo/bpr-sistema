#pragma once
#include <Arduino.h>

class BatteryMonitor {
private:
  uint16_t samples[10];
  uint8_t sampleIndex;
  float lastReading;

public:
  BatteryMonitor();
  void init();
  float readVoltage();
  bool isLowBattery(float threshold);
  uint8_t getPercentage();
};