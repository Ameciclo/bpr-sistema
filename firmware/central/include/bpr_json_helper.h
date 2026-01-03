#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

class BPRJsonHelper {
public:
    // Campos comuns que aparecem em vários JSONs
    static void addTimestamp(JsonDocument& doc, const String& prefix = "");
    
    // Para heartbeats
    static void addHeartbeatFields(JsonDocument& doc);
    
    // Para dados de config
    static void addConfigFields(JsonDocument& doc, const String& configType, const String& data);
    
    // Para respostas de bike
    static void addBikeResponse(JsonDocument& doc, const String& type, const String& bikeId);
    
    // Respostas padronizadas para bikes
    static String createProceedResponse();
    static String createBusyResponse(uint32_t retryAfterSec = 30);
    static String createConfigResponse(const String& bikeId, const JsonObject& config);
};