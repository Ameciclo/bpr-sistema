#include "bike_manager.h"
#include "bpr_json_helper.h"

void BPRJsonHelper::addTimestamp(JsonDocument& doc, const String& prefix) {
    time_t now = time(nullptr);
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    
    char dateStr[64];
    strftime(dateStr, sizeof(dateStr), "%Y-%m-%d %H:%M:%S UTC-3", &timeinfo);
    
    String tsKey = prefix.isEmpty() ? "timestamp" : prefix + "_timestamp";
    String humanKey = prefix.isEmpty() ? "timestamp_human" : prefix + "_timestamp_human";
    
    doc[tsKey] = now;
    doc[humanKey] = dateStr;
}

void BPRJsonHelper::addHeartbeatFields(JsonDocument& doc) {
    addTimestamp(doc);
    doc["bikes_connected"] = BikeManager::getConnectedCount();
    doc["heap"] = ESP.getFreeHeap();
    doc["uptime"] = millis() / 1000;
}

void BPRJsonHelper::addConfigFields(JsonDocument& doc, const String& configType, const String& data) {
    doc["config_type"] = configType;
    doc["data"] = data;
    addTimestamp(doc, "central_receive");
}

void BPRJsonHelper::addBikeResponse(JsonDocument& doc, const String& type, const String& bikeId) {
    doc["type"] = type;
    doc["bike_id"] = bikeId;
    doc["timestamp"] = time(nullptr);
}
String BPRJsonHelper::createProceedResponse() {
    DynamicJsonDocument doc(256);
    doc["type"] = "proceed";
    doc["can_upload"] = true;
    doc["can_clear_buffer"] = true;
    
    String result;
    serializeJson(doc, result);
    return result;
}

String BPRJsonHelper::createBusyResponse(uint32_t retryAfterSec) {
    DynamicJsonDocument doc(256);
    doc["type"] = "busy";
    doc["message"] = "Central busy - try again later";
    doc["retry_after_sec"] = retryAfterSec;
    
    String result;
    serializeJson(doc, result);
    return result;
}

String BPRJsonHelper::createConfigResponse(const String& bikeId, const JsonObject& config) {
    DynamicJsonDocument doc(1024);
    addBikeResponse(doc, "config_push", bikeId);
    doc["config"] = config;
    
    String result;
    serializeJson(doc, result);
    return result;
}