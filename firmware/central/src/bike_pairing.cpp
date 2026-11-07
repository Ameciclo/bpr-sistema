#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <queue>
#include <vector>
#include "bike_manager.h"
#include "bike_pairing.h"
#include "ble_server.h"
#include "bpr_json_helper.h"
#include "buffer_manager.h"
#include "config_manager.h"
#include "constants.h"

extern BufferManager bufferManager;
extern SystemState currentState;
extern uint32_t stateStartTime;
extern ConfigManager configManager;

// Estado do pairing com struct organizada
struct PairingState
{
    PairingStatus status;
    uint32_t lastActivity;
    String currentBike;
    uint32_t requestTimeout;

    PairingState() : status(PAIRING_IDLE), lastActivity(0), currentBike(""), requestTimeout(0) {}

    bool isTimedOut() const
    {
        extern ConfigManager configManager;
        return (status != PAIRING_IDLE) && (millis() - lastActivity) > configManager.getPairingBusyTimeout();
    }

    void updateActivity()
    {
        lastActivity = millis();
    }

    void reset()
    {
        status = PAIRING_IDLE;
        currentBike = "";
        requestTimeout = 0;
        updateActivity();
    }
};

static PairingState pairingState;

// Sistema de eventos
static BikeEventCallback eventCallback = nullptr;

// R19: callbacks BLE (task do NimBLE) apenas ENFILEIRAM eventos; todo o
// processamento — parsing, gestão de cadastro, persistência, pushes — roda
// no contexto do loop(). Fila limitada = backpressure explícito.
struct BleEvent {
    uint8_t type; // 0=connect 1=disconnect 2=data 3=config
    char bikeId[16];
    char payload[MAX_BLE_MESSAGE_SIZE + 1];
};
static QueueHandle_t eventQueue = nullptr;
static const uint8_t BLE_EVENT_CONNECT = 0;
static const uint8_t BLE_EVENT_DISCONNECT = 1;
static const uint8_t BLE_EVENT_DATA = 2;
static const uint8_t BLE_EVENT_CONFIG = 3;

static bool enqueueBleEvent(uint8_t type, const String& bikeId, const String& payload = "")
{
    if (!eventQueue) return false;
    if (payload.length() > MAX_BLE_MESSAGE_SIZE) return false;

    BleEvent ev;
    ev.type = type;
    strncpy(ev.bikeId, bikeId.c_str(), sizeof(ev.bikeId) - 1);
    ev.bikeId[sizeof(ev.bikeId) - 1] = '\0';
    strncpy(ev.payload, payload.c_str(), sizeof(ev.payload) - 1);
    ev.payload[sizeof(ev.payload) - 1] = '\0';

    if (xQueueSend(eventQueue, &ev, 0) != pdTRUE) {
        Serial.printf("⚠️ BLE event queue full - dropping %s event for %s\n",
                      type == BLE_EVENT_DATA ? "data" : "config", bikeId.c_str());
        return false;
    }
    return true;
}

// Sistema de fila com prioridade (consumida somente no loop — R19)
struct BikeQueueItem {
    String bikeId;
    String jsonData;
    uint8_t priority; // 1=alta (bateria baixa), 2=normal
    uint32_t timestamp;

    BikeQueueItem(const String& id, const String& data, uint8_t prio)
        : bikeId(id), jsonData(data), priority(prio), timestamp(millis()) {}
};

struct BikeQueueComparator {
    bool operator()(const BikeQueueItem& a, const BikeQueueItem& b) {
        if (a.priority != b.priority) {
            return a.priority > b.priority; // Menor número = maior prioridade
        }
        return a.timestamp > b.timestamp; // FIFO para mesma prioridade
    }
};

static std::priority_queue<BikeQueueItem, std::vector<BikeQueueItem>, BikeQueueComparator> priorityQueue;

