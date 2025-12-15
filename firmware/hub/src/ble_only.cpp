#include "ble_only.h"
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include "constants.h"
#include "config_manager.h"
#include "buffer_manager.h"
#include "led_controller.h"
#include "state_machine.h"
#include "bike_config.h"

extern ConfigManager configManager;
extern BufferManager bufferManager;
extern LEDController ledController;
extern StateMachine stateMachine;

static NimBLEServer* pServer = nullptr;
static NimBLEService* pService = nullptr;
static NimBLECharacteristic* pDataChar = nullptr;
static NimBLECharacteristic* pConfigChar = nullptr;
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
            Serial.printf("📥 Data received: %s\n", value.c_str());
            bufferManager.addData((uint8_t*)value.data(), value.length());
        }
    }
};

class ConfigCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar) {
        std::string value = pChar->getValue();
        if (value.length() > 0) {
            handleConfigRequest(String(value.c_str()));
        }
    }
    
    void onRead(NimBLECharacteristic* pChar) {
        // Config responses are sent via onWrite, not onRead
    }
    
private:
    void handleConfigRequest(const String& request) {
        DynamicJsonDocument doc(256);
        DeserializationError error = deserializeJson(doc, request);
        
        if (error) {
            Serial.printf("❌ Config request parse error: %s\n", error.c_str());
            return;
        }
        
        String type = doc["type"] | "";
        String bikeId = doc["bike_id"] | "";
        
        if (type == "config_request" && bikeId.length() > 0) {
            String response;
            if (BikeConfigManager::handleConfigRequest(bikeId, response)) {
                pConfigChar->setValue(response.c_str());
                pConfigChar->notify();
            }
        } else if (type == "config_received") {
            String status = doc["status"] | "";
            Serial.printf("📋 Config confirmation from %s: %s\n", 
                         bikeId.c_str(), status.c_str());
        }
    }
};

void BLEOnly::enter() {
    Serial.println("🔵 Entering BLE_ONLY mode");
    
    // Initialize bike config manager
    BikeConfigManager::init();
    
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
    
    // Config characteristic
    pConfigChar = pService->createCharacteristic(
        BLE_CHAR_CONFIG_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
    );
    pConfigChar->setCallbacks(new ConfigCallbacks());
    
    pService->start();
    
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    NimBLEDevice::startAdvertising();
    
    Serial.println("📡 BLE Server started with config support");
    ledController.bleReadyPattern();
}

void BLEOnly::update() {
    uint32_t now = millis();
    
    // Check sync trigger
    if (now - lastSyncCheck > configManager.getConfig().sync_interval_ms()) {
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
    uint32_t ledInterval = configManager.getConfig().intervals.led_count_sec * 1000;
    if (now - lastLedUpdate > ledInterval) {
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