#include "config_credentials.h"
#include "endpoints.h"

extern ConfigCredentials configCredentials;

String Endpoints::buildUrl(const String& path) {
    const CredentialsConfig& creds = configCredentials.getCredentials();
    return String(creds.firebase_database_url) + path + "?auth=" + creds.firebase_api_key;
}

String Endpoints::getConfigVersion() {
    const String& baseId = configCredentials.getBaseId();
    return buildUrl("/bases/" + baseId + "/configs/last_update.json");
}

String Endpoints::getCentralConfig() {
    const String& baseId = configCredentials.getBaseId();
    return buildUrl("/bases/" + baseId + "/config.json");
}

String Endpoints::getWiFiConfig() {
    const String& baseId = configCredentials.getBaseId();
    return buildUrl("/bases/" + baseId + "/configs/wifi.json");
}

String Endpoints::getBikeRegistry() {
    const String& baseId = configCredentials.getBaseId();
    return buildUrl("/bases/" + baseId + "/bike_registry.json");
}

String Endpoints::getBikeConfigs() {
    const String& baseId = configCredentials.getBaseId();
    return buildUrl("/bases/" + baseId + "/bikes/configs.csv");
}

String Endpoints::getBikeRegistryVersion() {
    const String& baseId = configCredentials.getBaseId();
    return buildUrl("/bases/" + baseId + "/bike_registry/last_update.json");
}

String Endpoints::getBikeConfigsVersion() {
    const String& baseId = configCredentials.getBaseId();
    return buildUrl("/bases/" + baseId + "/bikes/configs_last_update.txt");
}

String Endpoints::getHeartbeat() {
    const String& baseId = configCredentials.getBaseId();
    return buildUrl("/bases/" + baseId + "/last_heartbeat.json");
}

String Endpoints::getBufferData() {
    const String& baseId = configCredentials.getBaseId();
    return buildUrl("/bases/" + baseId + "/data.json");
}

// F19: lote imutável com chave estável — retries idempotentes não duplicam histórico
String Endpoints::getBufferDataBatch(const String& batchKey) {
    const String& baseId = configCredentials.getBaseId();
    return buildUrl("/bases/" + baseId + "/data/" + batchKey + ".json");
}

// R15: descobertas locais (bikes pendentes) publicadas para aprovação remota
String Endpoints::getPendingBikes() {
    const String& baseId = configCredentials.getBaseId();
    return buildUrl("/bases/" + baseId + "/pending_bikes.json");
}

// PRD lotes/recibos: armazenamento remoto por ORIGEM (bike+lote) — a
// conclusão canônica é consultada por bike, independente da estação (T18)
String Endpoints::getBatchPartUrl(const String& bikeId, uint32_t sid, uint16_t salt,
                                  uint16_t index) {
    return buildUrl("/bikes/" + bikeId + "/batches/" + String(sid) + "-" +
                    String(salt) + "/parts/" + String(index) + ".json");
}

String Endpoints::getBatchMetaUrl(const String& bikeId, uint32_t sid, uint16_t salt) {
    return buildUrl("/bikes/" + bikeId + "/batches/" + String(sid) + "-" +
                    String(salt) + "/meta.json");
}

String Endpoints::getBatchConfirmationUrl(const String& bikeId, uint32_t sid,
                                          uint16_t salt) {
    return buildUrl("/bikes/" + bikeId + "/batch_confirmations/" + String(sid) +
                    "-" + String(salt) + ".json");
}

// X10: timeouts explícitos — conexão 8 s, leitura 15 s. Sem isso, WiFi fraco
// podia travar cada chamada por dezenas de segundos no loop único do C3
void Endpoints::setupHttp(HTTPClient& http) {
    http.setConnectTimeout(8000);
    http.setTimeout(15000);
}