#include "config_credentials.h"
#include "endpoints.h"

extern ConfigCredentials configCredentials;

String Endpoints::buildUrl(const String& path) {
    const CredentialsConfig& creds = configCredentials.getCredentials();
    return String(creds.firebase_database_url) + path + "?auth=" + creds.firebase_api_key;
}

String Endpoints::getConfigVersion() {
    const String& baseId = configCredentials.getBaseId();
    return buildUrl("/bases/" + baseId + "/configs/last_update.txt");
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
    return buildUrl("/bases/" + baseId + "/bikes/registry.csv");
}

String Endpoints::getBikeConfigs() {
    const String& baseId = configCredentials.getBaseId();
    return buildUrl("/bases/" + baseId + "/bikes/configs.csv");
}

String Endpoints::getBikeRegistryVersion() {
    const String& baseId = configCredentials.getBaseId();
    return buildUrl("/bases/" + baseId + "/bikes/registry_last_update.txt");
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