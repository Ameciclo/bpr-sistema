#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

class BPRJsonHelper {
public:
    // Campos comuns que aparecem em vários JSONs
    static void addTimestamp(JsonDocument& doc, const String& prefix = "");
    
    // Para heartbeats
    static void addHeartbeatFields(JsonDocument& doc);

    // Para respostas de bike
    static void addBikeResponse(JsonDocument& doc, const String& type, const String& bikeId);
    
    // Respostas padronizadas para bikes
    // R13/F05: "proceed" (limpeza sem correlação) removido — limpeza só via
    // data_ack final com can_clear_buffer
    static String createBusyResponse(const String& bikeId, uint32_t retryAfterSec = 30);
    static String createConfigResponse(const String& bikeId, const JsonObject& config);
    static String createDataAckResponse(const String& bikeId, uint32_t sessionId,
                                        uint16_t sessionSalt, uint16_t seq,
                                        bool isFinal, bool canClear);
};