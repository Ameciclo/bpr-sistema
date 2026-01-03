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
    static void updateHeartbeat(const String& bikeId, int battery, int heap, 
                               uint16_t sessions = 0, uint32_t bytes = 0, uint32_t oldestTs = 0, uint8_t bufferPercent = 0);
    static int getAllowedCount();
    static int getPendingCount();
    static int getConnectedCount();
    static void populateHeartbeatData(JsonArray& bikes);
    
    // Getters para pending data (usado pelo BikePairing)
    static uint32_t getPendingBytes(const String& bikeId);
    static uint16_t getPendingSessions(const String& bikeId);
    static uint8_t getBufferUsage(const String& bikeId);
    static bool isBatteryLow(const String& bikeId);
    
    // Configurações (ex-BikeConfigManager)
    static bool hasConfigUpdate(const String& bikeId);
    static void markConfigSent(const String& bikeId);
    static String getConfigForBike(const String& bikeId);
    static bool needsConfigUpdate(const String& bikeId, uint32_t bikeLastUpdate);
    static std::vector<String> getBikesWithUpdates();
    
    // Sincronização com CloudSync (apenas preparação de dados)
    static bool getPendingBikesForUpload(DynamicJsonDocument& doc);
    static void updateFromCloudSync(const DynamicJsonDocument& firebaseData);
    
    // Logs e eventos
    static void logConfigEvent(const String& bikeId, const String& event, bool success);
    
    // === SYSTEM HEARTBEAT ===
    static String generateSystemHeartbeat();
    static void saveSystemHeartbeat();
    
    // === DATA UPLOAD CONFIRMATION ===
    static String confirmDataUpload(const String& bikeId);
};