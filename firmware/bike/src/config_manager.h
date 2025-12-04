#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

class ConfigManager {
private:
  String bikeId;
  String baseBleName;
  uint16_t scanIntervalSec;
  uint16_t scanIntervalLowBattSec;
  uint16_t deepSleepSec;
  float minBatteryVoltage;
  uint16_t maxWifiRecords;
  uint8_t bleScanTimeoutSec;
  uint8_t emergencyTimeoutSec;
  uint8_t statusReportIntervalSec;
  bool ledEnabled;
  bool debugEnabled;
  uint32_t configTimestamp;

public:
  ConfigManager();
  bool loadFromFile(const char* filename = "/config.json");
  bool saveToFile(const char* filename = "/config.json");
  bool updateFromBLE(const String& jsonConfig);
  void setDefaults();
  
  // Getters
  const String& getBikeId() const { return bikeId; }
  const String& getBaseBleName() const { return baseBleName; }
  uint16_t getScanInterval() const { return scanIntervalSec; }
  uint16_t getScanIntervalLowBatt() const { return scanIntervalLowBattSec; }
  uint16_t getDeepSleepSec() const { return deepSleepSec; }
  float getMinBatteryVoltage() const { return minBatteryVoltage; }
  uint16_t getMaxWifiRecords() const { return maxWifiRecords; }
  uint8_t getBleScanTimeout() const { return bleScanTimeoutSec; }
  uint8_t getEmergencyTimeout() const { return emergencyTimeoutSec; }
  uint8_t getStatusReportInterval() const { return statusReportIntervalSec; }
  bool isLedEnabled() const { return ledEnabled; }
  bool isDebugEnabled() const { return debugEnabled; }
  uint32_t getConfigTimestamp() const { return configTimestamp; }
  
  // Setters
  void setBikeId(const String& id) { bikeId = id; }
  void setBaseBleName(const String& name) { baseBleName = name; }
  void setConfigTimestamp(uint32_t timestamp) { configTimestamp = timestamp; }
  
  void printConfig();
};