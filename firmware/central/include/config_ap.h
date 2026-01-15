#pragma once
#include <Arduino.h>

class ConfigAP {
public:
    static void enter(bool isInitialMode = false);
    static void update();
    static void exit();
    static void printStatus();
    static void setupWebServer();

};