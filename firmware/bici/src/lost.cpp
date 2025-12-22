#include "lost.h"

LostState::LostState(ConfigManager& configMgr, BufferManager& bufferMgr) 
    : configManager(configMgr), bufferManager(bufferMgr) {}

BikeState LostState::update() {
    // TODO: Implement lost state logic
    // This state would handle scenarios where:
    // - Bike hasn't connected to base for extended period
    // - Emergency recovery procedures
    // - Fallback communication methods
    
    Serial.println("🔍 LOST state - not implemented yet");
    delay(5000);
    
    // For now, return to scanning
    return STATE_SCANNING;
}