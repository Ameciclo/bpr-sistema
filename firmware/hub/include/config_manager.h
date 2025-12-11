#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

struct WiFiConfig {
    char ssid[64];
    char password[64];
    uint32_t timeout_ms;
};

struct FirebaseConfig {
    char project_id[64];
    char database_url[128];
    char api_key[128];
};

struct LEDConfig {
    uint8_t pin;
    uint16_t boot_ms;
    uint16_t ble_ms;
    uint16_t sync_ms;
    uint16_t error_ms;
    uint16_t count_ms;
    uint16_t count_pause_ms;
};

struct HubConfig {
    char base_id[32];
    WiFiConfig wifi;
    FirebaseConfig firebase;
    LEDConfig led;
    uint32_t sync_interval_ms;
    uint16_t max_buffer_size;
    char ntp_server[64];
    int32_t timezone_offset;
    bool auto_approve_bikes;
    uint8_t max_bikes;
    
    // Compatibility with main.cpp
    int sync_interval_sec() const { return sync_interval_ms / 1000; }
};

class ConfigManager {
public:
    ConfigManager();
    bool loadConfig();
    bool saveConfig();
    bool isConfigValid();
    void updateFromFirebase(const DynamicJsonDocument& firebaseConfig);
    
    const HubConfig& getConfig() const { return config; }
    HubConfig& getConfig() { return config; }

private:
    HubConfig config;
};