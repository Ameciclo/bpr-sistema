/*
 * BPR Sistema - Firmware Bicicleta v2.0
 * Copyright (C) 2024 BPR Sistema Contributors
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 * 
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <LittleFS.h>
#include "constants.h"
#include "config_manager.h"
#include "buffer_manager.h"
#include "power_manager.h"
#include "at_base.h"
#include "scanning.h"
#include "lost.h"

// Global managers
ConfigManager configManager;
BufferManager bufferManager;
PowerManager powerManager;

// State handlers
AtBaseState* atBaseState = nullptr;
ScanningState* scanningState = nullptr;
LostState* lostState = nullptr;

// Current state
BikeState currentState = STATE_BOOT;

// Function declarations
void handleBoot();
void handleSleep();

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    
    if (!LittleFS.begin(true)) {
        Serial.println("🔧 LittleFS corrompido - formatando...");
        LittleFS.format();
        LittleFS.begin(true);
    }
    WiFi.mode(WIFI_STA);
    
    Serial.println("🚲 BPR Bici Modular v2.0");
    
    // Generate unique ID if needed
    configManager.generateUniqueId();
    
    // Initialize BLE with the bike_id
    NimBLEDevice::init(configManager.getConfig().bike_id);
    
    // Fluxo de inicialização (sem CONFIG_REQUEST):
    if (!configManager.load() || !configManager.isValid()) {
        // Central detecta necessidade e envia config
        Serial.println("⚠️ No config - central will detect and push");
    }
    
    // Load saved buffer if exists
    bufferManager.load();
    currentState = STATE_BOOT;
    
    // Initialize state handlers
    atBaseState = new AtBaseState(configManager, bufferManager);
    scanningState = new ScanningState(configManager, bufferManager);
    lostState = new LostState(configManager, bufferManager);
}

void loop() {
    BikeState nextState = currentState;
    
    // Check battery state first (FSD requirement)
    nextState = powerManager.checkBatteryState(currentState, configManager.getConfig());
    
    switch (currentState) {
        case STATE_BOOT:
            handleBoot();
            break;
            
        case STATE_SCANNING:
            nextState = scanningState->update();
            
            // FSD: Check if base detected during scanning
            if (atBaseState->scanForBase()) {
                nextState = STATE_AT_BASE;
            }
            break;
            
        case STATE_AT_BASE:
            nextState = atBaseState->update();
            break;
            
        case STATE_LOST:
            nextState = lostState->update();
            break;
            
        case STATE_SLEEP:
            handleSleep();
            break;
    }
    
    // State transition
    if (nextState != currentState) {
        Serial.printf("🔄 State transition: %d → %d\n", currentState, nextState);
        currentState = nextState;
    }
    
    delay(100);
}

void handleBoot() {
    Serial.println("🔄 BOOT");
    
    BikeConfig& config = configManager.getConfig();
    
    // Show current config
    Serial.println("📋 Current Configuration:");
    Serial.printf("   🆔 ID: %s (v%d)\n", config.bike_id, config.version);
    Serial.printf("   📡 WiFi: %ds interval, %dms timeout\n", config.wifi.scan_interval_sec, config.wifi.scan_timeout_ms);
    Serial.printf("   🔵 BLE: %ds scan, base='%s'\n", config.ble.scan_time_sec, config.ble.base_name);
    Serial.printf("   🔋 Battery: %.2fV critical, %.2fV low\n", config.battery.critical_voltage, config.battery.low_voltage);
    Serial.printf("   🛠️ Dev Mode: %s\n", config.dev_mode ? "ON" : "OFF");
    
    // Check battery
    powerManager.logBatteryStatus();
    
    if (powerManager.isCriticalBattery(config) && !config.dev_mode) {
        Serial.println("🔋 Critical battery - entering sleep\n");
        currentState = STATE_SLEEP;
        return;
    }
    
    // Always go to SCANNING (central detects config needs)
    bufferManager.startSession(config.bike_id);
    currentState = STATE_SCANNING;
}

void handleSleep() {
    Serial.println("💤 Entering deep sleep");
    
    // End current session and save
    bufferManager.endSession();
    bufferManager.save();
    
    // Deep sleep for configured duration
    BikeConfig& config = configManager.getConfig();
    powerManager.enterDeepSleep(config.power.deep_sleep_duration_sec);
}