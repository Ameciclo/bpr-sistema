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
    Config& getConfig();
};

#endif