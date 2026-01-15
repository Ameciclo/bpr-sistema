#ifndef LOST_H
#define LOST_H

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "constants.h"
#include "config_manager.h"
#include "buffer_manager.h"

class LostState {
private:
    ConfigManager& configManager;
    BufferManager& bufferManager;
    unsigned long searchStartTime;

public:
    LostState(ConfigManager& configMgr, BufferManager& bufferMgr);
    BikeState update();
};

#endif