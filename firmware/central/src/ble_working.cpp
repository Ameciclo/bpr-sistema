#include "ble_working.h"
#include <NimBLEDevice.h>

static NimBLEServer* pServer = nullptr;
static bool bleReady = false;

class ServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) {
        Serial.println("🔵 BLE: Client connected");
        NimBLEDevice::startAdvertising();
    }
    
    void onDisconnect(NimBLEServer* pServer) {
        Serial.println("🔴 BLE: Client disconnected");
        NimBLEDevice::startAdvertising();
    }
};

class CharCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar) {
        Serial.printf("📝 BLE: Data received: %s\n", pChar->getValue().c_str());
    }
};

bool initBLEWorking() {
    if (bleReady) return true;
    
    Serial.println("🔵 Initializing BLE (v2.x compatible)...");
    
    NimBLEDevice::init("BPR_Base");
    
#ifdef ESP_PLATFORM
    NimBLEDevice::setPower(ESP_PWR_LVL_P3);
#else
    NimBLEDevice::setPower(3);
#endif
    
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());
    
    // Create simple service
    NimBLEService* pService = pServer->createService("BAAD");
    
    NimBLECharacteristic* pChar = pService->createCharacteristic(
        "F00D",
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
    );
    
    pChar->setValue("BPR Ready");
    pChar->setCallbacks(new CharCallbacks());
    
    pService->start();
    
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(pService->getUUID());
    pAdvertising->setScanResponse(true);
    pAdvertising->start();
    
    bleReady = true;
    Serial.println("✅ BLE Working - Ready for connections");
    return true;
}

void bleWorkingTask(void *parameter) {
    vTaskDelay(pdMS_TO_TICKS(10000)); // Wait 10s for system stability
    
    if (initBLEWorking()) {
        while (true) {
            if (pServer && pServer->getConnectedCount() > 0) {
                // Send notification to connected clients
                NimBLEService* pSvc = pServer->getServiceByUUID("BAAD");
                if (pSvc) {
                    NimBLECharacteristic* pChr = pSvc->getCharacteristic("F00D");
                    if (pChr) {
                        String data = "Ping:" + String(millis());
                        pChr->setValue(data.c_str());
                        pChr->notify();
                    }
                }
            }
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
    
    vTaskDelete(NULL);
}

bool isBLEWorking() {
    return bleReady && pServer != nullptr;
}