#include "bike_pairing.h"
#include <ArduinoJson.h>
#include <queue>
#include "constants.h"
#include "buffer_manager.h"
#include "led_controller.h"
#include "bike_manager.h"
#include "ble_server.h"
#include "config_manager.h"

extern BufferManager bufferManager;
extern LEDController ledController;
extern SystemState currentState;
extern uint32_t stateStartTime;
extern ConfigManager configManager;

static uint32_t lastHeartbeat = 0;
static PairingStatus currentStatus = PAIRING_IDLE;
static uint32_t lastActivity = 0;
static uint32_t busyTimeout = 10000; // 10 segundos para considerar idle

// Sistema de fila sequencial
static std::queue<String> dataQueue;
static String currentBike = "";
static uint32_t requestTimeout = 0;
static const uint32_t BIKE_TIMEOUT_MS = 30000; // 30s timeout por bike

void BikePairing::enter()
{
    Serial.println("🔵 Entering BIKE_PAIRING mode");

    // Initialize bike manager
    BikeManager::init();
    
    // Reset status to idle
    currentStatus = PAIRING_IDLE;
    lastActivity = millis();
    
    // Start BLE server
    if (!BPRBLEServer::start()) {
        Serial.println("❌ Failed to start BLE Server");
        return;
    }
    
    // Set BLE to ready status
    BPRBLEServer::setBusyStatus(false);
    }

void BikePairing::update()
{
    uint32_t now = millis();

    // Atualizar status do BLE (verificar se busy expirou)
    BPRBLEServer::updateAdvertisingStatus();

    // Processar fila de dados sequencialmente
    processDataQueue();

    // Heartbeat local (só para debug de bikes conectadas)
    if (now - lastHeartbeat > HEARTBEAT_INTERVAL)
    {
        lastHeartbeat = now;
        sendHeartbeat();
    }

    // Update LED status (feedback visual de bikes conectadas)
    static uint32_t lastLedUpdate = 0;
    uint32_t ledInterval = 30000; // 30 segundos fixo
    if (now - lastLedUpdate > ledInterval)
    {
        lastLedUpdate = now;
        ledController.countPattern(BPRBLEServer::getConnectedBikes());
    }
}

void BikePairing::exit()
{
    // Limpar fila ao sair
    while (!dataQueue.empty()) {
        dataQueue.pop();
    }
    currentBike = "";
    requestTimeout = 0;
    
    // NÃO parar BLE - apenas marcar como busy
    BPRBLEServer::setBusyStatus(true, 60); // 60s busy para sync
    
    currentStatus = PAIRING_IDLE;
    Serial.println("🔚 Exiting BIKE_PAIRING mode - BLE marked as BUSY");
}

