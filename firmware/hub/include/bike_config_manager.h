#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include <map>
#include <vector>

#define BIKE_CONFIG_CACHE_FILE "/bike_config_versions.json"
#define BIKE_CONFIGS_FILE "/bike_configs.json"

class BikeConfigManager {
public:
    static bool init();
    static bool loadConfigCache();
    static bool saveConfigCache();
    static bool downloadConfigsFromFirebase();
    static void checkForConfigChanges(const DynamicJsonDocument& newConfigs);
    static bool hasConfigUpdate(const String& bikeId);
    static void markConfigSent(const String& bikeId);
    static String getConfigForBike(const String& bikeId);
    static String generateDefaultConfig(const String& bikeId);
    static void pushConfigToBike(const String& bikeId, NimBLECharacteristic* pConfigChar);
    static std::vector<String> getBikesWithUpdates();
};