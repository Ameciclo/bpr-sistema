#pragma once
#include <Arduino.h>
#include <vector>
#include "bike_config.h"

class WiFiScanner {
private:
  std::vector<WifiRecord> records;
  uint32_t lastScanTime;
  uint16_t scanCount;

public:
  WiFiScanner();
  void init();
  bool performScan(uint32_t currentTime);
  std::vector<WifiRecord>& getRecords();
  void clearRecords();
  bool hasRecords();
  size_t getRecordCount();
};