#pragma once
#include <Arduino.h>

class BikePairing {
public:
    static void enter();
    static void update();
    static void exit();
    static uint8_t getConnectedBikes();
    static void sendHeartbeat();
};