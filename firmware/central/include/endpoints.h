#pragma once
#include <Arduino.h>
#include <HTTPClient.h>

class Endpoints {
public:
    // Config endpoints
    static String getConfigVersion();
    static String getCentralConfig();
    static String getWiFiConfig();

    // Bike endpoints
    static String getBikeRegistry();
    static String getBikeConfigs();
    static String getBikeRegistryVersion();
    static String getBikeConfigsVersion();

    // Data endpoints
    static String getHeartbeat();
    static String getBufferData();
    static String getBufferDataBatch(const String& batchKey);
    static String getPendingBikes(); // R15

    // PRD lotes/recibos: lote por origem (bike + sessão) na nuvem
    static String getBatchPartUrl(const String& bikeId, uint32_t sid, uint16_t salt,
                                  uint16_t index);
    static String getBatchMetaUrl(const String& bikeId, uint32_t sid, uint16_t salt);
    static String getBatchConfirmationUrl(const String& bikeId, uint32_t sid,
                                          uint16_t salt);

    // X10: orçamento explícito de toda operação HTTP do produto — o default
    // da biblioteca (5 s de TCP) não é deadline de conexão/leitura do sistema
    static void setupHttp(HTTPClient& http);

private:
    static String buildUrl(const String& path);
};