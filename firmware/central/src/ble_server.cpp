#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include "bike_manager.h"
#include "ble_server.h"
#include "constants.h"

// Static members
NimBLEServer *BPRBLEServer::pServer = nullptr;
NimBLEService *BPRBLEServer::pService = nullptr;
NimBLECharacteristic *BPRBLEServer::pDataChar = nullptr;
NimBLECharacteristic *BPRBLEServer::pConfigChar = nullptr;
uint8_t BPRBLEServer::connectedBikes = 0;
std::map<uint16_t, String> BPRBLEServer::connectedDevices;
std::map<uint16_t, uint32_t> BPRBLEServer::connectionTimeouts;

// BUSY status tracking
bool BPRBLEServer::isBusy = false;
uint32_t BPRBLEServer::busyUntil = 0;

// Callback pointers
BPRBLEServer::DataCallback BPRBLEServer::dataCallback = nullptr;
BPRBLEServer::ConnectCallback BPRBLEServer::connectCallback = nullptr;
BPRBLEServer::DisconnectCallback BPRBLEServer::disconnectCallback = nullptr;
BPRBLEServer::ConfigCallback BPRBLEServer::configCallback = nullptr;

class ServerCallbacks : public NimBLEServerCallbacks
{
    void onConnect(NimBLEServer *pServer, ble_gap_conn_desc *desc)
    {
        uint16_t conn_handle = desc->conn_handle;
        NimBLEAddress addr = NimBLEAddress(desc->peer_id_addr);

        BPRBLEServer::connectedBikes++;
        Serial.printf("🔵 BLE CONNECT: %s | Handle: %d | Total: %d\n",
                      addr.toString().c_str(), conn_handle, BPRBLEServer::connectedBikes);
        NimBLEDevice::startAdvertising();

        // Store as unidentified with 10s timeout
        BPRBLEServer::connectedDevices[conn_handle] = "";
        BPRBLEServer::connectionTimeouts[conn_handle] = millis() + 10000; // 10s timeout
        Serial.printf("📝 Device connected: %s (10s to identify as BPR bike)\n", addr.toString().c_str());
    }

    void onDisconnect(NimBLEServer *pServer, ble_gap_conn_desc *desc)
    {
        if (BPRBLEServer::connectedBikes > 0)
            BPRBLEServer::connectedBikes--;

        uint16_t conn_handle = desc->conn_handle;
        String bikeId = "";
        if (BPRBLEServer::connectedDevices.find(conn_handle) != BPRBLEServer::connectedDevices.end())
        {
            bikeId = BPRBLEServer::connectedDevices[conn_handle];
            BPRBLEServer::connectedDevices.erase(conn_handle);
            BPRBLEServer::connectionTimeouts.erase(conn_handle); // Remove timeout
            Serial.printf("🔵 Bike %s disconnected (%d total)\n", bikeId.c_str(), BPRBLEServer::connectedBikes);
        }
        else
        {
            Serial.printf("🔵 Device disconnected (%d total)\n", BPRBLEServer::connectedBikes);
        }

        NimBLEDevice::startAdvertising();

        // Notificar via callback se registrado
        if (BPRBLEServer::disconnectCallback) {
            BPRBLEServer::disconnectCallback(bikeId);
        }
    }
};

class DataCallbacks : public NimBLECharacteristicCallbacks
{
    void onWrite(NimBLECharacteristic *pChar)
    {
        std::string value = pChar->getValue();
        if (value.length() == 0 || value.length() > 2048) {
            Serial.printf("⚠️ Invalid data length: %d\n", value.length());
            return;
        }
        
        DynamicJsonDocument doc(BIKE_DATA_BUFFER);
        DeserializationError error = deserializeJson(doc, value.c_str());

        if (!error && doc["bike_id"])
        {
            String bikeId = doc["bike_id"];
            
            // Only accept devices with valid BPR bike_id format
            if (!bikeId.startsWith("bpr-")) {
                Serial.printf("❌ Invalid device - not a BPR bike: %s\n", bikeId.c_str());
                return;
            }
            
            // Find first available handle (simpler approach)
            uint16_t conn_handle = 0;
            for (auto &pair : BPRBLEServer::connectedDevices) {
                if (pair.second.isEmpty() || pair.second == bikeId) {
                    conn_handle = pair.first;
                    break;
                }
            }
            
            if (conn_handle != 0) {
                // Map bike to this connection handle and clear timeout
                BPRBLEServer::connectedDevices[conn_handle] = bikeId;
                BPRBLEServer::connectionTimeouts.erase(conn_handle); // Valid bike, remove timeout
                Serial.printf("📝 Bike %s mapped to handle %d\n", bikeId.c_str(), conn_handle);

                // Notificar via callback se registrado
                if (BPRBLEServer::connectCallback) {
                    BPRBLEServer::connectCallback(bikeId);
                }
            } else {
                Serial.printf("⚠️ Could not find handle for bike %s\n", bikeId.c_str());
            }

            // Delegate processing to callback
            if (BPRBLEServer::dataCallback) {
                BPRBLEServer::dataCallback(bikeId, String(value.c_str()));
            }
        }
        else
        {
            Serial.printf("⚠️ Invalid JSON or missing bike_id: %s\n", value.c_str());
        }
    }
};

