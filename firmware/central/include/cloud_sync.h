#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

enum class SyncResult {
    SUCCESS,
    FAILURE,
    IN_PROGRESS
};

class CloudSync {
public:
    static SyncResult enter();
    static SyncResult update();
    static void exit();
    
private:
    static bool syncInProgress;
    static SyncResult currentResult;
    static bool connectWiFi();
    static void syncTime();
    static bool downloadCentralConfig();
    static bool downloadBikeData();
    static bool uploadBufferData();
    static bool uploadHeartbeat();
    static bool uploadWiFiConfig();
    static bool uploadBikeData();

};