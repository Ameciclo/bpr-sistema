#include "ble_central.h"
#include <NimBLEDevice.h>

static NimBLEScan* pBLEScan = nullptr;
static BLEBikeData connectedBikes[BLE_MAX_BIKES];
static int bikeCount = 0;
static bool bleInitialized = false;
static QueueHandle_t bleEventQueue;

class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        std::string deviceName = advertisedDevice->getName();
        
        // Look for bike devices (should advertise with "BPR_" prefix)
        if (deviceName.find("BPR_bike") == 0) {
            Serial.printf("🔍 Found bike: %s (RSSI: %d)\n", 
                         deviceName.c_str(), 
                         advertisedDevice->getRSSI());
            
            // Try to connect if we have space
            if (bikeCount < BLE_MAX_BIKES) {
                std::string bikeId = deviceName.substr(4); // Remove "BPR_" prefix
                connectToBike(bikeId.c_str());
            }
        }
    }
};

class ClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) {
        Serial.printf("🔵 Connected to bike: %s\n", 
                     pClient->getPeerAddress().toString().c_str());
        pClient->updateConnParams(120, 120, 0, 60);
    }
    
    void onDisconnect(NimBLEClient* pClient) {
        Serial.printf("🔴 Bike disconnected: %s\n", 
                     pClient->getPeerAddress().toString().c_str());
        
        // Remove from connected bikes list
        for (int i = 0; i < bikeCount; i++) {
            // Find and remove disconnected bike
            // Implementation depends on how we track connections
        }
    }
};

bool initBLECentral() {
    if (bleInitialized) return true;
    
    Serial.println("🔵 Initializing BLE Central...");
    
    try {
        NimBLEDevice::init("BPR_Central");
        NimBLEDevice::setPower(ESP_PWR_LVL_P3);
        
        // Configure for central role
        pBLEScan = NimBLEDevice::getScan();
        pBLEScan->setAdvertisedDeviceCallbacks(new ScanCallbacks(), false);
        pBLEScan->setActiveScan(true);
        pBLEScan->setInterval(100);
        pBLEScan->setWindow(99);
        
        // Initialize bike data array
        memset(connectedBikes, 0, sizeof(connectedBikes));
        bikeCount = 0;
        
        bleEventQueue = xQueueCreate(10, sizeof(int));
        
        bleInitialized = true;
        Serial.println("✅ BLE Central initialized");
        return true;
        
    } catch (const std::exception& e) {
        Serial.printf("❌ BLE init failed: %s\n", e.what());
        return false;
    }
}

void bleCentralTask(void *parameter) {
    // Wait for system stability
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    if (!initBLECentral()) {
        Serial.println("❌ Failed to initialize BLE Central");
        vTaskDelete(NULL);
        return;
    }
    
    unsigned long lastScan = 0;
    
    while (true) {
        unsigned long now = millis();
        
        // Periodic scan for new bikes
        if (now - lastScan > BLE_SCAN_INTERVAL_MS) {
            if (bikeCount < BLE_MAX_BIKES) {
                Serial.println("🔍 Scanning for bikes...");
                startBikeScan();
                lastScan = now;
            }
        }
        
        // Check connected bikes status
        for (int i = 0; i < bikeCount; i++) {
            if (connectedBikes[i].is_connected) {
                // Update last seen timestamp
                connectedBikes[i].last_seen = now;
                
                // Check if bike is still responsive
                if (now - connectedBikes[i].last_seen > 60000) { // 1 minute timeout
                    Serial.printf("⚠️ Bike %s timeout, disconnecting\n", 
                                 connectedBikes[i].bike_id);
                    disconnectFromBike(connectedBikes[i].bike_id);
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void startBikeScan() {
    if (pBLEScan && bleInitialized) {
        pBLEScan->start(BLE_SCAN_TIME_SEC, false);
    }
}

void stopBikeScan() {
    if (pBLEScan && pBLEScan->isScanning()) {
        pBLEScan->stop();
    }
}

bool connectToBike(const char* bike_id) {
    if (bikeCount >= BLE_MAX_BIKES) {
        Serial.println("⚠️ Max bikes connected");
        return false;
    }
    
    Serial.printf("🔗 Attempting to connect to bike: %s\n", bike_id);
    
    // Create client
    NimBLEClient* pClient = NimBLEDevice::createClient();
    if (!pClient) {
        Serial.println("❌ Failed to create BLE client");
        return false;
    }
    
    pClient->setClientCallbacks(new ClientCallbacks(), false);
    pClient->setConnectTimeout(5);
    
    // For now, just add to connected list (actual connection logic would go here)
    strncpy(connectedBikes[bikeCount].bike_id, bike_id, 15);
    connectedBikes[bikeCount].bike_id[15] = '\0';
    connectedBikes[bikeCount].is_connected = true;
    connectedBikes[bikeCount].last_seen = millis();
    connectedBikes[bikeCount].battery_voltage = 0.0;
    connectedBikes[bikeCount].rssi = -50;
    
    bikeCount++;
    
    Serial.printf("✅ Connected to bike %s (%d/%d)\n", bike_id, bikeCount, BLE_MAX_BIKES);
    return true;
}

void disconnectFromBike(const char* bike_id) {
    for (int i = 0; i < bikeCount; i++) {
        if (strcmp(connectedBikes[i].bike_id, bike_id) == 0) {
            Serial.printf("🔌 Disconnecting bike: %s\n", bike_id);
            
            // Shift array to remove this bike
            for (int j = i; j < bikeCount - 1; j++) {
                connectedBikes[j] = connectedBikes[j + 1];
            }
            bikeCount--;
            break;
        }
    }
}

int getConnectedBikesCount() {
    return bikeCount;
}

BLEBikeData* getConnectedBikes() {
    return connectedBikes;
}

bool sendConfigToBike(const char* bike_id, const char* config_json) {
    // Find the bike
    for (int i = 0; i < bikeCount; i++) {
        if (strcmp(connectedBikes[i].bike_id, bike_id) == 0 && 
            connectedBikes[i].is_connected) {
            
            Serial.printf("📤 Sending config to bike %s: %s\n", bike_id, config_json);
            
            // Here we would send the actual BLE characteristic write
            // For now, just simulate success
            return true;
        }
    }
    
    Serial.printf("❌ Bike %s not found or not connected\n", bike_id);
    return false;
}