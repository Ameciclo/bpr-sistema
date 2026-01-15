#include <ArduinoJson.h>
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

// Sistema de fila com prioridade
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

// Forward declarations dos callbacks
static void onBikeConnected(const String &bikeId);
static void onBikeDisconnected(const String &bikeId);
static void onBikeDataReceived(const String &bikeId, const String &jsonData);
static void onConfigRequest(const String &bikeId, const String &request);

void BikePairing::enter()
{
    Serial.println("🔵 Entering BIKE_PAIRING mode");

    // Initialize bike manager
    BikeManager::init();

    // Reset status to idle
    pairingState.reset();

    // Start BLE server and register callbacks
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

    // Set BLE to ready status
    BPRBLEServer::setBusyStatus(false);
}

void BikePairing::update()
{
    uint32_t now = millis();
    static uint32_t lastAdvertisingUpdate = 0;

    // Atualizar status do BLE apenas a cada 5 segundos para evitar spam
    if (now - lastAdvertisingUpdate > 5000) {
        BPRBLEServer::updateAdvertisingStatus();
        BPRBLEServer::checkConnectionTimeouts(); // Check for unidentified device timeouts
        lastAdvertisingUpdate = now;
    }

    // Processar fila de dados sequencialmente
    processDataQueue();
}

void BikePairing::exit()
{
    // Limpar fila ao sair
    while (!priorityQueue.empty())
    {
        priorityQueue.pop();
    }
    pairingState.reset();
    
    // Marcar BLE como BUSY durante sync
    BPRBLEServer::setBusyStatus(true, 60); // BUSY por 60 segundos (tempo típico de sync)
    
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
    
    // Verificação simples: se há bikes conectadas, aguardar um pouco mais
    uint8_t connectedBikes = BPRBLEServer::getConnectedBikes();
    if (connectedBikes > 0) {
        static uint32_t lastConnectedCheck = 0;
        uint32_t now = millis();
        
        // Log apenas a cada 30 segundos para evitar spam
        if (now - lastConnectedCheck > 30000) {
            Serial.printf("🔍 %d bikes connected - waiting for data or timeout\n", connectedBikes);
            lastConnectedCheck = now;
        }
        
        // Aguardar até 60 segundos por dados das bikes conectadas
        uint32_t stateTime = now - stateStartTime;
        if (stateTime < 60000) {
            return false; // Não é seguro ainda
        }
    }
    
    return true; // Seguro para sair
}

// Callbacks para BLE Server (registrados via setters)
static void onBikeConnected(const String &bikeId)
{
    BikePairing::triggerEvent(BIKE_ARRIVED, BPRBLEServer::getConnectedBikes());
    Serial.printf("🚲 Bike %s connected\n", bikeId.c_str());

    // Verificar se bike pode conectar (não blocked)
    if (!BikeManager::canConnect(bikeId))
    {
        Serial.printf("❌ Blocked bike %s - disconnecting\n", bikeId.c_str());
        BPRBLEServer::forceDisconnectBike(bikeId);
        return;
    }

    // Se tem config pendente, enviar imediatamente
    if (BikeManager::hasConfigUpdate(bikeId))
    {
        pairingState.status = PAIRING_SENDING_CONFIG;
        pairingState.updateActivity();

        String config = BikeManager::getConfigForBike(bikeId);
        BPRBLEServer::pushConfigToBike(bikeId, config);
        BikeManager::markConfigSent(bikeId);

        Serial.printf("⚙️ Config sent to %s on connection\n", bikeId.c_str());
    }
}

static void onBikeDisconnected(const String &bikeId)
{
    BikePairing::triggerEvent(BIKE_LEFT, BPRBLEServer::getConnectedBikes());
    Serial.printf("🚲 Bike %s disconnected\n", bikeId.c_str());
}

