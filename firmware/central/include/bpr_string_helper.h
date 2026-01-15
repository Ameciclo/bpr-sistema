#pragma once
#include <Arduino.h>

class BPRStringHelper {
public:
    // Respostas padronizadas para bikes
    static String createProceedResponse();
    static String createBusyResponse(uint32_t retryAfterSec = 30);
    static String createConfigResponse(const String& bikeId, const String& config);
    static String createHeartbeatResponse();
};