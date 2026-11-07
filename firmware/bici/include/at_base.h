#ifndef AT_BASE_H
#define AT_BASE_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
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

    // Transferência fragmentada (F04/F05)
    bool transferActive;
    bool awaitingAck;
    uint16_t fragIndex;
    uint16_t fragTotal;
    uint8_t fragRetries;
    uint32_t lastFragmentSend;
    bool transferComplete;
    bool configRequested;
    uint32_t busyUntilMs;        // R13: espera real após busy

    // Notificações BLE -> loop (sem processamento pesado na task do NimBLE)
    QueueHandle_t notifQueue;
    static const size_t NOTIF_MAX_LEN = 516;

    bool connectToBase(NimBLEAdvertisedDevice* device);
    void sendStatus();
    bool sendFragment(uint16_t index);
    void sendConfigAck(bool success, uint32_t version);
    void startTransfer();
    void abortTransfer();
    void processPendingNotifications();

public:
    AtBaseState(ConfigManager& configMgr, BufferManager& bufferMgr);
    bool scanForBase();
    bool requestConfig();
    BikeState update();
    void onBLEDisconnected();
    void onNotification(const String& message);
    bool isConnected() const;
    bool isTransferring() const { return transferActive; }
};

#endif