// Callbacks BLE: fazem o mínimo e delegam ao loop via fila
static void onBikeConnected(const String &bikeId);
static void onBikeDisconnected(const String &bikeId);
static void onBikeDataReceived(const String &bikeId, const String &jsonData);
static void onConfigRequest(const String &bikeId, const String &request);

// Processadores reais (contexto do loop)
static void processBikeConnected(const String &bikeId);
static void processBikeDisconnected(const String &bikeId);
static void processBikeData(const String &bikeId, const String &jsonData);
static void processConfigRequest(const String &bikeId, const String &request);

void BikePairing::enter()
{
    Serial.println("🔵 Entering BIKE_PAIRING mode");

    if (!eventQueue) {
        eventQueue = xQueueCreate(16, sizeof(BleEvent));
    } else {
        xQueueReset(eventQueue);
    }

    // Initialize bike manager (carrega status + configs + merge do registry)
    BikeManager::init();

    pairingState.reset();

    // F12: start() é idempotente — não recria serviços
    if (!BPRBLEServer::start())
    {
        Serial.println("❌ Failed to start BLE Server");
        return;
    }

    // Register BLE callbacks
    BPRBLEServer::setDataCallback(onBikeDataReceived);
    BPRBLEServer::setConnectCallback(onBikeConnected);
    BPRBLEServer::setDisconnectCallback(onBikeDisconnected);
    BPRBLEServer::setConfigCallback(onConfigRequest);

    BPRBLEServer::setBusyStatus(false);
}

void BikePairing::update()
{
    uint32_t now = millis();
    static uint32_t lastAdvertisingUpdate = 0;

    if (now - lastAdvertisingUpdate > 5000) {
        BPRBLEServer::updateAdvertisingStatus();
        BPRBLEServer::checkConnectionTimeouts();
        lastAdvertisingUpdate = now;
    }

    // R19: drena os eventos BLE no contexto do loop
    if (eventQueue) {
        BleEvent ev;
        while (xQueueReceive(eventQueue, &ev, 0) == pdTRUE) {
            String bikeId(ev.bikeId);
            String payload(ev.payload);
            switch (ev.type) {
                case BLE_EVENT_CONNECT: processBikeConnected(bikeId); break;
                case BLE_EVENT_DISCONNECT: processBikeDisconnected(bikeId); break;
                case BLE_EVENT_DATA: processBikeData(bikeId, payload); break;
                case BLE_EVENT_CONFIG: processConfigRequest(bikeId, payload); break;
            }
        }
    }

    // F11: envia pushes enfileirados (ACKs/configs) no contexto do loop
    BPRBLEServer::processPushQueue();

    // Processar fila de dados sequencialmente
    processDataQueue();
}

void BikePairing::exit()
{
    while (!priorityQueue.empty())
    {
        priorityQueue.pop();
    }
    pairingState.reset();
    if (eventQueue) {
        xQueueReset(eventQueue);
    }

    // Marcar BLE como BUSY durante sync (bike respeita e mantém dados)
    BPRBLEServer::setBusyStatus(true, 120);

    Serial.println("🔚 Exiting BIKE_PAIRING mode - BLE marked as BUSY");
}

void BikePairing::printStatus()
{
    int bikes = BikePairing::getConnectedBikes();
    Serial.printf("🚲 Bikes conectadas: %d | 💾 Heap: %d bytes\n", bikes, ESP.getFreeHeap());

    uint32_t stateTime = millis() - stateStartTime;
    uint32_t syncInterval = configManager.getConfig().sync_interval_ms();
    uint32_t nextSync = (syncInterval - stateTime) / 1000;

    if (stateTime < syncInterval)
    {
        Serial.printf("🔄 Próxima sync em: %lus\n", nextSync);
        return;
    }
    Serial.println("🔄 Sync pendente...");
}

uint8_t BikePairing::getConnectedBikes()
{
    return BPRBLEServer::getConnectedBikes();
}

PairingStatus BikePairing::getStatus()
{
    if (pairingState.isTimedOut())
    {
        pairingState.status = PAIRING_IDLE;
    }
    return pairingState.status;
}

