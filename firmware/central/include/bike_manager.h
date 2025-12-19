#pragma once
#include <Arduino.h>
#include <map>
#include <vector>
#include <ArduinoJson.h>

class BikeManager {
public:
    // Inicialização e cache
    static bool init();
    static bool loadData();
    static bool saveData();
    
    // Controle de acesso (ex-BikeRegistry)
    static bool canConnect(const String& bikeId);
    static bool isAllowed(const String& bikeId);
    static void recordPendingVisit(const String& bikeId);
    static void addPendingBike(const String& bikeId);
    
    // Heartbeat e status
    static void updateHeartbeat(const String& bikeId, int battery, int heap);
    static int getAllowedCount();
    static int getPendingCount();
    static int getConnectedCount();
    static void populateHeartbeatData(JsonArray& bikes);
    
    // Configurações (ex-BikeConfigManager)
    static bool hasConfigUpdate(const String& bikeId);
    static void markConfigSent(const String& bikeId);
    static String getConfigForBike(const String& bikeId);
    static String generateDefaultConfig(const String& bikeId);
    static std::vector<String> getBikesWithUpdates();
    
    // Sincronização Firebase
    static bool downloadFromFirebase();
    static bool uploadToFirebase(DynamicJsonDocument& doc);
    static void updateFromFirebase(const DynamicJsonDocument& firebaseData);
    
    // Logs e eventos
    static void logConfigEvent(const String& bikeId, const String& event, bool success);
};