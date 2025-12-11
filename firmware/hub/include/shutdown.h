#pragma once
#include <Arduino.h>

class Shutdown {
public:
    static void enter();
    static void update();
    static void exit();
};