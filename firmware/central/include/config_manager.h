#pragma once
#include <Arduino.h>

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
    uint32_t pairing_busy_ms;
    uint16_t config_ap_min;
};

struct LimitsConfig {
    uint8_t max_bikes;
    uint16_t batch_size;
};

struct FallbackConfig {
    uint8_t max_failures;
    uint16_t timeout_min;
    uint8_t sync_max_retries;
    uint16_t config_ap_timeout_sec;
};

struct BufferConfig {
    uint8_t max_size;
    uint8_t sync_threshold_percent;
    uint8_t auto_save_interval;
    uint16_t max_item_size;
};

struct CompressionConfig {
    bool enabled;
    uint16_t min_size_bytes;
};

struct StorageConfig {
    uint16_t min_free_kb;
    uint16_t warning_threshold_kb;
    float aggressive_cleanup_multiplier;
};

struct BackupConfig {
    bool enabled;
    uint16_t retention_hours;
};

struct CentralConfig {
    uint32_t version;
    uint32_t last_update;
    LocationConfig location;
    IntervalsConfig intervals;
    TimeoutsConfig timeouts;
    LEDConfig led;
    LimitsConfig limits;
    FallbackConfig fallback;
    BufferConfig buffer;
    CompressionConfig compression;
    StorageConfig storage;
    BackupConfig backup;
    uint8_t padding[4]; // Padding para alinhamento
    
    uint32_t sync_interval_ms() const { return intervals.sync_sec * 1000; }
    int sync_interval_sec() const { return intervals.sync_sec; }
} __attribute__((packed, aligned(4)));

class ConfigManager {
public:
    ConfigManager();
    bool loadConfig();
    bool saveConfig();
    bool isConfigValid();
    bool updateFromCSV(const String& csvData);
    void setConfigValue(int index, const String& value);
    
    const CentralConfig& getConfig() const { return config; }
    CentralConfig& getConfig() { return config; }
    
    String getBaseId() const;
    int getSyncInterval() const { return config.intervals.sync_sec; }
    uint32_t getPairingBusyTimeout() const { return config.timeouts.pairing_busy_ms; }
    int getBackupRetentionHours() const { return config.backup.retention_hours; }

private:
    CentralConfig config;
};