#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>

// Estrutura unificada de configuração
struct CentralConfigCache {
    char base_id[32];
    int sync_interval_sec;
    int wifi_timeout_sec;
    int led_pin;
    int firebase_batch_size;
    char central_name[32];
    int central_max_bikes;
    char wifi_ssid[32];
    char wifi_password[64];
    float central_lat;
    float central_lng;
    int led_boot_ms;
    int led_ble_ready_ms;
    int led_wifi_sync_ms;
    unsigned long configTimestamp;
    unsigned long lastUpdate;
    bool valid;
};

// Funções de configuração
bool checkConfigUpdates();
bool downloadConfigs();
bool loadConfigCache();
bool saveConfigCache();
CentralConfigCache getCentralConfig();
int getSyncInterval();
int getLedPin();
const char* getWifiSSID();
const char* getWifiPassword();
bool isConfigValid();
void invalidateConfig();

#endif