bool BikePairing::isSafeToExit()
{
    // Se buffer está cheio, forçar sync (prioridade máxima)
    if (bufferManager.isFull()) {
        Serial.println("🚨 Buffer full - forcing sync!");
        return true;
    }

    // Se está processando bike, não pode sair
    if (getStatus() != PAIRING_IDLE) {
        return false;
    }

    // Se há bikes com dados pendentes na fila, não pode sair
    if (!priorityQueue.empty()) {
        Serial.printf("📋 %d bikes in queue - not safe to exit\n", priorityQueue.size());
        return false;
    }

    uint8_t connectedBikes = BPRBLEServer::getConnectedBikes();
    if (connectedBikes > 0) {
        static uint32_t lastConnectedCheck = 0;
        uint32_t now = millis();

        if (now - lastConnectedCheck > 30000) {
            Serial.printf("🔍 %d bikes connected - waiting for data or timeout\n", connectedBikes);
            lastConnectedCheck = now;
        }

        uint32_t stateTime = now - stateStartTime;
        if (stateTime < 60000) {
            return false;
        }
    }

    return true;
}

// ---- Callbacks (task BLE): apenas enfileiram ----------------------------

static void onBikeConnected(const String &bikeId)
{
    enqueueBleEvent(BLE_EVENT_CONNECT, bikeId);
}

static void onBikeDisconnected(const String &bikeId)
{
    enqueueBleEvent(BLE_EVENT_DISCONNECT, bikeId);
}

static void onBikeDataReceived(const String &bikeId, const String &jsonData)
{
    enqueueBleEvent(BLE_EVENT_DATA, bikeId, jsonData);
}

static void onConfigRequest(const String &bikeId, const String &request)
{
    enqueueBleEvent(BLE_EVENT_CONFIG, bikeId, request);
}

// ---- Processadores (contexto do loop) ------------------------------------

static void processBikeConnected(const String &bikeId)
{
    BikePairing::triggerEvent(BIKE_ARRIVED, BPRBLEServer::getConnectedBikes());
    Serial.printf("🚲 Bike %s connected\n", bikeId.c_str());

    if (!BikeManager::canConnect(bikeId))
    {
        // F03: desconhecida vira pending no primeiro contato; blocked é desconectada
        if (!BikeManager::isKnownBike(bikeId)) {
            BikeManager::addPendingBike(bikeId);
            Serial.printf("📝 Bike %s registered as pending\n", bikeId.c_str());
        } else {
            Serial.printf("❌ Blocked bike %s - disconnecting\n", bikeId.c_str());
            BPRBLEServer::forceDisconnectBike(bikeId);
            return;
        }
    }

    // Se tem config pendente, enviar imediatamente
    if (BikeManager::hasConfigUpdate(bikeId))
    {
        pairingState.status = PAIRING_SENDING_CONFIG;
        pairingState.updateActivity();

        String config = BikeManager::getConfigForBike(bikeId);
        if (!config.isEmpty()) {
            BPRBLEServer::pushConfigToBike(bikeId, config);
            BikeManager::markConfigSent(bikeId);
            Serial.printf("⚙️ Config sent to %s on connection\n", bikeId.c_str());
        }
    }
}

static void processBikeDisconnected(const String &bikeId)
{
    BikePairing::triggerEvent(BIKE_LEFT, BPRBLEServer::getConnectedBikes());
    Serial.printf("🚲 Bike %s disconnected\n", bikeId.c_str());
}