static void onBikeDataReceived(const String &bikeId, const String &jsonData)
{
    // Rejeitar dados se central está busy
    if (BPRBLEServer::isCentralBusy())
    {
        Serial.printf("⚠️ Data rejected from %s - Central is BUSY\n", bikeId.c_str());

        String busyResponse = BPRJsonHelper::createBusyResponse(30);
        BPRBLEServer::pushConfigToBike(bikeId, busyResponse);
        return;
    }

    if (!BikeManager::canConnect(bikeId))
    {
        Serial.printf("❌ Data rejected from blocked bike: %s\n", bikeId.c_str());
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
        if (BikeManager::isAllowed(bikeId))
        {
            int batteryPercent = doc["battery_percent"] | 0;
            int heap = doc["heap"] | 0;
            uint32_t bikeLastUpdate = doc["last_update"] | 0;

            // Extrair pending_data se presente
            uint16_t sessions = 0;
            uint32_t bytes = 0;
            uint32_t oldestTs = 0;
            uint8_t bufferPercent = 0;

            if (doc.containsKey("pending_data"))
            {
                JsonObject pendingData = doc["pending_data"];
                sessions = pendingData["sessions_count"] | 0;
                bytes = pendingData["total_bytes"] | 0;
                oldestTs = pendingData["oldest_session_ts"] | 0;
                bufferPercent = pendingData["buffer_usage_percent"] | 0;

                // Log detalhado para análise
                Serial.printf("📊 Pending data from %s: %d sessions, %d bytes (%.1fKB), buffer %d%%\n",
                              bikeId.c_str(), sessions, bytes, bytes / 1024.0, bufferPercent);
            }

            BikeManager::updateHeartbeat(bikeId, batteryPercent, heap, sessions, bytes, oldestTs, bufferPercent);

            // Verificar se precisa atualizar config
            if (BikeManager::needsConfigUpdate(bikeId, bikeLastUpdate))
            {
                pairingState.status = PAIRING_SENDING_CONFIG;
                pairingState.updateActivity();

                String config = BikeManager::getConfigForBike(bikeId);
                BPRBLEServer::pushConfigToBike(bikeId, config);
                BikeManager::markConfigSent(bikeId);

                Serial.printf("⚙️ Auto-config sent to %s (bike_update=%lu)\n", bikeId.c_str(), bikeLastUpdate);
            }
        }
        else
        {
            BikeManager::recordPendingVisit(bikeId);
        }
        return;
    }

    if (!BikeManager::isAllowed(bikeId))
    {
        BikeManager::recordPendingVisit(bikeId);
        return;
    }

    // Sistema de fila com prioridade
    if (pairingState.currentBike.isEmpty())
    {
        BikePairing::processDataFromBike(bikeId, jsonData);
    }
    else if (pairingState.currentBike == bikeId)
    {
        BikePairing::processDataFromBike(bikeId, jsonData);
    }
    else
    {
        // Determinar prioridade baseada na bateria
        uint8_t priority = BikeManager::isBatteryLow(bikeId) ? 1 : 2; // 1=alta, 2=normal
        BikePairing::enqueueBike(bikeId, jsonData, priority);
    }
}

static void onConfigRequest(const String &bikeId, const String &request)
{
    pairingState.status = PAIRING_SENDING_CONFIG;
    pairingState.updateActivity();

    DynamicJsonDocument doc(BLE_COMMAND_BUFFER);
    DeserializationError error = deserializeJson(doc, request);

    if (error)
    {
        Serial.printf("❌ Config request parse error: %s\n", error.f_str());
        return;
    }

    // Processar request de configuração
    String config = BikeManager::getConfigForBike(bikeId);
    if (!config.isEmpty())
    {
        BPRBLEServer::pushConfigToBike(bikeId, config);
        BikeManager::markConfigSent(bikeId);
    }
}

// Implementação dos métodos de processamento
void BikePairing::processDataQueue()
{
    uint32_t now = millis();

    // Verificar timeout da bike atual
    if (!pairingState.currentBike.isEmpty() && (now - pairingState.requestTimeout) > BIKE_DATA_TIMEOUT_MS)
    {
        Serial.printf("⏰ Bike %s timeout - finishing\n", pairingState.currentBike.c_str());
        finishCurrentBike();
    }

    // Processar próxima bike da fila se não há bike atual
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

    // Processar dados via buffer manager
    if (bufferManager.addBikeData(bikeId, jsonData))
    {
        Serial.printf("💾 Data processed from %s\n", bikeId.c_str());

        // Usar método do BikeManager para confirmação
        String confirmation = BikeManager::confirmDataUpload(bikeId);
        BPRBLEServer::pushConfigToBike(bikeId, confirmation);
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