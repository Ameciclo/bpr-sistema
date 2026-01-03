#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <map>

class BPRBLEServer {
public:
    static bool start();
    static void stop();
    static uint8_t getConnectedBikes();
    static void pushConfigToBike(const String& bikeId, const String& config);
    static bool isBikeConnected(const String& bikeId);
    static void forceDisconnectBike(const String& bikeId);
    static void sendConfigToHandle(uint16_t handle, const String& bikeId, const String& config);
    static void checkAndSendPendingConfig(const String& bikeId, uint16_t handle);
    
    // Callback setters para BLE Server
    typedef void (*DataCallback)(const String& bikeId, const String& jsonData);
    typedef void (*ConnectCallback)(const String& bikeId);
    typedef void (*DisconnectCallback)(const String& bikeId);
    typedef void (*ConfigCallback)(const String& bikeId, const String& request);
    
    static void setDataCallback(DataCallback callback);
    static void setConnectCallback(ConnectCallback callback);
    static void setDisconnectCallback(DisconnectCallback callback);
    static void setConfigCallback(ConfigCallback callback);
    
    // === ADVERTISING STATUS MANAGEMENT ===
    static void setBusyStatus(bool busy, uint32_t durationSeconds = 300);
    static void updateAdvertisingStatus();
    static bool isCentralBusy();
    
    // Callbacks - agora como ponteiros para funções externas
    static DataCallback dataCallback;
    static ConnectCallback connectCallback;
    static DisconnectCallback disconnectCallback;
    static ConfigCallback configCallback;
    
    // Static members - public para acesso das callbacks
    static NimBLEServer* pServer;
    static NimBLEService* pService;
    static NimBLECharacteristic* pDataChar;
    static NimBLECharacteristic* pConfigChar;
    static uint8_t connectedBikes;
    static std::map<uint16_t, String> connectedDevices;
    
    // BUSY status tracking
    static bool isBusy;
    static uint32_t busyUntil;
};