static void processBikeData(const String &bikeId, const String &jsonData)
{
    // R19: admissão vinculada ao estado real da central, não só ao prazo de
    // busy — durante CLOUD_SYNC/CONFIG_AP a recepção é recusada
    if (currentState != STATE_BIKE_PAIRING)
    {
        Serial.printf("⚠️ Data rejected from %s - central not in PAIRING state\n", bikeId.c_str());
        String busyResponse = BPRJsonHelper::createBusyResponse(bikeId, 30);
        BPRBLEServer::pushConfigToBike(bikeId, busyResponse);
        return;
    }

    // Rejeitar dados se central está busy
    if (BPRBLEServer::isCentralBusy())
    {
        Serial.printf("⚠️ Data rejected from %s - Central is BUSY\n", bikeId.c_str());
        String busyResponse = BPRJsonHelper::createBusyResponse(bikeId, 30);
        BPRBLEServer::pushConfigToBike(bikeId, busyResponse);
        return;
    }

    if (!BikeManager::canConnect(bikeId))
    {
        Serial.printf("❌ Data rejected from non-allowed bike: %s\n", bikeId.c_str());
        BikeManager::recordPendingVisit(bikeId);
        return;
    }

    DynamicJsonDocument doc(BIKE_DATA_BUFFER);
    DeserializationError error = deserializeJson(doc, jsonData);

    if (error)
    {
        Serial.printf("❌ JSON parse error: %s\n", error.f_str());
        return;
    }

    String type = doc["type"] | "data";

    if (type == "heartbeat")
    {
        int batteryPercent = doc["battery_percent"] | 0;
        int heap = doc["heap"] | 0;
        uint32_t bikeConfigVersion = doc["config_version"] | 0;

        uint16_t sessions = 0;
        uint32_t bytes = 0;
        uint32_t oldestTs = 0;
        uint8_t bufferPercent = 0;

        if (doc.containsKey("pending_data"))
        {
            JsonObject pendingData = doc["pending_data"];
            sessions = pendingData["sessions_count"] | 0;
            bytes = pendingData["total_bytes"] | 0;
        }

        BikeManager::updateHeartbeat(bikeId, batteryPercent, heap, sessions, bytes, oldestTs, bufferPercent);

        // Verificar se precisa atualizar config (versão por bike)
        if (BikeManager::needsConfigUpdate(bikeId, bikeConfigVersion))
        {
            pairingState.status = PAIRING_SENDING_CONFIG;
            pairingState.updateActivity();

            String config = BikeManager::getConfigForBike(bikeId);
            if (!config.isEmpty()) {
                BPRBLEServer::pushConfigToBike(bikeId, config);
                BikeManager::markConfigSent(bikeId);
                Serial.printf("⚙️ Auto-config sent to %s (bike_version=%lu)\n",
                              bikeId.c_str(), (unsigned long)bikeConfigVersion);
            }
        }
        return;
    }

    // type == "data": fragmento de sessão
    if (pairingState.currentBike.isEmpty() || pairingState.currentBike == bikeId)
    {
        BikePairing::processDataFromBike(bikeId, jsonData);
    }
    else
    {
        uint8_t priority = BikeManager::isBatteryLow(bikeId) ? 1 : 2;
        BikePairing::enqueueBike(bikeId, jsonData, priority);
    }
}

static void processConfigRequest(const String &bikeId, const String &request)
{
    pairingState.status = PAIRING_SENDING_CONFIG;
    pairingState.updateActivity();

    DynamicJsonDocument doc(BLE_COMMAND_BUFFER);
    DeserializationError error = deserializeJson(doc, request);

    if (error)
    {
        Serial.printf("❌ Config request parse error: %s\n", error.f_str());
        pairingState.status = PAIRING_IDLE;
        return;
    }

    String type = doc["type"] | "config_request";

    // F06/R08: confirmação de aplicação pela bike
    if (type == "config_ack")
    {
        uint32_t appliedVersion = doc["applied_version"] | 0;
        // R08: compara CONTEÚDO da string, não endereços de literais
        String status = doc["status"] | "error";
        bool ok = (status == "ok");
        BikeManager::markConfigAcked(bikeId, ok ? appliedVersion : 0);
        pairingState.status = PAIRING_IDLE;
        return;
    }

    if (type == "config_request")
    {
        String config = BikeManager::getConfigForBike(bikeId);
        if (!config.isEmpty())
        {
            BPRBLEServer::pushConfigToBike(bikeId, config);
            BikeManager::markConfigSent(bikeId);
        }
        pairingState.status = PAIRING_IDLE;
    }
}

