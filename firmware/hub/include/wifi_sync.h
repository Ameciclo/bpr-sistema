#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

enum class SyncResult {
    SUCCESS,
    FAILURE
};

class WiFiSync {
public:
    static SyncResult enter();
    static void update();
    static void exit();
    static bool connectWiFi();
    static void syncTime();
    static bool downloadHubConfig();
    static bool downloadBikeConfigs();
    static bool uploadBufferData();
    static bool uploadHeartbeat();
    static bool uploadWiFiConfig();
    static bool downloadBikeRegistry();
    static bool uploadBikeRegistry();

};