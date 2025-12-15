#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

struct BikeConfigData {
    char bike_id[32];
    char bike_name[64];
    uint16_t version;
    bool dev_mode;
    
    struct {
        uint16_t scan_interval_sec;
        uint16_t scan_timeout_ms;
    } wifi;
    
    struct {
        char base_name[32];
        uint8_t scan_time_sec;
    } ble;
    
    struct {
        uint16_t deep_sleep_duration_sec;
    } power;
    
    struct {
        float critical_voltage;
        float low_voltage;
    } battery;
};

class BikeConfigManager {
public:
    static void init();
    static bool handleConfigRequest(const String& bikeId, String& response);
    static bool isBikeAuthorized(const String& bikeId);
    static void logConfigAttempt(const String& bikeId, bool authorized);
    static BikeConfigData getDefaultConfig(const String& bikeId);
    static String generateConfigJson(const BikeConfigData& config);
    
private:
    static bool loadWhitelist();
    static bool checkFirebaseConfig(const String& bikeId, BikeConfigData& config);
};