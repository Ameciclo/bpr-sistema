#pragma once
#include <Arduino.h>

class SelfCheck {
public:
    SelfCheck();
    static bool performCheck();
    static void printResults();
    static bool systemCheck();
    static bool checkMemory();
    static bool checkFileSystem();
    static bool checkLED();
    static bool checkWiFi();
    static bool checkBLE();
};