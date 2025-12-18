#include "bike_pairing.h"
#include <ArduinoJson.h>
#include "constants.h"
#include "buffer_manager.h"
#include "led_controller.h"
#include "bike_registry.h"
#include "bike_config_manager.h"
#include "ble_server.h"

extern BufferManager bufferManager;
extern LEDController ledController;
extern SystemState currentState;

static uint32_t lastHeartbeat = 0;
static PairingStatus currentStatus = PAIRING_IDLE;
static uint32_t lastActivity = 0;
static uint32_t busyTimeout = 10000; // 10 segundos para considerar idle

void BikePairing::enter()
{
    Serial.println("🔵 Entering BIKE_PAIRING mode");

    // Initialize services
    BikeRegistry::init();
    BikeConfigManager::init();
    
    // Start BLE server
    if (!BLEServer::start()) {
        Serial.println("❌ Failed to start BLE Server");
        return;
    }
    
    Serial.println("📡 BLE Server started successfully");
    ledController.bikePairingPattern();
}

void BikePairing::update()
{
    uint32_t now = millis();

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
        ledController.countPattern(BLEServer::getConnectedBikes());
    }
}

void BikePairing::exit()
{
    BLEServer::stop();
    Serial.println("🔚 Exiting BIKE_PAIRING mode");
}

uint8_t BikePairing::getConnectedBikes()
{
    return BLEServer::getConnectedBikes();
}

void BikePairing::sendHeartbeat()
{
    DynamicJsonDocument heartbeat(1024);
    heartbeat["timestamp"] = millis() / 1000;
    heartbeat["uptime_sec"] = millis() / 1000;
    heartbeat["heap_free"] = ESP.getFreeHeap();
    
    // Array detalhado de cada bike
    JsonArray bikes = heartbeat.createNestedArray("bikes");
    
    // Iterar por todas as bikes conhecidas
    std::vector<String> knownBikes = BikeRegistry::getAllKnownBikes();
    
    for (const String& bikeId : knownBikes) {
        JsonObject bike = bikes.createNestedObject();
        bike["id"] = bikeId;
        bike["last_seen"] = BikeRegistry::getLastSeen(bikeId);
        bike["status"] = calculateBikeStatus(bikeId);
        bike["sleep_interval_sec"] = BikeConfigManager::getSleepInterval(bikeId);
        bike["next_expected_contact"] = calculateNextContact(bikeId);
        bike["battery_last"] = BikeRegistry::getLastBattery(bikeId);
        bike["is_overdue"] = isBikeOverdue(bikeId);
    }
    
    // Resumo geral
    heartbeat["total_bikes"] = knownBikes.size();
    heartbeat["bikes_connected_now"] = BLEServer::getConnectedBikes();
    heartbeat["bikes_sleeping"] = countSleepingBikes();
    heartbeat["bikes_overdue"] = countOverdueBikes();
    
    // Salvar no LittleFS
    bufferManager.addHeartbeat(heartbeat.as<String>());
    
    Serial.printf("💓 Heartbeat: %d total, %d sleeping, %d overdue\n", 
                  (int)knownBikes.size(), countSleepingBikes(), countOverdueBikes());
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
void BLEServer::onBikeConnected(const String& bikeId) {
    ledController.bikeArrivedPattern();
    Serial.printf("🚲 Bike %s connected\n", bikeId.c_str());
    
    // Verificar se bike pode conectar (não blocked)
    if (!BikeRegistry::canConnect(bikeId)) {
        Serial.printf("❌ Blocked bike %s - disconnecting\n", bikeId.c_str());
        BLEServer::forceDisconnectBike(bikeId);
        return;
    }
    
    // Se tem config pendente, enviar imediatamente
    if (BikeConfigManager::hasConfigUpdate(bikeId)) {
        currentStatus = PAIRING_SENDING_CONFIG;
        lastActivity = millis();
        
        String config = BikeConfigManager::getConfigForBike(bikeId);
        BLEServer::pushConfigToBike(bikeId, config);
        BikeConfigManager::markConfigSent(bikeId);
        
        Serial.printf("⚙️ Config sent to %s on connection\n", bikeId.c_str());
    } else {
        Serial.printf("📝 No config update for %s - skipping\n", bikeId.c_str());
    }
}

void BLEServer::onBikeDisconnected(const String& bikeId) {
    ledController.bikeLeftPattern();
    if (!bikeId.isEmpty()) {
        Serial.printf("🚲 Bike %s disconnected - LED pattern triggered\n", bikeId.c_str());
    } else {
        Serial.printf("🚲 Device disconnected - LED pattern triggered\n");
    }
}

void BLEServer::onBikeDataReceived(const String& bikeId, const String& jsonData) {
    // Marcar atividade de recepção de dados
    currentStatus = PAIRING_RECEIVING_DATA;
    lastActivity = millis();
    
    Serial.printf("🔍 Processing data from bike: %s\n", bikeId.c_str());
    
    // Validar se bike pode se conectar (não blocked)
    if (!BikeRegistry::canConnect(bikeId)) {
        Serial.printf("❌ Data rejected from blocked bike: %s\n", bikeId.c_str());
        currentStatus = PAIRING_IDLE; // Rejeição é rápida
        return;
    }
    
    Serial.printf("✅ Bike %s can connect\n", bikeId.c_str());
    
    // Validar se pode enviar dados (só allowed)
    if (!BikeRegistry::isAllowed(bikeId)) {
        BikeRegistry::recordPendingVisit(bikeId);
        Serial.printf("📝 Pending bike %s visited - data ignored (awaiting approval)\n", bikeId.c_str());
        currentStatus = PAIRING_IDLE; // Pending é rápido
        return;
    }
    
    Serial.printf("📥 Data from %s\n", bikeId.c_str());
    
    // Parse para atualizar heartbeat
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, jsonData) == DeserializationError::Ok) {
        if (doc["battery"] && doc["heap"]) {
            BikeRegistry::updateHeartbeat(bikeId, doc["battery"], doc["heap"]);
        }
    }
    
    // Processar dados via BufferManager
    bufferManager.addBikeData(bikeId, jsonData);
    
    // Verificar se tem config nova para enviar
    if (BikeConfigManager::hasConfigUpdate(bikeId)) {
        currentStatus = PAIRING_SENDING_CONFIG;
        lastActivity = millis();
        
        String config = BikeConfigManager::getConfigForBike(bikeId);
        BLEServer::pushConfigToBike(bikeId, config);
        BikeConfigManager::markConfigSent(bikeId);
        
        Serial.printf("⚙️ Config sent to %s - marked as busy\n", bikeId.c_str());
    } else {
        // Processamento completo, voltar para idle
        currentStatus = PAIRING_IDLE;
    }
}

