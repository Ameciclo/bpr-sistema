#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "bike_manager.h"
#include "ble_server.h"
#include "constants.h"

// F11: fila de pushes — NimBLE 1.4.3 só notifica TODOS os assinantes; a entrega
// direcionada é feita com envelope (bike_id) filtrado pela bike + pushes
// serializados exclusivamente no contexto do loop (sem interleave setValue/notify)
struct ConfigPushRequest {
    char bikeId[16];
    char payload[MAX_BLE_MESSAGE_SIZE + 8];
};

static QueueHandle_t pushQueue = nullptr;

// R19: callbacks BLE rodam na task do NimBLE; mapas de conexão compartilham
// com o loop — acesso serializado por mutex
static SemaphoreHandle_t bleStateMutex = nullptr;
static void bleStateLock() {
    if (bleStateMutex) xSemaphoreTake(bleStateMutex, portMAX_DELAY);
}
static void bleStateUnlock() {
    if (bleStateMutex) xSemaphoreGive(bleStateMutex);
}

// F10: sentinela real — zero É um handle válido
static const uint16_t INVALID_HANDLE = 0xFFFF;

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
public:
    // F12: instância única/reusada — não vaza a cada ciclo stop/start
    void onConnect(NimBLEServer *pServer, ble_gap_conn_desc *desc)
    {
        // F10: handle 0 é válido; mapas aceitam qualquer handle
        uint16_t conn_handle = desc->conn_handle;
        NimBLEAddress addr = NimBLEAddress(desc->peer_id_addr);

        bleStateLock();
        BPRBLEServer::connectedBikes++;
        uint8_t total = BPRBLEServer::connectedBikes;
        // Store as unidentified with 10s timeout
        BPRBLEServer::connectedDevices[conn_handle] = "";
        BPRBLEServer::connectionTimeouts[conn_handle] = millis() + 10000; // 10s timeout
        bleStateUnlock();

        Serial.printf("🔵 BLE CONNECT: %s | Handle: %d | Total: %d\n",
                      addr.toString().c_str(), conn_handle, total);
        NimBLEDevice::startAdvertising();
    }

    void onDisconnect(NimBLEServer *pServer, ble_gap_conn_desc *desc)
    {
        uint16_t conn_handle = desc->conn_handle;
        String bikeId = "";

        bleStateLock();
        if (BPRBLEServer::connectedBikes > 0)
            BPRBLEServer::connectedBikes--;
        uint8_t total = BPRBLEServer::connectedBikes;
        auto it = BPRBLEServer::connectedDevices.find(conn_handle);
        if (it != BPRBLEServer::connectedDevices.end())
        {
            bikeId = it->second;
            BPRBLEServer::connectedDevices.erase(it);
            BPRBLEServer::connectionTimeouts.erase(conn_handle);
        }
        bleStateUnlock();

        if (!bikeId.isEmpty())
        {
            Serial.printf("🔵 Bike %s disconnected (%d total)\n", bikeId.c_str(), total);
        }
        else
        {
            Serial.printf("🔵 Device disconnected (%d total)\n", total);
        }

        NimBLEDevice::startAdvertising();

        if (BPRBLEServer::disconnectCallback) {
            BPRBLEServer::disconnectCallback(bikeId);
        }
    }
};

