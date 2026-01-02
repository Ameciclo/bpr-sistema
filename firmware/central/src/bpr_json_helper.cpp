#include "bpr_json_helper.h"
#include "bike_manager.h"

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