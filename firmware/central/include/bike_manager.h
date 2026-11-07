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
    static bool isLoaded();

    // Controle de acesso (ex-BikeRegistry)
    static bool canConnect(const String& bikeId);
    static bool isAllowed(const String& bikeId);
    static bool isKnownBike(const String& bikeId);
    static void recordPendingVisit(const String& bikeId);
    static void addPendingBike(const String& bikeId);

    // F03: cadastro remoto -> lista operacional
    static bool mergeFromRegistry();

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

    // Bike configs (binary format)
    static bool loadBikeConfigs();
    static bool saveBikeConfigs();
    static bool downloadSingleBikeConfig(const String& bikeId);
    static bool fetchAndApplyBikeConfig(const String& bikeId, uint32_t remoteVersion); // R07/R20
    static bool updateBikeConfigFromCSV(const String& bikeId, const String& csvData, uint32_t remoteVersion = 0);
    static std::vector<String> getAllowedBikeIds();

    // Configurações (ex-BikeConfigManager)
    static bool hasConfigUpdate(const String& bikeId);
    static void markConfigSent(const String& bikeId);
    static void markConfigAcked(const String& bikeId, uint32_t appliedVersion);
    static uint32_t getAckedConfigVersion(const String& bikeId);
    static String getConfigForBike(const String& bikeId);
    static bool needsConfigUpdate(const String& bikeId, uint32_t bikeLastUpdate);
    static std::vector<String> getBikesWithUpdates();

    // Sincronização com CloudSync (apenas preparação de dados)
    static bool getPendingBikesForUpload(DynamicJsonDocument& doc);

    // Logs e eventos
    static void logConfigEvent(const String& bikeId, const String& event, bool success);

    // === SYSTEM HEARTBEAT ===
    static String generateSystemHeartbeat();
};
