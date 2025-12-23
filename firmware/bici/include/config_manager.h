#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include "constants.h"

struct Config {
    // Basic
    char bike_id[32];
    char bike_name[32];
    char base_ble_name[32];
    int version;
    bool dev_mode;
    
    // WiFi
    int scan_interval_sec;
    int scan_interval_low_batt_sec;
    int wifi_scan_timeout_ms;
    int wifi_max_networks;
    int wifi_rssi_threshold;
    
    // BLE
    int ble_scan_time_sec;
    int ble_connection_timeout_ms;
    
    // Power
    int radio_coordination_delay_ms;
    int light_sleep_duration_ms;
    int deep_sleep_sec;
    int max_time_without_base_sec;
    
    // Battery
    float battery_critical_voltage;
    float min_battery_voltage;
    float battery_full_voltage;
    
    // Timing
    int status_report_interval_ms;
    int emergency_button_hold_ms;
    
    // Buffers
    int max_wifi_records;
    
    // === NEW FIELDS FROM INTERACAO_BIKE_CENTRAL.md ===
    // Battery percentages
    uint8_t battery_critical_percent;     // 15% - Entra em LOW_BATTERY
    uint8_t battery_low_percent;          // 25% - Reduz frequência de scans
    uint8_t battery_recovery_percent;     // 30% - Sai de LOW_BATTERY (hysteresis)
    
    // Intervals
    uint32_t checkin_interval_sec;        // "dar oi" a cada 5min
    uint32_t scan_interval_normal_sec;    // WiFi scan a cada 25s
    uint32_t scan_interval_low_battery_sec;  // 2min (economia)
    uint32_t scan_interval_critical_sec;  // 5min+ (economia extrema)
    uint32_t checkin_low_battery_sec;     // 10min (esporádico)
    
    // Sleep durations
    uint32_t deep_sleep_duration_sec;     // 1h (normal)
    uint32_t deep_sleep_critical_sec;     // 2h (economia extrema)
    
    // Timeouts
    uint32_t busy_retry_delay_sec;        // delay extra quando central busy
    
    // States
    bool enable_low_battery_mode;
    bool enable_emergency_upload;
};

class ConfigManager {
private:
    Config config;

public:
    ConfigManager();
    bool load();
    void save();
    void generateUniqueId();
    bool processUpdate(const String& configJson);
    bool isValid();
    Config& getConfig();
};

#endif