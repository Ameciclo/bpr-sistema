#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

class WiFiSync {
public:
    static void enter();
    static void update();
    static void exit();
    static bool connectWiFi();
    static void syncTime();
    static bool downloadConfig();
    static bool uploadData();
    static bool uploadHeartbeat();
    static bool uploadBikeConfigLogs();
    static bool validateFirebaseConfig(const DynamicJsonDocument& doc);
};