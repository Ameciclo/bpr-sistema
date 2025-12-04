#pragma once
#include <Arduino.h>
#include <vector>
#include <LittleFS.h>
#include "bike_config.h"

class WiFiScanner {
private:
  std::vector<WifiRecord> records; // Buffer RAM temporário
  uint32_t lastScanTime;
  uint16_t scanCount;
  uint16_t fileIndex; // Índice do arquivo atual
  
  void flushToFile();
  void loadPendingData();
  String getDataFileName(uint16_t index);

public:
  WiFiScanner();
  void init();
  bool performScan(uint32_t currentTime);
  std::vector<WifiRecord>& getRecords();
  void clearRecords();
  bool hasRecords();
  size_t getRecordCount();
  size_t getTotalStoredCount();
  bool exportAllData(String& jsonData);
  void clearAllData();
};