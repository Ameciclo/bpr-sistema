#include "ble_server.h"
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include "constants.h"
#include "bike_config_manager.h"

// Static members
NimBLEServer *BLEServer::pServer = nullptr;
NimBLEService *BLEServer::pService = nullptr;
NimBLECharacteristic *BLEServer::pDataChar = nullptr;
NimBLECharacteristic *BLEServer::pConfigChar = nullptr;
uint8_t BLEServer::connectedBikes = 0;
std::map<uint16_t, String> BLEServer::connectedDevices;

class ServerCallbacks : public NimBLEServerCallbacks
{
    void onConnect(NimBLEServer *pServer, ble_gap_conn_desc *desc)
    {
        uint16_t conn_handle = desc->conn_handle;
        NimBLEAddress addr = NimBLEAddress(desc->peer_id_addr);

        BLEServer::connectedBikes++;
        Serial.printf("🔵 BLE CONNECT: %s | Handle: %d | Total: %d\n",
                      addr.toString().c_str(), conn_handle, BLEServer::connectedBikes);
        NimBLEDevice::startAdvertising();

        BLEServer::connectedDevices[conn_handle] = "";
        Serial.printf("📝 Stored connection handle %d\n", conn_handle);
    }

    void onDisconnect(NimBLEServer *pServer, ble_gap_conn_desc *desc)
    {
        if (BLEServer::connectedBikes > 0)
            BLEServer::connectedBikes--;

        uint16_t conn_handle = desc->conn_handle;
        if (BLEServer::connectedDevices.find(conn_handle) != BLEServer::connectedDevices.end())
        {
            String bikeId = BLEServer::connectedDevices[conn_handle];
            BLEServer::connectedDevices.erase(conn_handle);
            Serial.printf("🔵 Bike %s disconnected (%d total)\n", bikeId.c_str(), BLEServer::connectedBikes);
        }
        else
        {
            Serial.printf("🔵 Device disconnected (%d total)\n", BLEServer::connectedBikes);
        }

        NimBLEDevice::startAdvertising();

        // Notificar bike_pairing sobre desconexão (só se conhece a bike)
        if (!bikeId.isEmpty())
        {
            BLEServer::onBikeDisconnected(bikeId);
        }
    }
};

class DataCallbacks : public NimBLECharacteristicCallbacks
{
    void onWrite(NimBLECharacteristic *pChar)
    {
        std::string value = pChar->getValue();
        if (value.length() > 0)
        {
            DynamicJsonDocument doc(512);
            DeserializationError error = deserializeJson(doc, value.c_str());

            if (!error && doc["bike_id"])
            {
                String bikeId = doc["bike_id"];
                
                // Encontrar handle desta conexão
                uint16_t conn_handle = 0;
                for (auto &pair : BLEServer::connectedDevices) {
                    if (pair.second.isEmpty()) {
                        conn_handle = pair.first;
                        break;
                    }
                }
                
                if (conn_handle != 0) {
                    // Armazenar bike_id para esta conexão específica
                    BLEServer::connectedDevices[conn_handle] = bikeId;
                    Serial.printf("📝 Bike %s mapped to handle %d\n", bikeId.c_str(), conn_handle);

                    // Verificar se tem config pendente e enviar imediatamente
                    BLEServer::checkAndSendPendingConfig(bikeId, conn_handle);
                } else {
                    Serial.printf("⚠️ Could not find handle for bike %s\n", bikeId.c_str());
                }

                // Delegar processamento para bike_pairing
                BLEServer::onBikeDataReceived(bikeId, String(value.c_str()));
            }
            else
            {
                Serial.printf("⚠️ Data without bike_id: %s\n", value.c_str());
            }
        }
    }
};

class ConfigCallbacks : public NimBLECharacteristicCallbacks
{
    void onWrite(NimBLECharacteristic *pChar)
    {
        std::string value = pChar->getValue();
        if (value.length() > 0)
        {
            DynamicJsonDocument doc(256);
            DeserializationError error = deserializeJson(doc, value.c_str());

            if (!error && doc["bike_id"])
            {
                String bikeId = doc["bike_id"];
                BLEServer::onConfigRequest(bikeId, String(value.c_str()));
            }
        }
    }
};

bool BLEServer::start()
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

void BLEServer::stop()
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

uint8_t BLEServer::getConnectedBikes()
{
    return connectedBikes;
}

bool BLEServer::isBikeConnected(const String &bikeId)
{
    for (auto &pair : connectedDevices) {
        if (pair.second == bikeId) {
            return true;
        }
    }
    return false;
}

void BLEServer::pushConfigToBike(const String &bikeId, const String &config)
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

void BLEServer::sendConfigToHandle(uint16_t handle, const String &bikeId, const String &config)
{
    if (!pConfigChar) return;
    
    // Incluir target no JSON para segurança extra
    DynamicJsonDocument wrapper(1024);
    wrapper["target_bike"] = bikeId;
    wrapper["timestamp"] = millis();
    
    // Parse config original
    DynamicJsonDocument originalConfig(512);
    if (deserializeJson(originalConfig, config) == DeserializationError::Ok) {
        wrapper["config"] = originalConfig;
    } else {
        wrapper["config"] = config;
    }
    
    String wrappedConfig;
    serializeJson(wrapper, wrappedConfig);
    
    pConfigChar->setValue(wrappedConfig.c_str());
    
    // Por enquanto usar broadcast com target (mais compatível)
    pConfigChar->notify();
    Serial.printf("📤 Config sent to %s (handle %d) with target filter\n", bikeId.c_str(), handle);
}

void BLEServer::checkAndSendPendingConfig(const String &bikeId, uint16_t handle)
{
    // Verificar se tem config pendente via bike_pairing
    if (BikeConfigManager::hasConfigUpdate(bikeId)) {
        String config = BikeConfigManager::getConfigForBike(bikeId);
        sendConfigToHandle(handle, bikeId, config);
        BikeConfigManager::markConfigSent(bikeId);
        
        Serial.printf("⚡ Immediate config sent to %s on connection\n", bikeId.c_str());
    } else {
        Serial.printf("📝 No pending config for %s\n", bikeId.c_str());
    }
}