void BikePairing::printStatus()
{
    int bikes = BikePairing::getConnectedBikes();
    Serial.printf("😲 Bikes conectadas: %d | 💾 Heap: %d bytes\n", bikes, ESP.getFreeHeap());

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

void BikePairing::sendHeartbeat()
{
    DynamicJsonDocument heartbeat(JSON_MEDIUM_BUFFER);
    
    // Dados da central
    heartbeat["timestamp"] = millis() / 1000;
    heartbeat["uptime_sec"] = millis() / 1000;
    heartbeat["heap_free"] = ESP.getFreeHeap();
    
    // Pedir ao bike manager para popular dados das bikes
    JsonArray bikes = heartbeat.createNestedArray("bikes");
    BikeManager::populateHeartbeatData(bikes);
    
    // Estatísticas calculadas
    heartbeat["total_bikes"] = bikes.size();
    heartbeat["bikes_connected_now"] = BPRBLEServer::getConnectedBikes();
    heartbeat["bikes_allowed"] = BikeManager::getAllowedCount();
    heartbeat["bikes_pending"] = BikeManager::getPendingCount();
    heartbeat["bikes_with_recent_contact"] = BikeManager::getConnectedCount();
    
    // Salvar no LittleFS
    bufferManager.addBikeData("heartbeat", heartbeat.as<String>());
    
    Serial.printf("💓 Heartbeat: %d total, %d allowed, %d pending, %d recent\n", 
                  (int)bikes.size(), BikeManager::getAllowedCount(), 
                  BikeManager::getPendingCount(), BikeManager::getConnectedCount());
}

PairingStatus BikePairing::getStatus()
{
    // Se passou do timeout, voltar para IDLE
    if (currentStatus != PAIRING_IDLE && (millis() - lastActivity) > busyTimeout) {
        currentStatus = PAIRING_IDLE;
    }
    return currentStatus;
}

bool BikePairing::isSafeToExit()
{
    // Seguro sair se:
    // 1. Status é IDLE
    // 2. OU se passou muito tempo sem atividade (timeout)
    return (getStatus() == PAIRING_IDLE) || (millis() - lastActivity) > busyTimeout;
}

// Implementação dos callbacks do BLE Server
void BPRBLEServer::onBikeConnected(const String& bikeId) {
    ledController.bikeArrivedPattern();
    Serial.printf("🚲 Bike %s connected\n", bikeId.c_str());
    
    // Verificar se bike pode conectar (não blocked)
    if (!BikeManager::canConnect(bikeId)) {
        Serial.printf("❌ Blocked bike %s - disconnecting\n", bikeId.c_str());
        BPRBLEServer::forceDisconnectBike(bikeId);
        return;
    }
    
    // Se tem config pendente, enviar imediatamente
    if (BikeManager::hasConfigUpdate(bikeId)) {
        currentStatus = PAIRING_SENDING_CONFIG;
        lastActivity = millis();
        
        String config = BikeManager::getConfigForBike(bikeId);
        BPRBLEServer::pushConfigToBike(bikeId, config);
        BikeManager::markConfigSent(bikeId);
        
        Serial.printf("⚙️ Config sent to %s on connection\n", bikeId.c_str());
    } else {
        Serial.printf("📝 No config update for %s - skipping\n", bikeId.c_str());
    }
}

void BPRBLEServer::onBikeDisconnected(const String& bikeId) {
    ledController.bikeLeftPattern();
    if (!bikeId.isEmpty()) {
        Serial.printf("🚲 Bike %s disconnected - LED pattern triggered\n", bikeId.c_str());
    } else {
        Serial.printf("🚲 Device disconnected - LED pattern triggered\n");
    }
}

void BPRBLEServer::onBikeDataReceived(const String& bikeId, const String& jsonData) {
    // Rejeitar dados se central está busy
    if (BPRBLEServer::isCentralBusy()) {
        Serial.printf("⚠️ Data rejected from %s - Central is BUSY\n", bikeId.c_str());
        
        // Enviar resposta informando que está busy
        DynamicJsonDocument busyResponse(JSON_SMALL_BUFFER);
        busyResponse["type"] = "busy";
        busyResponse["message"] = "Central busy - try again later";
        busyResponse["retry_after_sec"] = 30;
        
        String response;
        serializeJson(busyResponse, response);
        BPRBLEServer::pushConfigToBike(bikeId, response);
        return;
    }
    
    // Validações rápidas primeiro
    if (!BikeManager::canConnect(bikeId)) {
        Serial.printf("❌ Data rejected from blocked bike: %s\n", bikeId.c_str());
        return;
    }
    
    // Parse JSON para verificar tipo
    DynamicJsonDocument doc(JSON_MEDIUM_BUFFER);
    DeserializationError error = deserializeJson(doc, jsonData);
    
    if (error) {
        Serial.printf("❌ JSON parse error: %s\n", error.c_str());
        return;
    }
    
    String type = doc["type"] | "data";
    
    if (type == "heartbeat") {
        // Processar heartbeat rapidamente
        if (BikeManager::isAllowed(bikeId)) {
            int batteryPercent = doc["battery_percent"] | 0;
            int heap = doc["heap"] | 0;
            BikeManager::updateHeartbeat(bikeId, batteryPercent, heap);
            
            // Resposta rápida de heartbeat
            String response = BikeManager::processHeartbeat(bikeId, doc.as<JsonObject>());
            if (!response.isEmpty()) {
                BPRBLEServer::pushConfigToBike(bikeId, response);
            }
        } else {
            BikeManager::recordPendingVisit(bikeId);
            Serial.printf("📝 Pending bike %s heartbeat - awaiting approval\n", bikeId.c_str());
        }
        return;
    }
    
    // Processar dados normais (upload de scans WiFi)
    if (!BikeManager::isAllowed(bikeId)) {
        BikeManager::recordPendingVisit(bikeId);
        Serial.printf("📝 Pending bike %s visited - data ignored (awaiting approval)\n", bikeId.c_str());
        return;
    }
    
    // Sistema de fila sequencial para uploads de dados
    if (currentBike.isEmpty()) {
        // Nenhuma bike processando - processar imediatamente
        BikePairing::processDataFromBike(bikeId, jsonData);
    } else if (currentBike == bikeId) {
        // Bike atual enviando mais dados - processar
        BikePairing::processDataFromBike(bikeId, jsonData);
    } else {
        // Outra bike tentando enviar - enfileirar
        Serial.printf("📋 Bike %s enqueued (current: %s)\n", bikeId.c_str(), currentBike.c_str());
        BikePairing::enqueueBike(bikeId, jsonData);
    }
}

void BPRBLEServer::onConfigRequest(const String& bikeId, const String& request) {
    // Marcar atividade de configuração
    currentStatus = PAIRING_SENDING_CONFIG;
    lastActivity = millis();
    
    DynamicJsonDocument doc(JSON_SMALL_BUFFER);
    DeserializationError error = deserializeJson(doc, request);
    
    if (error) {
        Serial.printf("❌ Config request parse error: %s\n", error.c_str());
        currentStatus = PAIRING_IDLE;
        return;
    }
    
    String type = doc["type"] | "";
    
    if (type == "config_request") {
        Serial.printf("📝 Config request from %s\n", bikeId.c_str());
        
        if (BikeManager::hasConfigUpdate(bikeId)) {
            String config = BikeManager::getConfigForBike(bikeId);
            BPRBLEServer::pushConfigToBike(bikeId, config);
            BikeManager::markConfigSent(bikeId);
            Serial.printf("⚙️ Config sent to %s\n", bikeId.c_str());
        } else {
            Serial.printf("📝 No config update for %s\n", bikeId.c_str());
            currentStatus = PAIRING_IDLE; // Sem config = idle
        }
    } else if (type == "config_received") {
        String status = doc["status"] | "";
        Serial.printf("📋 Config confirmation from %s: %s\n", bikeId.c_str(), status.c_str());
        currentStatus = PAIRING_IDLE; // Confirmação recebida = idle
    }
}

// Funções auxiliares removidas - dados vêm do BikeRegistry::populateHeartbeatData()

// Funções da fila sequencial
void BikePairing::processDataQueue() {
    uint32_t now = millis();
    
    // Verificar timeout da bike atual
    if (!currentBike.isEmpty() && (now - requestTimeout) > BIKE_TIMEOUT_MS) {
        Serial.printf("⏰ Timeout for bike %s - processing next\n", currentBike.c_str());
        finishCurrentBike();
    }
    
    // Se não há bike atual e há fila, processar próxima
    if (currentBike.isEmpty() && !dataQueue.empty()) {
        String nextBike = dataQueue.front();
        dataQueue.pop();
        
        Serial.printf("🎯 Processing next bike from queue: %s\n", nextBike.c_str());
        requestDataFromBike(nextBike);
    }
}

void BikePairing::requestDataFromBike(const String& bikeId) {
    currentBike = bikeId;
    requestTimeout = millis();
    currentStatus = PAIRING_RECEIVING_DATA;
    lastActivity = millis();
    
    // Enviar comando para bike enviar dados
    DynamicJsonDocument cmd(JSON_SMALL_BUFFER);
    cmd["type"] = "data_request";
    cmd["bike_id"] = bikeId;
    
    String cmdStr;
    serializeJson(cmd, cmdStr);
    BPRBLEServer::pushConfigToBike(bikeId, cmdStr);
    
    Serial.printf("📤 Data request sent to %s\n", bikeId.c_str());
}

void BikePairing::processDataFromBike(const String& bikeId, const String& jsonData) {
    currentBike = bikeId;
    requestTimeout = millis();
    currentStatus = PAIRING_RECEIVING_DATA;
    lastActivity = millis();
    
    Serial.printf("📥 Processing data from %s\n", bikeId.c_str());
    
    // Parse para atualizar heartbeat
    DynamicJsonDocument doc(JSON_SMALL_BUFFER);
    if (deserializeJson(doc, jsonData) == DeserializationError::Ok) {
        if (doc["battery"] && doc["heap"]) {
            BikeManager::updateHeartbeat(bikeId, doc["battery"], doc["heap"]);
        }
    }
    
    // Processar dados via BufferManager
    bufferManager.addBikeData(bikeId, jsonData);
    
    // Verificar se tem config nova para enviar
    if (BikeManager::hasConfigUpdate(bikeId)) {
        currentStatus = PAIRING_SENDING_CONFIG;
        
        String config = BikeManager::getConfigForBike(bikeId);
        BPRBLEServer::pushConfigToBike(bikeId, config);
        BikeManager::markConfigSent(bikeId);
        
        Serial.printf("⚙️ Config sent to %s\n", bikeId.c_str());
    }
    
    // Finalizar processamento desta bike
    finishCurrentBike();
}

void BikePairing::enqueueBike(const String& bikeId, const String& jsonData) {
    // Verificar se bike já está na fila
    std::queue<String> tempQueue = dataQueue;
    while (!tempQueue.empty()) {
        if (tempQueue.front() == bikeId) {
            Serial.printf("📋 Bike %s already in queue\n", bikeId.c_str());
            return;
        }
        tempQueue.pop();
    }
    
    // Adicionar à fila
    dataQueue.push(bikeId);
    Serial.printf("📋 Bike %s added to queue (size: %d)\n", bikeId.c_str(), (int)dataQueue.size());
}

void BikePairing::finishCurrentBike() {
    if (!currentBike.isEmpty()) {
        Serial.printf("✅ Finished processing bike %s\n", currentBike.c_str());
        currentBike = "";
        requestTimeout = 0;
        currentStatus = PAIRING_IDLE;
    }
}

// Funções auxiliares removidas - lógica consolidada no BikeRegistry