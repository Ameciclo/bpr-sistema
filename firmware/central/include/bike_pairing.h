#pragma once
#include <Arduino.h>

enum PairingStatus {
    PAIRING_IDLE,           // Nenhuma atividade crítica
    PAIRING_RECEIVING_DATA, // Recebendo dados de bike
    PAIRING_SENDING_CONFIG, // Enviando config para bike
    PAIRING_BUSY           // Atividade geral (múltiplas bikes)
};

class BikePairing {
public:
    static void enter();
    static void update();
    static void exit();
    static uint8_t getConnectedBikes();
    static PairingStatus getStatus();
    static bool isSafeToExit();
    static void sendHeartbeat();
    
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