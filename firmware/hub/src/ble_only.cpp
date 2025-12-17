#include "ble_only.h"
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include "constants.h"
#include "config_manager.h"
#include "buffer_manager.h"
#include "led_controller.h"
#include "state_machine.h"
// #include "bike_config.h" - removido, conflito com bike_config_manager
#include "bike_registry.h"
#include "bike_config_manager.h"

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
static std::map<uint16_t, String> connectedDevices;
static bool filteringEnabled = false;

// Filtro BLE removido - hub é servidor, não cliente

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) {
        // Obter endereço do dispositivo conectado
        uint16_t conn_handle = desc->conn_handle;
        NimBLEAddress addr = NimBLEAddress(desc->peer_id_addr);
        
        connectedBikes++;
        Serial.printf("🔵 BLE CONNECT: %s | Handle: %d | Total: %d\n", 
                     addr.toString().c_str(), conn_handle, connectedBikes);
        ledController.bikeArrivedPattern();
        NimBLEDevice::startAdvertising();
        
        // Armazenar handle para push de config posterior
        connectedDevices[conn_handle] = "";
        Serial.printf("📝 Stored connection handle %d\n", conn_handle);
    }
    
    void onDisconnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) {
        if (connectedBikes > 0) connectedBikes--;
        
        // Remover do mapa de conexões
        uint16_t conn_handle = desc->conn_handle;
        if (connectedDevices.find(conn_handle) != connectedDevices.end()) {
            String bikeId = connectedDevices[conn_handle];
            connectedDevices.erase(conn_handle);
            Serial.printf("🔵 Bike %s disconnected (%d total)\n", bikeId.c_str(), connectedBikes);
        } else {
            Serial.printf("🔵 Device disconnected (%d total)\n", connectedBikes);
        }
        
        ledController.bikeLeftPattern();
        NimBLEDevice::startAdvertising();
    }
};

class DataCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar) {
        std::string value = pChar->getValue();
        if (value.length() > 0) {
            // Tentar extrair bike_id dos dados JSON
            DynamicJsonDocument doc(512);
            DeserializationError error = deserializeJson(doc, value.c_str());
            
            if (!error && doc["bike_id"]) {
                String bikeId = doc["bike_id"];
                Serial.printf("🔍 Processing data from bike: %s\n", bikeId.c_str());
                
                // Validar se bike pode se conectar (não blocked)
                if (BikeRegistry::canConnect(bikeId)) {
                    Serial.printf("✅ Bike %s can connect\n", bikeId.c_str());
                    // Validar se pode enviar dados (só allowed)
                    if (BikeRegistry::isAllowed(bikeId)) {
                        Serial.printf("📥 Data from %s: %s\n", bikeId.c_str(), value.c_str());
                        
                        // Atualizar heartbeat se tiver dados de bateria
                        if (doc["battery"] && doc["heap"]) {
                            BikeRegistry::updateHeartbeat(bikeId, doc["battery"], doc["heap"]);
                        }
                        
                        // Adicionar timestamp de recebimento aos dados
                        time_t now = time(nullptr);
                        struct tm timeinfo;
                        getLocalTime(&timeinfo);
                        
                        char dateStr[64];
                        strftime(dateStr, sizeof(dateStr), "%Y-%m-%d %H:%M:%S UTC-3", &timeinfo);
                        
                        doc["hub_receive_timestamp"] = now;
                        doc["hub_receive_timestamp_human"] = dateStr;
                        
                        // Serializar JSON modificado
                        String modifiedJson;
                        serializeJson(doc, modifiedJson);
                        
                        bufferManager.addData((uint8_t*)modifiedJson.c_str(), modifiedJson.length());
                        
                        // TODO: Verificar se tem config nova para enviar
                        // if (BikeConfigManager::hasConfigUpdate(bikeId)) {
                        //     BikeConfigManager::pushConfigToBike(bikeId, pConfigChar);
                        // }
                        
                        // Armazenar bike_id para esta conexão
                        for (auto& pair : connectedDevices) {
                            if (pair.second.isEmpty()) {
                                pair.second = bikeId;
                                break;
                            }
                        }
                    } else {
                        // Bike pending - só registrar visita, não processar dados
                        BikeRegistry::recordPendingVisit(bikeId);
                        Serial.printf("📝 Pending bike %s visited - data ignored (awaiting approval)\n", bikeId.c_str());
                    }
                } else {
                    Serial.printf("❌ Data rejected from blocked bike: %s\n", bikeId.c_str());
                }
            } else {
                Serial.printf("⚠️ Data without bike_id: %s\n", value.c_str());
            }
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
            // TODO: Implementar resposta de config
            Serial.printf("📝 Config request from %s (not implemented)\n", bikeId.c_str());
        } else if (type == "config_received") {
            String status = doc["status"] | "";
            Serial.printf("📋 Config confirmation from %s: %s\n", 
                         bikeId.c_str(), status.c_str());
        }
    }
};

void BLEOnly::enter() {
    Serial.println("🔵 Entering BLE_ONLY mode");
    
    // Initialize bike registry and config manager
    BikeRegistry::init();
    BikeConfigManager::init();
    Serial.println("🔍 BLE server mode - accepting all connections");
    
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
    Serial.printf("💓 Heartbeat: %d bikes connected | Devices map size: %d\n", 
                 connectedBikes, connectedDevices.size());
}