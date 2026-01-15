#pragma once
#include <Arduino.h>

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
    static void printStatus();
    
private:
    static bool syncInProgress;
    static SyncResult currentResult;
    
    static bool connectWiFi();
    static bool checkLastUpdateTime(const String& url, uint32_t localLastUpdate, const String& componentName);
    static bool needsConfigUpdate();
    static bool needsBikeRegistryUpdate();
    static bool needsBikeConfigsUpdate();
    static void updateConfigFromFirebase(const String& csvData);
    static bool downloadCentralConfig();
    static bool downloadBikeRegistryData();
    static bool downloadBikeConfigs();
    static bool uploadBufferData();
    static bool uploadPresenceData();
    static bool uploadHeartbeat();
    static bool uploadWiFiConfig();
    static bool uploadBikeData();

};