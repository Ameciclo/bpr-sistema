#include "bpr_string_helper.h"

String BPRStringHelper::createProceedResponse() {
    return "proceed";
}

String BPRStringHelper::createBusyResponse(uint32_t retryAfterSec) {
    return "busy," + String(retryAfterSec);
}

String BPRStringHelper::createConfigResponse(const String& bikeId, const String& config) {
    return "config," + bikeId + "," + config;
}

String BPRStringHelper::createHeartbeatResponse() {
    return String(millis() / 1000) + "," + String(ESP.getFreeHeap());
}