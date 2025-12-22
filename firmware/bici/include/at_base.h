#ifndef AT_BASE_H
#define AT_BASE_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "constants.h"
#include "config_manager.h"
#include "buffer_manager.h"

class AtBaseState;

class ClientCallbacks : public NimBLEClientCallbacks {
private:
    AtBaseState* atBaseState;
    
public:
    ClientCallbacks(AtBaseState* state) : atBaseState(state) {}
    void onDisconnect(NimBLEClient* pClient);
};

class AtBaseState {
private:
    ConfigManager& configManager;
    BufferManager& bufferManager;
    NimBLEClient* pClient;
    NimBLERemoteCharacteristic* pDataChar;
    NimBLERemoteCharacteristic* pConfigChar;
    bool bleConnected;
    unsigned long lastStatusSent;
    
    bool connectToBase(NimBLEAdvertisedDevice* device);
    void sendStatus();
    void sendWiFiData();
    float getBatteryVoltage();

public:
    AtBaseState(ConfigManager& configMgr, BufferManager& bufferMgr);
    bool scanForBase();
    bool requestConfig();
    BikeState update();
    void onBLEDisconnected();
    bool isConnected() const;
};

#endif