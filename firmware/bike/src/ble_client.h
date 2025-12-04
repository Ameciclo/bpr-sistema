#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <vector>
#include "bike_config.h"

class BikeClient : public NimBLEClientCallbacks {
private:
  NimBLEClient* pClient;
  NimBLEAddress baseAddress;
  String bikeId;
  String baseBleName;
  bool connected;
  bool baseFound;
  bool registered;

public:
  BikeClient();
  void init(const String& bikeId);
  bool scanForBase(const String& baseName);
  bool connectToBase();
  bool registerWithBase();
  bool sendBikeInfo();
  bool sendStatus(float batteryVoltage, uint16_t recordsCount);
  bool receiveConfig(String& configJson);
  bool sendWifiData(const std::vector<WifiRecord>& records);
  void disconnect();
  bool isConnected();
  bool isRegistered() const { return registered; }
  
  // Callbacks
  void onConnect(NimBLEClient* pClient) override;
  void onDisconnect(NimBLEClient* pClient) override;
};