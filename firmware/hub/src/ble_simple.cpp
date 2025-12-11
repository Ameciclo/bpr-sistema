#include "ble_simple.h"
#include <NimBLEDevice.h>

static bool bleReady = false;

bool initBLESimple() {
    Serial.println("🔵 Initializing BLE...");
    try {
        NimBLEDevice::init("BPR Hub");
        bleReady = true;
        Serial.println("✅ BLE initialized");
        return true;
    } catch (...) {
        Serial.println("❌ BLE init failed");
        return false;
    }
}

bool startBLEServer() {
    Serial.println("📡 Starting BLE Server...");
    return true;
}

bool isBLEReady() {
    return bleReady;
}

int getConnectedClients() {
    return 0;
}

void bleScanOnce() {
    Serial.println("🔍 BLE scan...");
}

void setBLEDeviceName(String name) {
    Serial.printf("📡 BLE name: %s\n", name.c_str());
}

void onBLEConnect(uint16_t connHandle) {
    Serial.printf("🔗 BLE connect: %d\n", connHandle);
}

void onBLEMessage(uint16_t connHandle, String message) {
    Serial.printf("📨 BLE message: %s\n", message.c_str());
}

void sendMessage(uint16_t connHandle, String message) {
    Serial.printf("📤 BLE send: %s\n", message.c_str());
}

void registerPendingBike(String bleName, String macAddress) {
    Serial.printf("🆕 New bike: %s\n", bleName.c_str());
}