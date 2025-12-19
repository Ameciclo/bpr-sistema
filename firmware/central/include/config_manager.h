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
    char base_id[32];
    LocationConfig location;
    WiFiConfig wifi;
    FirebaseConfig firebase;
    IntervalsConfig intervals;
    TimeoutsConfig timeouts;
    LEDConfig led;
    LimitsConfig limits;
    FallbackConfig fallback;
    BufferConfig buffer;
    CompressionConfig compression;
    StorageConfig storage;
    BackupConfig backup;
    
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
    
    const CentralConfig& getConfig() const { return config; }
    CentralConfig& getConfig() { return config; }
    
    // Firebase URL builders
    String getCentralConfigUrl() const;
    String getBikeRegistryUrl() const;
    String getWiFiConfigUrl() const;
    String getHeartbeatUrl() const;
    String getBufferDataUrl() const;
    
    // JSON parsing and validation
    bool updateFromJson(const String& json);
    bool isValidFirebaseConfig(const DynamicJsonDocument& doc) const;
    
    // Buffer configuration getters
    int getBufferMaxSize() const { return config.buffer.max_size; }
    int getBufferSyncThreshold() const { return config.buffer.sync_threshold_percent; }
    int getAutoSaveInterval() const { return config.buffer.auto_save_interval; }
    int getMaxItemSize() const { return config.buffer.max_item_size; }
    
    // Compression configuration
    bool getCompressionEnabled() const { return config.compression.enabled; }
    int getCompressionMinSize() const { return config.compression.min_size_bytes; }
    
    // Storage configuration
    int getStorageMinFreeKB() const { return config.storage.min_free_kb; }
    int getStorageWarningKB() const { return config.storage.warning_threshold_kb; }
    float getAggressiveCleanupMultiplier() const { return config.storage.aggressive_cleanup_multiplier; }
    
    // Backup configuration
    bool getBackupEnabled() const { return config.backup.enabled; }
    int getBackupRetentionHours() const { return config.backup.retention_hours; }
    
    // Convenience methods
    String getBaseId() const { return String(config.base_id); }
    int getSyncInterval() const { return config.intervals.sync_sec; }

private:
    CentralConfig config;
};