void BLEServer::onConfigRequest(const String& bikeId, const String& request) {
    // Marcar atividade de configuração
    currentStatus = PAIRING_SENDING_CONFIG;
    lastActivity = millis();
    
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, request);
    
    if (error) {
        Serial.printf("❌ Config request parse error: %s\n", error.c_str());
        currentStatus = PAIRING_IDLE;
        return;
    }
    
    String type = doc["type"] | "";
    
    if (type == "config_request") {
        Serial.printf("📝 Config request from %s\n", bikeId.c_str());
        
        if (BikeConfigManager::hasConfigUpdate(bikeId)) {
            String config = BikeConfigManager::getConfigForBike(bikeId);
            BLEServer::pushConfigToBike(bikeId, config);
            BikeConfigManager::markConfigSent(bikeId);
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

// Funções auxiliares para heartbeat inteligente
String BikePairing::calculateBikeStatus(const String& bikeId) {
    uint32_t lastSeen = BikeRegistry::getLastSeen(bikeId);
    uint32_t sleepInterval = BikeConfigManager::getSleepInterval(bikeId);
    uint32_t now = millis() / 1000;
    
    if (BLEServer::isBikeConnected(bikeId)) return "connected";
    if ((now - lastSeen) < sleepInterval) return "sleeping";
    if ((now - lastSeen) < (sleepInterval * 1.5)) return "expected_soon";
    return "overdue";
}

uint32_t BikePairing::calculateNextContact(const String& bikeId) {
    uint32_t lastSeen = BikeRegistry::getLastSeen(bikeId);
    uint32_t sleepInterval = BikeConfigManager::getSleepInterval(bikeId);
    return lastSeen + sleepInterval;
}

bool BikePairing::isBikeOverdue(const String& bikeId) {
    uint32_t nextExpected = calculateNextContact(bikeId);
    uint32_t now = millis() / 1000;
    return (now > nextExpected + 300); // 5min de tolerância
}

int BikePairing::countSleepingBikes() {
    std::vector<String> knownBikes = BikeRegistry::getAllKnownBikes();
    int count = 0;
    for (const String& bikeId : knownBikes) {
        if (calculateBikeStatus(bikeId) == "sleeping") count++;
    }
    return count;
}

int BikePairing::countOverdueBikes() {
    std::vector<String> knownBikes = BikeRegistry::getAllKnownBikes();
    int count = 0;
    for (const String& bikeId : knownBikes) {
        if (isBikeOverdue(bikeId)) count++;
    }
    return count;
}