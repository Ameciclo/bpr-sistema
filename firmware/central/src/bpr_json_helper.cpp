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

// T03 (PRD lotes): mesmo índice com conteúdo diferente = conflito explícito —
// a bike mantém o lote e não conta a parte como válida
String BPRJsonHelper::createDataAckConflict(const String& bikeId, uint32_t sessionId,
                                            uint16_t sessionSalt, uint16_t seq) {
    DynamicJsonDocument doc(256);
    doc["type"] = "data_ack";
    doc["bike_id"] = bikeId;
    doc["session_id"] = sessionId;
    doc["session_salt"] = sessionSalt;
    doc["seq"] = seq;
    doc["stored"] = false;
    doc["conflict"] = true;

    String result;
    serializeJson(doc, result);
    return result;
}

// PRD lotes/recibos: página do bitmap de pendências (bit 1 = parte presente).
// Páginas de 64 partes (16 hex chars) cabem em MTU pequeno (T16).
String BPRJsonHelper::createBatchStatusPage(const String& bikeId, uint32_t sessionId,
                                            uint16_t sessionSalt, const String& state,
                                            uint8_t page, uint8_t pages,
                                            const uint8_t bitmap8[8]) {
    DynamicJsonDocument doc(320);
    doc["type"] = "batch_status";
    doc["v"] = 1;
    doc["bike_id"] = bikeId;
    doc["session_id"] = sessionId;
    doc["session_salt"] = sessionSalt;
    doc["state"] = state;
    doc["page"] = page;
    doc["pages"] = pages;

    char hex[17];
    for (int i = 0; i < 8; i++) {
        snprintf(&hex[i * 2], 3, "%02x", bitmap8[i]);
    }
    doc["missing"] = hex;

    String result;
    serializeJson(doc, result);
    return result;
}

// PRD lotes/recibos: autorização de limpeza ligada à confirmação DURÁVEL na
// nuvem — identifica exatamente o conteúdo que pode ser liberado (5.2)
String BPRJsonHelper::createBatchClearAuth(const String& bikeId, uint32_t sessionId,
                                           uint16_t sessionSalt, uint16_t total,
                                           uint32_t totalBytes, const char shaHex[65],
                                           uint32_t confirmedAt) {
    DynamicJsonDocument doc(384);
    doc["type"] = "batch_clear_auth";
    doc["v"] = 1;
    doc["bike_id"] = bikeId;
    doc["session_id"] = sessionId;
    doc["session_salt"] = sessionSalt;
    doc["total"] = total;
    doc["total_bytes"] = totalBytes;
    doc["sha256"] = shaHex;
    doc["confirmed_at"] = confirmedAt;

    String result;
    serializeJson(doc, result);
    return result;
}
