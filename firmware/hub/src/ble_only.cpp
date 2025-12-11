#include "ble_only.h"
#include <NimBLEDevice.h>
#include "constants.h"
#include "config_manager.h"
#include "buffer_manager.h"
#include "led_controller.h"
#include "state_machine.h"
#include "state_machine.h"

extern ConfigManager configManager;
extern BufferManager bufferManager;
extern LEDController ledController;
extern StateMachine stateMachine;

static NimBLEServer* pServer = nullptr;
static NimBLEService* pService = nullptr;
static NimBLECharacteristic* pDataChar = nullptr;
static uint8_t connectedBikes = 0;
static uint32_t lastSyncCheck = 0;
static uint32_t lastHeartbeat = 0;

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) {
        connectedBikes++;
        Serial.printf("Bike connected: %d\n", connectedBikes);
        ledController.bikeArrivedPattern();
        NimBLEDevice::startAdvertising();
    }
    
    void onDisconnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) {
        if (connectedBikes > 0) connectedBikes--;
        Serial.printf("Bike disconnected: %d\n", connectedBikes);
        ledController.bikeLeftPattern();
        NimBLEDevice::startAdvertising();
    }
};

class DataCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar) {
        std::string value = pChar->getValue();
        if (value.length() > 0) {
            bufferManager.addData((uint8_t*)value.data(), value.length());
        }
    }
};

void BLEOnly::enter() {
    Serial.println("🔵 Entering BLE_ONLY mode");
    
    NimBLEDevice::init(BLE_DEVICE_NAME);
    NimBLEDevice::setPower(ESP_PWR_LVL_P3);
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());
    
    pService = pServer->createService(BLE_SERVICE_UUID);
    
    // Data characteristic
    pDataChar = pService->createCharacteristic(
        BLE_CHAR_DATA_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
    );
    pDataChar->setCallbacks(new DataCallbacks());
    

    
    pService->start();
    
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    NimBLEDevice::startAdvertising();
    
    Serial.println("📡 BLE Server started, advertising...");
    ledController.bleReadyPattern();
}

void BLEOnly::update() {
    uint32_t now = millis();
    
    // Check sync trigger
    if (now - lastSyncCheck > configManager.getConfig().sync_interval_ms) {
        lastSyncCheck = now;
        
        if (bufferManager.needsSync()) {
            Serial.println("🔄 Triggering sync");
            stateMachine.handleEvent(EVENT_SYNC_TRIGGER);
            return;
        }
    }
    
    // Heartbeat
    if (now - lastHeartbeat > HEARTBEAT_INTERVAL) {
        lastHeartbeat = now;
        sendHeartbeat();
    }
    
    // Update LED status
    static uint32_t lastLedUpdate = 0;
    if (now - lastLedUpdate > 30000) { // Every 30s
        lastLedUpdate = now;
        ledController.countPattern(connectedBikes);
    }
}

void BLEOnly::exit() {
    if (pServer) {
        pServer->getAdvertising()->stop();
        NimBLEDevice::deinit(false);
    }
    Serial.println("🔚 Exiting BLE_ONLY mode");
}

uint8_t BLEOnly::getConnectedBikes() {
    return connectedBikes;
}

void BLEOnly::sendHeartbeat() {
    // This will be sent during next WiFi sync
    bufferManager.addHeartbeat(connectedBikes);
    Serial.printf("💓 Heartbeat: %d bikes connected\n", connectedBikes);
}