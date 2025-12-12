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
    void recordSyncFailure();
    void recordSyncSuccess();
    
    SystemState getCurrentState() const { return currentState; }
    uint32_t getStateTime() const { return millis() - lastStateChange; }
    bool isFirstSync() const { return firstSync; }
    void setFirstSync(bool value) { firstSync = value; }
    
    static const char* getStateName(SystemState state);

private:
    SystemState currentState;
    uint32_t lastStateChange;
    bool firstSync;
    uint8_t syncFailureCount;
    uint32_t firstFailureTime;
    
    void enterState(SystemState state);
    void exitState(SystemState state);
    void checkTransitions();
    bool shouldFallbackToAP();
};