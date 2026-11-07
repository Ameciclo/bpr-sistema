#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "constants.h"

// FSD Binary Config Structure
struct BikeConfig {
    uint32_t version;
    char bike_id[16];
    char bike_name[32];
    bool dev_mode;
    
    struct {
        uint16_t scan_interval_sec;
        uint16_t scan_timeout_ms;
        uint8_t max_networks;
        int8_t rssi_threshold;
    } wifi;
    
    struct {
        char base_name[32];
        uint16_t scan_time_sec;
        uint16_t connection_timeout_ms;
    } ble;
    
    struct {
        uint32_t deep_sleep_duration_sec;
        uint16_t radio_coordination_delay_ms;
    } power;
    
    struct {
        float critical_voltage;
        float low_voltage;
    } battery;
    
    uint32_t timestamp;
} __attribute__((packed));

class ConfigManager {
private:
    BikeConfig config;
    void resetToDefaults();
    // N09.2: rejected=true sinaliza campo presente com valor inválido —
    // a operação inteira é recusada, não aplicada parcialmente
    bool applyUpdate(JsonObject cfg, BikeConfig& candidate, bool& rejected);
    static bool isValidConfig(const BikeConfig& cfg);

public:
    ConfigManager();
    bool load();
    bool save(); // R09: retorno verificável
    void generateUniqueId();
    bool processUpdate(const String& configJson);
    bool isValid();
    BikeConfig& getConfig();
};

#endif