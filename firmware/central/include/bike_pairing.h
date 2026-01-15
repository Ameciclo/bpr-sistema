#pragma once
#include <Arduino.h>
#include "constants.h"

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

    
    // Sistema de eventos
    static void setEventCallback(BikeEventCallback callback);
    static void triggerEvent(BikeEvent event, uint8_t bikeCount = 0);
    
    // Métodos de processamento de dados
    static void processDataQueue();
    static void processDataFromBike(const String& bikeId, const String& jsonData);
    static void enqueueBike(const String& bikeId, const String& jsonData, uint8_t priority = 2);
    static void finishCurrentBike();
};