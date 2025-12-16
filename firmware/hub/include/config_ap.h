#pragma once
#include <Arduino.h>

class ConfigAP {
public:
    static void enter();
    static void update();
    static void exit();
    static void setupWebServer();
    static bool tryUpdateWiFiInFirebase();
};