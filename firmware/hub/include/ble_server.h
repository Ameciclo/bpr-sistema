#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <map>

class BLEServer {
public:
    static bool start();
    static void stop();
    static uint8_t getConnectedBikes();
    static void pushConfigToBike(const String& bikeId, const String& config);
    
    // Callbacks implementados externamente no bike_pairing.cpp
    static void onBikeConnected(const String& bikeId);
    static void onBikeDisconnected(const String& bikeId);
    static void onBikeDataReceived(const String& bikeId, const String& jsonData);
    static void onConfigRequest(const String& bikeId, const String& request);

private:
    static NimBLEServer* pServer;
    static NimBLEService* pService;
    static NimBLECharacteristic* pDataChar;
    static NimBLECharacteristic* pConfigChar;
    static uint8_t connectedBikes;
    static std::map<uint16_t, String> connectedDevices;
};