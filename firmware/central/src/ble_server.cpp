#include "ble_server.h"
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include "constants.h"
#include "bike_manager.h"

// Static members
NimBLEServer *BPRBLEServer::pServer = nullptr;
NimBLEService *BPRBLEServer::pService = nullptr;
NimBLECharacteristic *BPRBLEServer::pDataChar = nullptr;
NimBLECharacteristic *BPRBLEServer::pConfigChar = nullptr;
uint8_t BPRBLEServer::connectedBikes = 0;
std::map<uint16_t, String> BPRBLEServer::connectedDevices;

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

        BPRBLEServer::connectedDevices[conn_handle] = "";
        Serial.printf("📝 Stored connection handle %d\n", conn_handle);
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
            Serial.printf("🔵 Bike %s disconnected (%d total)\n", bikeId.c_str(), BPRBLEServer::connectedBikes);
        }
        else
        {
            Serial.printf("🔵 Device disconnected (%d total)\n", BPRBLEServer::connectedBikes);
        }

        NimBLEDevice::startAdvertising();

        // Notificar bike_pairing sobre desconexão (só se conhece a bike)
        if (!bikeId.isEmpty())
        {
            BPRBLEServer::onBikeDisconnected(bikeId);
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
        
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, value.c_str());

        if (!error && doc["bike_id"])
        {
            String bikeId = doc["bike_id"];
            
            // Find first available handle (simpler approach)
            uint16_t conn_handle = 0;
            for (auto &pair : BPRBLEServer::connectedDevices) {
                if (pair.second.isEmpty() || pair.second == bikeId) {
                    conn_handle = pair.first;
                    break;
                }
            }
            
            if (conn_handle != 0) {
                // Map bike to this connection handle
                BPRBLEServer::connectedDevices[conn_handle] = bikeId;
                Serial.printf("📝 Bike %s mapped to handle %d\n", bikeId.c_str(), conn_handle);

                // Check for pending config and send immediately
                BPRBLEServer::checkAndSendPendingConfig(bikeId, conn_handle);
            } else {
                Serial.printf("⚠️ Could not find handle for bike %s\n", bikeId.c_str());
            }

            // Delegate processing to bike_pairing
            BPRBLEServer::onBikeDataReceived(bikeId, String(value.c_str()));
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
        
        DynamicJsonDocument doc(512);
        DeserializationError error = deserializeJson(doc, value.c_str());

        if (!error && doc["bike_id"])
        {
            String bikeId = doc["bike_id"];
            BPRBLEServer::onConfigRequest(bikeId, String(value.c_str()));
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
