#include "endpoints.h"
#include "config_credentials.h"

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
    return buildUrl("/bases/" + baseId + "/configs.json");
}

String Endpoints::getWiFiConfig() {
    const String& baseId = configCredentials.getBaseId();
    return buildUrl("/bases/" + baseId + "/configs/wifi.json");
}

String Endpoints::getBikeRegistry() {
    const String& baseId = configCredentials.getBaseId();
    return buildUrl("/bases/" + baseId + "/bikes.json");
}

String Endpoints::getBikeConfigs() {
    return buildUrl("/bike_configs.json");
}

String Endpoints::getBikeRegistryVersion() {
    const String& baseId = configCredentials.getBaseId();
    return buildUrl("/bases/" + baseId + "/bikes/last_update.json");
}

String Endpoints::getBikeConfigsVersion() {
    return buildUrl("/bike_configs/last_update.json");
}

String Endpoints::getHeartbeat() {
    const String& baseId = configCredentials.getBaseId();
    return buildUrl("/bases/" + baseId + "/last_heartbeat.json");
}

String Endpoints::getBufferData() {
    const String& baseId = configCredentials.getBaseId();
    return buildUrl("/bases/" + baseId + "/data.json");
}