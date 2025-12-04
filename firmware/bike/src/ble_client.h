#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <vector>
#include "bike_config.h"

class BLEClient : public NimBLEClientCallbacks {
private:
  NimBLEClient* pClient;
  NimBLEAddress baseAddress;
  char bikeId[16];
  bool connected;
  bool baseFound;

public:
  BLEClient();
  void init(const char* bikeId);
  bool scanForBase(const char* baseName);
  bool connectToBase();
  bool sendStatus(const BikeStatus& status);
  bool receiveConfig(BikeConfig& config);
  bool sendWifiData(const std::vector<WifiRecord>& records);
  void disconnect();
  bool isConnected();
  
  // Callbacks
  void onConnect(NimBLEClient* pClient) override;
  void onDisconnect(NimBLEClient* pClient) override;
};