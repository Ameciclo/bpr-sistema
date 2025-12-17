#include "sync_monitor.h"
#include "config_manager.h"

extern ConfigManager configManager;

// Variáveis internas do módulo
static uint8_t syncFailureCount = 0;
static uint32_t firstFailureTime = 0;

namespace SyncMonitor {
    
    void recordFailure() {
        if (syncFailureCount == 0) {
            firstFailureTime = millis();
        }
        syncFailureCount++;
        
        const auto& config = configManager.getConfig().fallback;
        Serial.printf("❌ Sync failure %d/%d\n", syncFailureCount, config.max_failures);
    }
    
    void recordSuccess() {
        if (syncFailureCount > 0) {
            Serial.printf("✅ Sync recovered after %d failures\n", syncFailureCount);
        }
        syncFailureCount = 0;
        firstFailureTime = 0;
    }
    
    bool shouldFallback() {
        if (syncFailureCount == 0) return false;
        
        const auto& fallback = configManager.getConfig().fallback;
        
        if (syncFailureCount >= fallback.max_failures) {
            Serial.printf("⚠️ Max failures reached: %d\n", fallback.max_failures);
            return true;
        }
        
        uint32_t elapsed = millis() - firstFailureTime;
        uint32_t timeout = fallback.timeout_min * 60000;
        if (elapsed > timeout) {
            Serial.printf("⚠️ Failure timeout: %d min\n", fallback.timeout_min);
            return true;
        }
        
        return false;
    }
    
    void reset() {
        syncFailureCount = 0;
        firstFailureTime = 0;
    }
}