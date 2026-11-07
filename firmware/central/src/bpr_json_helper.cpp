#include "bike_manager.h"
#include "bpr_json_helper.h"
#include "time_sync.h"

void BPRJsonHelper::addTimestamp(JsonDocument& doc, const String& prefix) {
    time_t now = time(nullptr);

    String tsKey = prefix.isEmpty() ? "timestamp" : prefix + "_timestamp";

    // F20: não fabrica data válida quando o NTP falhou
    if (now > 1000000000L) {
        doc[tsKey] = now;
    } else {
        doc[tsKey] = 0;
        String flagKey = prefix.isEmpty() ? "timestamp_valid" : prefix + "_timestamp_valid";
        doc[flagKey] = false;
    }
}

void BPRJsonHelper::addHeartbeatFields(JsonDocument& doc) {
    addTimestamp(doc);
    doc["bikes_connected"] = BikeManager::getConnectedCount();
    doc["heap"] = ESP.getFreeHeap();
    doc["uptime"] = millis() / 1000;
}

void BPRJsonHelper::addBikeResponse(JsonDocument& doc, const String& type, const String& bikeId) {
    doc["type"] = type;
    doc["bike_id"] = bikeId;
    doc["timestamp"] = time(nullptr);
}

String BPRJsonHelper::createBusyResponse(const String& bikeId, uint32_t retryAfterSec) {
    DynamicJsonDocument doc(256);
    doc["type"] = "busy";
    // R13: resposta endereçada e correlacionada — a bike certa espera e as
    // outras ignoram; retry_after_sec é respeitado pelo receptor
    doc["bike_id"] = bikeId;
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

// F05: ACK de fragmento identificado — stored só é true com persistência íntegra
String BPRJsonHelper::createDataAckResponse(const String& bikeId, uint32_t sessionId,
                                            uint16_t sessionSalt, uint16_t seq,
                                            bool isFinal, bool canClear) {
    DynamicJsonDocument doc(256);
    doc["type"] = "data_ack";
    doc["bike_id"] = bikeId;
    doc["session_id"] = sessionId;
    doc["session_salt"] = sessionSalt;
    doc["seq"] = seq;
    doc["stored"] = true;
    doc["is_final"] = isFinal;
    doc["can_clear_buffer"] = canClear;

    String result;
    serializeJson(doc, result);
    return result;
}
