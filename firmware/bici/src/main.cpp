#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <LittleFS.h>
#include "constants.h"
#include "config_manager.h"
#include "buffer_manager.h"
#include "at_base.h"
#include "scanning.h"
#include "lost.h"

// Global managers
ConfigManager configManager;
BufferManager bufferManager;

// State handlers
AtBaseState* atBaseState = nullptr;
ScanningState* scanningState = nullptr;
LostState* lostState = nullptr;

// Current state
BikeState currentState = STATE_BOOT;

// Function declarations
void handleBoot();
void handleConfigRequest();
void handleSleep();
float getBatteryVoltage();

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
    
    // Load config
    if (!configManager.load()) {
        Serial.println("⚙️ No config found - entering CONFIG_REQUEST");
        currentState = STATE_CONFIG_REQUEST;
    } else {
        // Load saved buffer if exists
        bufferManager.load();
        currentState = STATE_BOOT;
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
    
    switch (currentState) {
        case STATE_BOOT:
            handleBoot();
            break;
            
        case STATE_CONFIG_REQUEST:
            handleConfigRequest();
            break;
            
        case STATE_AT_BASE:
            nextState = atBaseState->update();
            break;
            
        case STATE_SCANNING:
            nextState = scanningState->update();
            
            // Check if base is available
            if (atBaseState->scanForBase()) {
                nextState = STATE_AT_BASE;
            }
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
    float voltage = getBatteryVoltage();
    Serial.printf("🔋 Battery: %.2fV (min=%.2fV)\n", voltage, config.battery_critical_voltage);
    
    if (voltage < config.battery_critical_voltage && !config.dev_mode) {
        Serial.println("🔋 Bateria crítica - sleep");
        currentState = STATE_SLEEP;
        return;
    } else if (voltage < config.battery_critical_voltage && config.dev_mode) {
        Serial.println("🛠️ DEV MODE: Ignoring low battery");
    }
    
    // Try to find base
    if (atBaseState->scanForBase()) {
        currentState = STATE_AT_BASE;
    } else {
        currentState = STATE_SCANNING;
    }
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
    Serial.println("💤 Deep sleep for 1 hour");
    
    // Save buffer
    bufferManager.save();
    
    // Deep sleep
    esp_sleep_enable_timer_wakeup(3600 * 1000000ULL); // 1 hour
    esp_deep_sleep_start();
}

float getBatteryVoltage() {
    static unsigned long lastRead = 0;
    static float lastVoltage = 4.0;
    
    if (millis() - lastRead < 5000) {
        return lastVoltage;
    }
    
    int adc = analogRead(BATTERY_PIN);
    lastRead = millis();
    
    if (adc < 1500) {
        if (lastVoltage != 4.0) {
            Serial.printf("⚠️ ADC=%d - USB power (4.0V)\n", adc);
        }
        lastVoltage = 4.0;
        return 4.0;
    }
    
    lastVoltage = (adc / 4095.0) * 3.3 * 2.0;
    return lastVoltage;
}