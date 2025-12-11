#pragma once
#include <Arduino.h>
#include "constants.h"

enum SystemState {
    STATE_BOOT,
    STATE_CONFIG_AP,
    STATE_BLE_ONLY,
    STATE_WIFI_SYNC,
    STATE_SHUTDOWN
};

class StateMachine {
public:
    StateMachine();
    void setState(SystemState newState);
    void update();
    void handleEvent(SystemEvent event);
    
    SystemState getCurrentState() const { return currentState; }
    uint32_t getStateTime() const { return millis() - lastStateChange; }
    
    static const char* getStateName(SystemState state);

private:
    SystemState currentState;
    uint32_t lastStateChange;
    
    void enterState(SystemState state);
    void exitState(SystemState state);
    void checkTransitions();
};