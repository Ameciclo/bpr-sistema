#include "state_machine.h"
#include "constants.h"
#include "config_ap.h"
#include "ble_only.h"
#include "wifi_sync.h"
#include "shutdown.h"

StateMachine::StateMachine() : currentState(STATE_BOOT), lastStateChange(0) {}

void StateMachine::setState(SystemState newState) {
    if (currentState != newState) {
        Serial.printf("🔄 State: %s -> %s\n", 
                     getStateName(currentState), 
                     getStateName(newState));
        
        exitState(currentState);
        currentState = newState;
        lastStateChange = millis();
        enterState(newState);
    }
}

void StateMachine::update() {
    switch (currentState) {
        case STATE_CONFIG_AP:
            ConfigAP::update();
            break;
        case STATE_BLE_ONLY:
            BLEOnly::update();
            break;
        case STATE_WIFI_SYNC:
            WiFiSync::update();
            break;
        case STATE_SHUTDOWN:
            Shutdown::update();
            break;
        default:
            break;
    }
    
    checkTransitions();
}

void StateMachine::handleEvent(SystemEvent event) {
    switch (event) {
        case EVENT_CONFIG_COMPLETE:
            if (currentState == STATE_CONFIG_AP) {
                setState(STATE_BLE_ONLY);
            }
            break;
            
        case EVENT_SYNC_TRIGGER:
            if (currentState == STATE_BLE_ONLY) {
                setState(STATE_WIFI_SYNC);
            }
            break;
            
        case EVENT_SYNC_COMPLETE:
            if (currentState == STATE_WIFI_SYNC) {
                setState(STATE_BLE_ONLY);
            }
            break;
            
        case EVENT_INACTIVITY_TIMEOUT:
            if (currentState == STATE_BLE_ONLY) {
                setState(STATE_SHUTDOWN);
            }
            break;
            
        case EVENT_WAKE_UP:
            if (currentState == STATE_SHUTDOWN) {
                setState(STATE_BLE_ONLY);
            }
            break;
            
        case EVENT_ERROR:
            Serial.println("❌ System Error Event");
            break;
    }
}

void StateMachine::enterState(SystemState state) {
    switch (state) {
        case STATE_CONFIG_AP:
            ConfigAP::enter();
            break;
        case STATE_BLE_ONLY:
            BLEOnly::enter();
            break;
        case STATE_WIFI_SYNC:
            WiFiSync::enter();
            break;
        case STATE_SHUTDOWN:
            Shutdown::enter();
            break;
        default:
            break;
    }
}

void StateMachine::exitState(SystemState state) {
    switch (state) {
        case STATE_CONFIG_AP:
            ConfigAP::exit();
            break;
        case STATE_BLE_ONLY:
            BLEOnly::exit();
            break;
        case STATE_WIFI_SYNC:
            WiFiSync::exit();
            break;
        case STATE_SHUTDOWN:
            Shutdown::exit();
            break;
        default:
            break;
    }
}

void StateMachine::checkTransitions() {
    uint32_t stateTime = millis() - lastStateChange;
    
    // Auto transitions based on time
    if (currentState == STATE_BLE_ONLY && stateTime > SHUTDOWN_TIMEOUT) {
        if (BLEOnly::getConnectedBikes() == 0) {
            handleEvent(EVENT_INACTIVITY_TIMEOUT);
        }
    }
}

const char* StateMachine::getStateName(SystemState state) {
    switch (state) {
        case STATE_BOOT: return "BOOT";
        case STATE_CONFIG_AP: return "CONFIG_AP";
        case STATE_BLE_ONLY: return "BLE_ONLY";
        case STATE_WIFI_SYNC: return "WIFI_SYNC";
        case STATE_SHUTDOWN: return "SHUTDOWN";
        default: return "UNKNOWN";
    }
}