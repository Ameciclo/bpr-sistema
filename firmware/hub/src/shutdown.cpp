#include "shutdown.h"
#include <esp_sleep.h>
#include "constants.h"
#include "config_manager.h"
#include "buffer_manager.h"
#include "led_controller.h"
#include "state_machine.h"
#include "state_machine.h"

extern ConfigManager configManager;
extern BufferManager bufferManager;
extern LEDController ledController;
extern StateMachine stateMachine;

void Shutdown::enter() {
    Serial.println("💤 Entering SHUTDOWN mode");
    
    // Save current state
    bufferManager.saveState();
    configManager.saveConfig();
    
    // Turn off LED
    ledController.off();
    
    // Configure wake up timer (30 minutes)
    esp_sleep_enable_timer_wakeup(30 * 60 * 1000000ULL); // 30 min in microseconds
    
    Serial.println("😴 Going to light sleep...");
    Serial.flush();
    
    // Light sleep (can wake up from BLE or timer)
    esp_light_sleep_start();
    
    Serial.println("🌅 Woke up from sleep");
    stateMachine.handleEvent(EVENT_WAKE_UP);
}

void Shutdown::update() {
    // This should not be called as we're in sleep mode
    // If we reach here, something went wrong
    Serial.println("⚠️ Shutdown update called - should not happen");
    stateMachine.handleEvent(EVENT_WAKE_UP);
}

void Shutdown::exit() {
    Serial.println("🔚 Exiting SHUTDOWN mode");
    ledController.bootPattern();
}