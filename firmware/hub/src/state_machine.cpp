#include "state_machine.h"
#include "constants.h"
#include "config_ap.h"
#include "ble_only.h"
#include "wifi_sync.h"
// #include "shutdown.h" - removido
#include "config_manager.h"

extern ConfigManager configManager;

StateMachine::StateMachine() : currentState(STATE_BOOT), lastStateChange(0), firstSync(false), syncFailureCount(0), firstFailureTime(0) {}

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
            // SHUTDOWN removido - não usado
            break;
            
        case EVENT_WAKE_UP:
            // SHUTDOWN removido - não usado
            break;
            
        case EVENT_ERROR:
            Serial.println("❌ System Error Event");
            break;
    }
}

void StateMachine::enterState(SystemState state) {
    switch (state) {
        case STATE_CONFIG_AP:
            // Reset failure counter when entering AP mode
            syncFailureCount = 0;
            firstFailureTime = 0;
            ConfigAP::enter();
            break;
        case STATE_BLE_ONLY:
            BLEOnly::enter();
            break;
        case STATE_WIFI_SYNC:
            WiFiSync::enter();
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
        default:
            break;
    }
}

void StateMachine::checkTransitions() {
    uint32_t stateTime = millis() - lastStateChange;
    
    // Check fallback to AP due to sync failures
    if (currentState == STATE_BLE_ONLY && shouldFallbackToAP()) {
        Serial.println("⚠️ Muitas falhas de sync - retornando ao modo AP");
        setState(STATE_CONFIG_AP);
        return;
    }
    
    // Auto transitions removidas - SHUTDOWN não usado
}

void StateMachine::recordSyncFailure() {
    if (syncFailureCount == 0) {
        firstFailureTime = millis();
    }
    syncFailureCount++;
    Serial.printf("❌ Falha de sync %d/%d\n", syncFailureCount, configManager.getConfig().fallback.max_failures);
}

void StateMachine::recordSyncSuccess() {
    if (syncFailureCount > 0) {
        Serial.printf("✅ Sync recuperada após %d falhas\n", syncFailureCount);
    }
    syncFailureCount = 0;
    firstFailureTime = 0;
}

bool StateMachine::shouldFallbackToAP() {
    if (syncFailureCount == 0) return false;
    
    const FallbackConfig& fallback = configManager.getConfig().fallback;
    
    // Fallback por número de falhas
    if (syncFailureCount >= fallback.max_failures) {
        Serial.printf("⚠️ %d falhas consecutivas atingidas\n", fallback.max_failures);
        return true;
    }
    
    // Fallback por tempo
    uint32_t timeoutMs = fallback.timeout_min * 60000;
    if (millis() - firstFailureTime > timeoutMs) {
        Serial.printf("⚠️ Timeout de falhas atingido: %d min\n", fallback.timeout_min);
        return true;
    }
    
    return false;
}

const char* StateMachine::getStateName(SystemState state) {
    switch (state) {
        case STATE_BOOT: return "BOOT";
        case STATE_CONFIG_AP: return "CONFIG_AP";
        case STATE_BLE_ONLY: return "BLE_ONLY";
        case STATE_WIFI_SYNC: return "WIFI_SYNC";
        default: return "UNKNOWN";
    }
}