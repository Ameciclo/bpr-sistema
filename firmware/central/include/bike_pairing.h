#pragma once
#include <Arduino.h>

enum PairingStatus {
    PAIRING_IDLE,           // Nenhuma atividade crítica
    PAIRING_RECEIVING_DATA, // Recebendo dados de bike
    PAIRING_SENDING_CONFIG, // Enviando config para bike
    PAIRING_BUSY           // Atividade geral (múltiplas bikes)
};

enum BikeEvent {
    BIKE_ARRIVED,
    BIKE_LEFT,
    BIKE_COUNT_CHANGED
};

// Callback para eventos de bike
typedef void (*BikeEventCallback)(BikeEvent event, uint8_t bikeCount);

class BikePairing {
public:
    static void enter();
    static void update();
    static void exit();
    static void printStatus();
    static uint8_t getConnectedBikes();
    static PairingStatus getStatus();
    static bool isSafeToExit();
    static void sendHeartbeat();
    
    // Sistema de eventos
    static void setEventCallback(BikeEventCallback callback);
    static void triggerEvent(BikeEvent event, uint8_t bikeCount = 0);
    
    // Funções auxiliares para heartbeat inteligente
    static String calculateBikeStatus(const String& bikeId);
    static uint32_t calculateNextContact(const String& bikeId);
    static bool isBikeOverdue(const String& bikeId);
    static int countSleepingBikes();
    static int countOverdueBikes();
    
    // Métodos de processamento de dados
    static void processDataQueue();
    static void requestDataFromBike(const String& bikeId);
    static void processDataFromBike(const String& bikeId, const String& jsonData);
    static void enqueueBike(const String& bikeId, const String& jsonData);
    static void finishCurrentBike();
};