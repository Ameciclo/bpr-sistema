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
    uint16_t bike_arrived_ms;
    uint16_t bike_left_ms;
};

struct LocationConfig {
    float lat;
    float lng;
};

struct IntervalsConfig {
    uint32_t sync_sec;
    uint32_t cleanup_sec;
    uint32_t log_sec;
    uint32_t led_count_sec;
};

struct TimeoutsConfig {
    uint32_t wifi_sec;
    uint32_t firebase_ms;
    uint16_t config_ap_min;
};

struct LimitsConfig {
    uint8_t max_bikes;
    uint16_t batch_size;
};

struct FallbackConfig {
    uint8_t max_failures;
    uint16_t timeout_min;
};

struct HubConfig {
    char base_id[32];
    LocationConfig location;
    WiFiConfig wifi;
    FirebaseConfig firebase;
    IntervalsConfig intervals;
    TimeoutsConfig timeouts;
    LEDConfig led;
    LimitsConfig limits;
    FallbackConfig fallback;
    
    // Compatibility methods
    uint32_t sync_interval_ms() const { return intervals.sync_sec * 1000; }
    int sync_interval_sec() const { return intervals.sync_sec; }
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
    
    // Firebase URL builders
    String getHubConfigUrl() const;
    String getBikeRegistryUrl() const;
    String getWiFiConfigUrl() const;
    String getHeartbeatUrl() const;
    String getBufferDataUrl() const;
    
    // JSON parsing and validation
    bool updateFromJson(const String& json);
    bool isValidFirebaseConfig(const DynamicJsonDocument& doc) const;

private:
    HubConfig config;
};