class DataCallbacks : public NimBLECharacteristicCallbacks
{
public:
    // F10: overload com descritor da conexão — sabe QUEM escreveu
    void onWrite(NimBLECharacteristic *pChar, ble_gap_conn_desc *desc)
    {
        std::string value = pChar->getValue();
        if (value.length() == 0 || value.length() > MAX_BLE_MESSAGE_SIZE) {
            Serial.printf("⚠️ Invalid data length: %d\n", value.length());
            return;
        }

        uint16_t conn_handle = desc->conn_handle;

        DynamicJsonDocument doc(BIKE_DATA_BUFFER);
        DeserializationError error = deserializeJson(doc, value.c_str());

        if (!error && doc["bike_id"])
        {
            String bikeId = doc["bike_id"];

            if (!bikeId.startsWith("bpr-")) {
                Serial.printf("❌ Invalid device - not a BPR bike: %s\n", bikeId.c_str());
                return;
            }

            bool firstIdentification = false;
            bool identityConflict = false;
            bleStateLock();
            auto it = BPRBLEServer::connectedDevices.find(conn_handle);
            if (it == BPRBLEServer::connectedDevices.end())
            {
                // Escrita de conexão não registrada (ex.: após onConnect perdido)
                it = BPRBLEServer::connectedDevices.emplace(conn_handle, String("")).first;
                BPRBLEServer::connectionTimeouts[conn_handle] = millis() + 10000;
            }

            String previousId = it->second;
            if (previousId.isEmpty())
            {
                // R18: ID já reivindicado por OUTRA conexão é recusado
                bool idInUse = false;
                for (auto& pair : BPRBLEServer::connectedDevices) {
                    if (pair.first != conn_handle && pair.second == bikeId) {
                        idInUse = true;
                        break;
                    }
                }
                if (!idInUse)
                {
                    it->second = bikeId;
                    BPRBLEServer::connectionTimeouts.erase(conn_handle); // identificada: sem timeout
                    firstIdentification = true;
                }
                else
                {
                    identityConflict = true;
                }
            }
            else if (previousId != bikeId)
            {
                // R18: troca de identidade na MESMA conexão é recusada
                identityConflict = true;
            }
            bleStateUnlock();

            if (identityConflict)
            {
                Serial.printf("❌ Identity conflict on handle %d (claims %s) - rejected\n",
                              conn_handle, bikeId.c_str());
                return;
            }

            if (firstIdentification)
            {
                Serial.printf("📝 Bike %s identified on handle %d\n", bikeId.c_str(), conn_handle);
                // F10: evento de chegada apenas UMA vez por conexão
                if (BPRBLEServer::connectCallback) {
                    BPRBLEServer::connectCallback(bikeId);
                }
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
public:
    void onWrite(NimBLECharacteristic *pChar, ble_gap_conn_desc *desc)
    {
        std::string value = pChar->getValue();
        if (value.length() == 0 || value.length() > MAX_BLE_MESSAGE_SIZE) {
            Serial.printf("⚠️ Invalid config request length: %d\n", value.length());
            return;
        }

        uint16_t conn_handle = desc->conn_handle;

        DynamicJsonDocument doc(BLE_COMMAND_BUFFER);
        DeserializationError error = deserializeJson(doc, value.c_str());

        if (!error && doc["bike_id"])
        {
            String bikeId = doc["bike_id"];

            // R18: a configuração exige vínculo de identidade da CONEXÃO —
            // handle não identificado ou ID divergente é recusado
            String registeredId = "";
            bleStateLock();
            auto it = BPRBLEServer::connectedDevices.find(conn_handle);
            if (it != BPRBLEServer::connectedDevices.end()) {
                registeredId = it->second;
            }
            bleStateUnlock();

            if (registeredId.isEmpty())
            {
                Serial.printf("❌ Config write from unidentified handle %d - rejected\n", conn_handle);
                return;
            }
            if (registeredId != bikeId)
            {
                Serial.printf("❌ Config write identity mismatch on handle %d (%s != %s) - rejected\n",
                              conn_handle, bikeId.c_str(), registeredId.c_str());
                return;
            }
            // R18/X09: a AUTORIZAÇÃO (BikeManager/bikeStatus) não é mais
            // consultada aqui na task do NimBLE — o processConfigRequest no
            // loop aplica a mesma regra sem concorrer com quem altera o
            // cadastro (merge do registry, heartbeats)

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
    // F12: idempotente — não recria serviço/características a cada ciclo pairing/sync
    if (pServer)
    {
        Serial.println("🔵 BLE Server already running - ensuring advertising");
        NimBLEDevice::startAdvertising();
        return true;
    }

    Serial.println("🔵 Starting BLE Server");

    if (!bleStateMutex) {
        bleStateMutex = xSemaphoreCreateMutex();
    }

    NimBLEDevice::init(BLE_DEVICE_NAME);
    NimBLEDevice::setPower(ESP_PWR_LVL_P3);
    // R23: a flag de build (CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU) é sobrescrita
    // pelo sdkconfig do framework (256); o MTU preferido é definido em runtime
    NimBLEDevice::setMTU(512);

    pServer = NimBLEDevice::createServer();
    static ServerCallbacks serverCallbacksInstance;
    // R11: a 1.4.3 assume posse do callback por padrão e o destrói em
    // deinit(true) — delete de objeto estático é UB; propriedade desligada
    pServer->setCallbacks(&serverCallbacksInstance, false);

    if (!pushQueue) {
        pushQueue = xQueueCreate(8, sizeof(ConfigPushRequest));
    }

    pService = pServer->createService(BLE_SERVICE_UUID);

    // Data characteristic
    pDataChar = pService->createCharacteristic(
        BLE_CHAR_DATA_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
    static DataCallbacks dataCallbacksInstance;
    pDataChar->setCallbacks(&dataCallbacksInstance);

    // Config characteristic
    pConfigChar = pService->createCharacteristic(
        BLE_CHAR_CONFIG_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
    static ConfigCallbacks configCallbacksInstance;
    pConfigChar->setCallbacks(&configCallbacksInstance);

    pService->start();

    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    NimBLEDevice::startAdvertising();

    Serial.println("📡 BLE Server started successfully (preferred MTU 512)");
    return true;
}

void BPRBLEServer::stop()
{
    if (pServer)
    {
        if (pushQueue) {
            xQueueReset(pushQueue);
        }
        pServer->getAdvertising()->stop();
        // F12: cleanup COMPLETO — a 1.4.3 reusa o servidor interno se clearAll=false
        // e createService() duplicaria o UUID; deinit(true) libera servidor/serviços.
        // R11: callbacks estáticos NÃO são mais destruídos (setCallbacks(..., false))
        NimBLEDevice::deinit(true);
        pServer = nullptr;
        pService = nullptr;
        pDataChar = nullptr;
        pConfigChar = nullptr;
        bleStateLock();
        connectedBikes = 0;
        connectedDevices.clear();
        connectionTimeouts.clear();
        bleStateUnlock();
        isBusy = false;
        busyUntil = 0;
    }
    Serial.println("🔚 BLE Server stopped");
}

uint8_t BPRBLEServer::getConnectedBikes()
{
    bleStateLock();
    uint8_t count = connectedBikes;
    bleStateUnlock();
    return count;
}

uint16_t BPRBLEServer::getHandleForBike(const String& bikeId)
{
    bleStateLock();
    uint16_t handle = INVALID_HANDLE;
    for (auto& pair : connectedDevices) {
        if (pair.second == bikeId) {
            handle = pair.first;
            break;
        }
    }
    bleStateUnlock();
    return handle;
}

bool BPRBLEServer::isBikeConnected(const String& bikeId)
{
    return getHandleForBike(bikeId) != INVALID_HANDLE;
}

void BPRBLEServer::pushConfigToBike(const String& bikeId, const String& config)
{
    if (!pConfigChar) return;

    if (config.length() > MAX_BLE_MESSAGE_SIZE) {
        Serial.printf("⚠️ Payload too large for %s: %d bytes\n", bikeId.c_str(), config.length());
        return;
    }

    ConfigPushRequest req;
    strncpy(req.bikeId, bikeId.c_str(), sizeof(req.bikeId) - 1);
    req.bikeId[sizeof(req.bikeId) - 1] = '\0';
    strncpy(req.payload, config.c_str(), sizeof(req.payload) - 1);
    req.payload[sizeof(req.payload) - 1] = '\0';

    if (pushQueue && xQueueSend(pushQueue, &req, 0) != pdTRUE) {
        Serial.printf("⚠️ Push queue full - dropping message for %s\n", bikeId.c_str());
    }
}

void BPRBLEServer::processPushQueue()
{
    if (!pConfigChar || !pushQueue) return;

    ConfigPushRequest req;
    while (xQueueReceive(pushQueue, &req, 0) == pdTRUE)
    {
        String bikeId = String(req.bikeId);
        String payload = String(req.payload);

        uint16_t handle = getHandleForBike(bikeId);
        if (handle == INVALID_HANDLE)
        {
            // bike saiu entre o pedido e o envio: dados/ACK serão re-pedidos pela bike
            Serial.printf("⚠️ %s no longer connected - dropping push\n", bikeId.c_str());
            continue;
        }

        sendConfigToHandle(handle, bikeId, payload);
    }
}

void BPRBLEServer::sendConfigToHandle(uint16_t handle, const String& bikeId, const String& config)
{
    if (!pConfigChar || handle == INVALID_HANDLE) {
        Serial.printf("❌ Invalid config char or handle for %s\n", bikeId.c_str());
        return;
    }

    // Validate config size
    if (config.length() > MAX_BLE_MESSAGE_SIZE) {
        Serial.printf("⚠️ Config too large for %s: %d bytes\n", bikeId.c_str(), config.length());
        return;
    }

    // R23: o envelope só cabe na notificação se respeitar o MTU NEGOCIADO
    // desta conexão (valor útil = MTU - 3 bytes de overhead ATT)
    uint16_t peerMtu = pServer->getPeerMTU(handle);
    size_t maxNotifyPayload = (peerMtu > 3) ? (size_t)(peerMtu - 3) : 20;
    if (config.length() > maxNotifyPayload)
    {
        // Não há fragmentação central→bike: recusa explícita em vez de
        // notificação truncada anunciada como sucesso
        Serial.printf("❌ Config for %s is %d bytes but negotiated MTU %d allows %d - NOT sent\n",
                      bikeId.c_str(), config.length(), peerMtu, (int)maxNotifyPayload);
        return;
    }

    // F11: notificação da 1.4.3 alcança todos os assinantes; o envelope bike_id
    // permite à bike certa aplicar e às outras rejeitar
    pConfigChar->setValue(config.c_str());
    pConfigChar->notify(true);

    Serial.printf("📤 Config sent to %s (handle %d): %d bytes (MTU %d)\n",
                  bikeId.c_str(), handle, config.length(), peerMtu);
}

void BPRBLEServer::checkAndSendPendingConfig(const String& bikeId, uint16_t handle)
{
    if (BikeManager::hasConfigUpdate(bikeId)) {
        String config = BikeManager::getConfigForBike(bikeId);
        sendConfigToHandle(handle, bikeId, config);
        BikeManager::markConfigSent(bikeId);

        Serial.printf("⚡ Immediate config sent to %s on connection\n", bikeId.c_str());
    } else {
        Serial.printf("📝 No pending config for %s\n", bikeId.c_str());
    }
}

void BPRBLEServer::forceDisconnectBike(const String& bikeId)
{
    if (!pServer) return;

    uint16_t targetHandle = getHandleForBike(bikeId);
    if (targetHandle != INVALID_HANDLE) {
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

    printBLEInfo();
}

void BPRBLEServer::updateAdvertisingStatus() {
    if (!pServer) return;

    // Check if busy period expired (but not if permanent)
    if (isBusy && busyUntil != UINT32_MAX && millis() > busyUntil) {
        isBusy = false;
        Serial.println("🔵 BLE BUSY period expired - back to READY");
    }

    static bool lastBusyState = false;
    static uint32_t lastBusyUntil = 0;

    if (isBusy == lastBusyState && busyUntil == lastBusyUntil) {
        return;
    }

    lastBusyState = isBusy;
    lastBusyUntil = busyUntil;

    // Update device name based on status
    String deviceName;
    if (busyUntil == UINT32_MAX) {
        deviceName = "BPR Central OFF";
    } else if (isBusy) {
        deviceName = "BPR Central BUSY";
    } else {
        deviceName = "BPR Central";
    }

    NimBLEAdvertising *pAdvertising = pServer->getAdvertising();
    pAdvertising->stop();
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
        setBusyStatus(true, 0);
    }
}

void BPRBLEServer::checkConnectionTimeouts() {
    if (!pServer) return;

    uint32_t now = millis();
    std::vector<uint16_t> toDisconnect;

    bleStateLock();
    for (auto& pair : connectionTimeouts) {
        if (now > pair.second) {
            toDisconnect.push_back(pair.first);
        }
    }
    bleStateUnlock();

    for (uint16_t handle : toDisconnect) {
        Serial.printf("⏰ Disconnecting unidentified device (handle %d) - timeout\n", handle);
        pServer->disconnect(handle);
    }
}
