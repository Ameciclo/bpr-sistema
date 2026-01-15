#pragma once
#include <Arduino.h>

class Endpoints {
public:
    // Config endpoints
    static String getConfigVersion();
    static String getCentralConfig();
    static String getWiFiConfig();
    
    // Bike endpoints
    static String getBikeRegistry();
    static String getBikeConfigs();
    static String getBikeRegistryVersion();
    static String getBikeConfigsVersion();
    
    // Data endpoints
    static String getHeartbeat();
    static String getBufferData();
    
private:
    static String buildUrl(const String& path);
};