// Implementação dos métodos de processamento
void BikePairing::processDataQueue()
{
    uint32_t now = millis();

    if (!pairingState.currentBike.isEmpty() && (now - pairingState.requestTimeout) > BIKE_DATA_TIMEOUT_MS)
    {
        Serial.printf("⏰ Bike %s timeout - finishing\n", pairingState.currentBike.c_str());
        finishCurrentBike();
    }

    if (pairingState.currentBike.isEmpty() && !priorityQueue.empty())
    {
        BikeQueueItem nextItem = priorityQueue.top();
        priorityQueue.pop();

        Serial.printf("🚀 Processing next bike: %s (priority: %d)\n",
                     nextItem.bikeId.c_str(), nextItem.priority);

        processDataFromBike(nextItem.bikeId, nextItem.jsonData);
    }
}

void BikePairing::processDataFromBike(const String &bikeId, const String &jsonData)
{
    pairingState.currentBike = bikeId;
    pairingState.status = PAIRING_RECEIVING_DATA;
    pairingState.updateActivity();
    pairingState.requestTimeout = millis();

    // F16: addBikeData só retorna true com persistência íntegra (gravação verificada)
    if (bufferManager.addBikeData(bikeId, jsonData))
    {
        Serial.printf("💾 Fragment persisted from %s\n", bikeId.c_str());

        // F05: ACK vinculado a sessão/fragmento persistidos
        DynamicJsonDocument doc(BIKE_DATA_BUFFER);
        if (deserializeJson(doc, jsonData) == DeserializationError::Ok)
        {
            uint32_t sessionId = doc["session_id"] | 0;
            uint16_t salt = doc["session_salt"] | 0;
            uint16_t seq = doc["seq"] | 0;
            uint16_t total = doc["total"] | 0;
            bool isFinal = doc["is_final"] | false;

            bool canClear = isFinal && total > 0 &&
                            bufferManager.hasCompleteSession(bikeId, sessionId, salt, total);

            String ack = BPRJsonHelper::createDataAckResponse(
                bikeId, sessionId, salt, seq, isFinal, canClear);
            BPRBLEServer::pushConfigToBike(bikeId, ack);
        }
    }
    else
    {
        // Persistência falhou ou buffer cheio: bike mantém os dados e tenta depois
        Serial.printf("⚠️ Cannot store fragment from %s - asking to retry\n", bikeId.c_str());
        String busyResponse = BPRJsonHelper::createBusyResponse(bikeId, 30);
        BPRBLEServer::pushConfigToBike(bikeId, busyResponse);
    }
}

void BikePairing::enqueueBike(const String &bikeId, const String &jsonData, uint8_t priority)
{
    priorityQueue.emplace(bikeId, jsonData, priority);

    const char* priorityStr = (priority == 1) ? "HIGH (battery low)" : "NORMAL";
    Serial.printf("📋 Bike %s enqueued with %s priority (queue size: %d)\n",
                 bikeId.c_str(), priorityStr, priorityQueue.size());
}

void BikePairing::finishCurrentBike()
{
    if (!pairingState.currentBike.isEmpty())
    {
        Serial.printf("✅ Finished processing bike %s\n", pairingState.currentBike.c_str());
        pairingState.currentBike = "";
        pairingState.requestTimeout = 0;
        pairingState.status = PAIRING_IDLE;
    }
}

void BikePairing::setEventCallback(BikeEventCallback callback)
{
    eventCallback = callback;
}

void BikePairing::triggerEvent(BikeEvent event, uint8_t bikeCount)
{
    if (eventCallback)
    {
        eventCallback(event, bikeCount);
    }
}
