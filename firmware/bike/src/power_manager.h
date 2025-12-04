#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>

class PowerManager {
private:
  uint32_t bootTime;
  uint32_t lastSleepTime;

public:
  PowerManager();
  void init();
  void enterLightSleep(uint32_t seconds);
  void enterDeepSleep(uint32_t seconds);
  void optimizeForScanning();
  void optimizeForBLE();
  uint32_t getUptimeSeconds();
  esp_sleep_wakeup_cause_t getWakeupCause();
  void printWakeupReason();
};