#pragma once
#include <Arduino.h>

class BLEOnly {
public:
    static void enter();
    static void update();
    static void exit();
    static uint8_t getConnectedBikes();
    static void sendHeartbeat();
};