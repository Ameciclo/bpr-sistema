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

// Timers from config.json (loaded at initialization)
// Default values if config.json doesn't exist:
uint32_t checkin_interval_sec = 300;        // "dar oi" a cada 5min
uint32_t scan_interval_sec = 25;            // WiFi scan a cada 25s
uint32_t low_battery_scan_interval_sec = 120; // Scan mais lento se bateria baixa
uint8_t battery_critical_percent = 15;      // Entra em LOW_BATTERY
uint8_t battery_low_percent = 25;           // Reduz frequência de scans

// Function declarations
void handleBoot();
void handleConfigRequest();
void handleSleep();
void handleWakeCheck();
void handleLowBattery();
void loadTimersFromConfig();

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
    
    // Fluxo de inicialização:
    if (!configManager.load() || !configManager.isValid()) {
        // Qualquer bike sem config válida (nova OU corrompida)
        currentState = STATE_CONFIG_REQUEST;
    } else {
        // Carregar timers do config.json
        loadTimersFromConfig();
        // Load saved buffer if exists
        bufferManager.load();
        currentState = STATE_WAKE_CHECK;
    }
    
    // Initialize state handlers
    atBaseState = new AtBaseState(configManager, bufferManager);
    scanningState = new ScanningState(configManager, bufferManager);
    lostState = new LostState(configManager, bufferManager);
    
    // Update buffer size based on config
    bufferManager.setMaxRecords(configManager.getConfig().max_wifi_records);
}

void loop() {
    BikeState nextState = currentState;
    
    // Check battery state first
    nextState = powerManager.checkBatteryState(currentState, configManager.getConfig());
    
    switch (currentState) {
        case STATE_BOOT:
            handleBoot();
            break;
            
        case STATE_CONFIG_REQUEST:
            handleConfigRequest();
            break;
            
        case STATE_WAKE_CHECK:
            handleWakeCheck();
            break;
            
        case STATE_AT_BASE:
            nextState = atBaseState->update();
            break;
            
        case STATE_SCANNING:
            nextState = scanningState->update();
            
            if (atBaseState->scanForBase()) {
                nextState = STATE_DATA_UPLOAD;
            } else {
                // Se não encontrou base ou central ocupada, continuar scanning
                nextState = STATE_SCANNING;
            }
            break;
            
        case STATE_DATA_UPLOAD:
            nextState = atBaseState->update();
            if (nextState == STATE_AT_BASE) {
                // Data uploaded successfully, can sleep
                nextState = STATE_SLEEPING;
            }
            break;
            
        case STATE_LOW_BATTERY:
            handleLowBattery();
            break;
            
        case STATE_SLEEPING:
            // This should trigger deep sleep
            powerManager.enterDeepSleep(powerManager.getSleepDuration(configManager.getConfig()));
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
    
    Config& config = configManager.getConfig();
    
    // Show current config
    Serial.println("📋 Current Configuration:");
    Serial.printf("   🆔 ID: %s (v%d)\n", config.bike_id, config.version);
    Serial.printf("   📡 WiFi Scan: %ds interval, %dms timeout\n", config.scan_interval_sec, config.wifi_scan_timeout_ms);
    Serial.printf("   🔵 BLE: %ds scan, base='%s'\n", config.ble_scan_time_sec, config.base_ble_name);
    Serial.printf("   🔋 Battery: %.2fV critical, %.2fV low\n", config.battery_critical_voltage, config.min_battery_voltage);
    Serial.printf("   🛠️ Dev Mode: %s\n", config.dev_mode ? "ON" : "OFF");
    
    // Check battery
    powerManager.logBatteryStatus();
    
    if (powerManager.isCriticalBattery(config) && !config.dev_mode) {
        Serial.println("🔋 Bateria crítica - entering LOW_BATTERY state");
        currentState = STATE_LOW_BATTERY;
        return;
    } else if (powerManager.isCriticalBattery(config) && config.dev_mode) {
        Serial.println("🛠️ DEV MODE: Ignoring low battery");
    }
    
    currentState = STATE_WAKE_CHECK;
}

void handleWakeCheck() {
    Serial.println("🔄 WAKE_CHECK - Verifying if at base");
    
    // Try to find base
    if (atBaseState->scanForBase()) {
        currentState = STATE_DATA_UPLOAD;
    } else {
        currentState = STATE_SCANNING;
    }
}

void handleLowBattery() {
    Serial.println("😨 LOW_BATTERY - Emergency mode");
    
    BikeState nextState = powerManager.handleLowBatteryState(configManager.getConfig());
    if (nextState != STATE_LOW_BATTERY) {
        currentState = nextState;
    }
}

void loadTimersFromConfig() {
    Config& config = configManager.getConfig();
    
    // Load intervals from config
    checkin_interval_sec = config.checkin_interval_sec;
    scan_interval_sec = config.scan_interval_normal_sec;
    low_battery_scan_interval_sec = config.scan_interval_low_battery_sec;
    battery_critical_percent = config.battery_critical_percent;
    battery_low_percent = config.battery_low_percent;
    
    Serial.println("⚙️ Timers loaded from config:");
    Serial.printf("   Checkin: %ds, Scan: %ds, Low battery scan: %ds\n", 
                  checkin_interval_sec, scan_interval_sec, low_battery_scan_interval_sec);
    Serial.printf("   Battery thresholds: %d%% critical, %d%% low\n", 
                  battery_critical_percent, battery_low_percent);
}

void handleConfigRequest() {
    static unsigned long lastScan = 0;
    
    Serial.println("🔍 CONFIG REQUEST - Searching for BLE base to get configuration...");
    
    // Search for base every 5 seconds
    if (millis() - lastScan > 5000) {
        if (atBaseState->scanForBase()) {
            Serial.println("✅ Base found! Requesting configuration...");
            if (atBaseState->requestConfig()) {
                Serial.println("✅ Configuration received and saved!");
                currentState = STATE_BOOT;
                return;
            } else {
                Serial.println("❌ Failed to get configuration from base");
            }
        }
        lastScan = millis();
    }
    
    // Fallback: button to use default configuration
    if (digitalRead(BUTTON_PIN) == LOW) {
        delay(100);
        if (digitalRead(BUTTON_PIN) == LOW) {
            Serial.println("🔧 Using default configuration (button pressed)");
            configManager.save();
            currentState = STATE_BOOT;
        }
    }
    
    delay(1000);
}

void handleSleep() {
    Serial.println("💤 Deep sleep for configured duration");
    
    // Save buffer
    bufferManager.save();
    
    // Use power manager for sleep
    powerManager.enterDeepSleep(powerManager.getSleepDuration(configManager.getConfig()));
}