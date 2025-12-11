#pragma once
#include <Arduino.h>

class WiFiSync {
public:
    static void enter();
    static void update();
    static void exit();
    static bool connectWiFi();
    static void syncTime();
    static void downloadConfig();
    static void uploadData();
    static void uploadHeartbeat();
};