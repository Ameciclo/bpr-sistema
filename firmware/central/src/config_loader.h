#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

struct AppConfig {
    char firebase_host[128];
    char firebase_auth[256];
    char base_id[32];
    char base_name[64];
    char wifi_ssid[64];
    char wifi_password[128];
};

bool loadConfiguration();
AppConfig getAppConfig();

#endif