class ConfigCallbacks : public NimBLECharacteristicCallbacks
{
    void onWrite(NimBLECharacteristic *pChar)
    {
        std::string value = pChar->getValue();
        if (value.length() == 0 || value.length() > 512) {
            Serial.printf("⚠️ Invalid config request length: %d\n", value.length());
            return;
        }
        
        DynamicJsonDocument doc(BLE_COMMAND_BUFFER);
        DeserializationError error = deserializeJson(doc, value.c_str());

        if (!error && doc["bike_id"])
        {
            String bikeId = doc["bike_id"];
            if (BPRBLEServer::configCallback) {
                BPRBLEServer::configCallback(bikeId, String(value.c_str()));
            }
        }
        else
        {
            Serial.printf("⚠️ Invalid config request JSON: %s\n", value.c_str());
        }
    }
};

bool BPRBLEServer::start()
{
    Serial.println("🔵 Starting BLE Server");

    NimBLEDevice::init(BLE_DEVICE_NAME);
    NimBLEDevice::setPower(ESP_PWR_LVL_P3);
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    pService = pServer->createService(BLE_SERVICE_UUID);

    // Data characteristic
    pDataChar = pService->createCharacteristic(
        BLE_CHAR_DATA_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
    pDataChar->setCallbacks(new DataCallbacks());

    // Config characteristic
    pConfigChar = pService->createCharacteristic(
        BLE_CHAR_CONFIG_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
    pConfigChar->setCallbacks(new ConfigCallbacks());

    pService->start();

    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    NimBLEDevice::startAdvertising();

    Serial.println("📡 BLE Server started successfully");
    return true;
}

void BPRBLEServer::stop()
{
    if (pServer)
    {
        pServer->getAdvertising()->stop();
        NimBLEDevice::deinit(false);
        pServer = nullptr;
        pService = nullptr;
        pDataChar = nullptr;
        pConfigChar = nullptr;
        connectedBikes = 0;
        connectedDevices.clear();
        isBusy = false;
        busyUntil = 0;
    }
    Serial.println("🔚 BLE Server stopped");
}

uint8_t BPRBLEServer::getConnectedBikes()
{
    return connectedBikes;
}

bool BPRBLEServer::isBikeConnected(const String &bikeId)
{
    for (auto &pair : connectedDevices) {
        if (pair.second == bikeId) {
            return true;
        }
    }
    return false;
}

void BPRBLEServer::pushConfigToBike(const String &bikeId, const String &config)
{
    if (!pConfigChar) return;
    
    // Encontrar handle da bike específica
    uint16_t targetHandle = 0;
    for (auto &pair : connectedDevices) {
        if (pair.second == bikeId) {
            targetHandle = pair.first;
            break;
        }
    }
    
    if (targetHandle == 0) {
        Serial.printf("❌ Bike %s not connected, cannot send config\n", bikeId.c_str());
        return;
    }
    
    // Enviar config direcionada
    sendConfigToHandle(targetHandle, bikeId, config);
}

void BPRBLEServer::sendConfigToHandle(uint16_t handle, const String &bikeId, const String &config)
{
    if (!pConfigChar || handle == 0) {
        Serial.printf("❌ Invalid config char or handle for %s\n", bikeId.c_str());
        return;
    }
    
    // Validate config size
    if (config.length() > 800) {
        Serial.printf("⚠️ Config too large for %s: %d bytes\n", bikeId.c_str(), config.length());
        return;
    }
    
    // Send config directly without wrapper to avoid corruption
    pConfigChar->setValue(config.c_str());
    pConfigChar->notify();
    
    Serial.printf("📤 Config sent to %s (handle %d): %d bytes\n", bikeId.c_str(), handle, config.length());
}

void BPRBLEServer::checkAndSendPendingConfig(const String &bikeId, uint16_t handle)
{
    // Verificar se tem config pendente via bike_pairing
    if (BikeManager::hasConfigUpdate(bikeId)) {
        String config = BikeManager::getConfigForBike(bikeId);
        sendConfigToHandle(handle, bikeId, config);
        BikeManager::markConfigSent(bikeId);
        
        Serial.printf("⚡ Immediate config sent to %s on connection\n", bikeId.c_str());
    } else {
        Serial.printf("📝 No pending config for %s\n", bikeId.c_str());
    }
}

void BPRBLEServer::forceDisconnectBike(const String &bikeId)
{
    if (!pServer) return;
    
    // Encontrar handle da bike específica
    uint16_t targetHandle = 0;
    for (auto &pair : connectedDevices) {
        if (pair.second == bikeId) {
            targetHandle = pair.first;
            break;
        }
    }
    
    if (targetHandle != 0) {
        pServer->disconnect(targetHandle);
        Serial.printf("🚫 Forced disconnect of bike %s (handle %d)\n", bikeId.c_str(), targetHandle);
    } else {
        Serial.printf("❌ Cannot disconnect %s - not found\n", bikeId.c_str());
    }
}

void BPRBLEServer::setBusyStatus(bool busy, uint32_t durationSeconds) {
    isBusy = busy;
    if (busy) {
        if (durationSeconds == 0) {
            busyUntil = UINT32_MAX; // Permanent busy (OFF mode)
            Serial.println("🔵 BLE Status: OFF (permanent)");
        } else {
            busyUntil = millis() + (durationSeconds * 1000);
            Serial.printf("🔵 BLE Status: BUSY for %d seconds\n", durationSeconds);
        }
    } else {
        busyUntil = 0;
        Serial.println("🔵 BLE Status: READY");
    }
    updateAdvertisingStatus();
    
    // Imprimir info do BLE após atualizar status
    printBLEInfo();
}

void BPRBLEServer::updateAdvertisingStatus() {
    if (!pServer) return;
    
    // Check if busy period expired (but not if permanent)
    if (isBusy && busyUntil != UINT32_MAX && millis() > busyUntil) {
        isBusy = false;
        Serial.println("🔵 BLE BUSY period expired - back to READY");
    }
    
    // Only update advertising if status actually changed
    static bool lastBusyState = false;
    static uint32_t lastBusyUntil = 0;
    
    if (isBusy == lastBusyState && busyUntil == lastBusyUntil) {
        return; // No change, skip update
    }
    
    lastBusyState = isBusy;
    lastBusyUntil = busyUntil;
    
    // Update device name based on status
    String deviceName;
    if (busyUntil == UINT32_MAX) {
        deviceName = "BPR Central OFF";  // Permanent OFF mode
    } else if (isBusy) {
        deviceName = "BPR Central BUSY"; // Temporary busy
    } else {
        deviceName = "BPR Central";      // Ready
    }
    
    // Stop current advertising
    pServer->getAdvertising()->stop();
    
    // Simpler approach: just restart advertising with setName
    NimBLEAdvertising *pAdvertising = pServer->getAdvertising();
    
    // Clear existing data and set new name
    pAdvertising->reset();
    pAdvertising->setName(deviceName.c_str());
    pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->start();
    
    Serial.printf("📡 BLE Advertising updated: %s\n", deviceName.c_str());
}

void BPRBLEServer::setDataCallback(DataCallback callback) {
    dataCallback = callback;
}

void BPRBLEServer::setConnectCallback(ConnectCallback callback) {
    connectCallback = callback;
}

void BPRBLEServer::setDisconnectCallback(DisconnectCallback callback) {
    disconnectCallback = callback;
}

void BPRBLEServer::setConfigCallback(ConfigCallback callback) {
    configCallback = callback;
}

bool BPRBLEServer::isCentralBusy() {
    if (isBusy && busyUntil != UINT32_MAX && millis() > busyUntil) {
        isBusy = false;
    }
    return isBusy;
}

void BPRBLEServer::printBLEInfo() {
    if (NimBLEDevice::getInitialized()) {
        Serial.printf("📱 BLE MAC Address: %s\n", NimBLEDevice::getAddress().toString().c_str());
        
        if (NimBLEDevice::getAdvertising()->isAdvertising()) {
            Serial.println("✅ BLE Advertising is ACTIVE");
        } else {
            Serial.println("❌ BLE Advertising is NOT ACTIVE!");
        }
    } else {
        Serial.println("❌ BLE not initialized!");
    }
}

void BPRBLEServer::checkAdvertisingStatus() {
    if (!pServer) return;
    
    if (NimBLEDevice::getAdvertising() && NimBLEDevice::getAdvertising()->isAdvertising()) {
        Serial.println("✅ BLE Advertising is still ACTIVE");
    } else {
        Serial.println("❌ BLE Advertising STOPPED! Restarting...");
        setBusyStatus(true, 0); // Force restart
    }
}

void BPRBLEServer::checkConnectionTimeouts() {
    if (!pServer) return;
    
    uint32_t now = millis();
    std::vector<uint16_t> toDisconnect;
    
    // Find expired connections
    for (auto &pair : connectionTimeouts) {
        if (now > pair.second) {
            uint16_t handle = pair.first;
            toDisconnect.push_back(handle);
        }
    }
    
    // Disconnect expired devices
    for (uint16_t handle : toDisconnect) {
        Serial.printf("⏰ Disconnecting unidentified device (handle %d) - timeout\n", handle);
        pServer->disconnect(handle);
    }
}
