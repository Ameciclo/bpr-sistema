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
    static void retry();  // F14: re-arma a tentativa sem depender de changeState
    static void printStatus();

private:
    static bool syncInProgress;
    static SyncResult currentResult;

    static bool connectWiFi();
    static bool checkLastUpdateTime(const String& url, uint32_t localLastUpdate,
                                    const String& componentName, uint32_t* remoteOut = nullptr);
    static bool needsConfigUpdate(uint32_t* remoteVersion = nullptr);
    static bool needsBikeRegistryUpdate(uint32_t* remoteVersion = nullptr);
    static void updateConfigFromFirebase(const String& csvData, uint32_t remoteVersion);
    static bool downloadCentralConfig();
    static bool downloadBikeRegistryData();
    static bool downloadBikeConfigs();
    static bool uploadBufferData();
    static bool uploadPendingBikes(); // R15: publica descobertas locais
    static bool uploadHeartbeat();
};
