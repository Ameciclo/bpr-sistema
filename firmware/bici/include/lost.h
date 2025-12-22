#ifndef LOST_H
#define LOST_H

#include <Arduino.h>
#include "constants.h"
#include "config_manager.h"
#include "buffer_manager.h"

class LostState {
private:
    ConfigManager& configManager;
    BufferManager& bufferManager;

public:
    LostState(ConfigManager& configMgr, BufferManager& bufferMgr);
    BikeState update();
};

#endif