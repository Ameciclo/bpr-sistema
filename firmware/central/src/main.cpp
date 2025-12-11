#include <Arduino.h>
#include <LittleFS.h>
#include "config_manager.h"
#include "ble_simple.h"
#include "led_controller.h"
#include "state_machine.h"

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n🏢 BPR Central Station v2.0");
    Serial.println("============================");
    
    // Inicializar LittleFS
    if (!LittleFS.begin()) {
        Serial.println("❌ Falha no LittleFS");
        ESP.restart();
    }
    
    // Carregar configurações
    loadConfigCache();
    
    // Inicializar LED
    initLED();
    setLEDPattern(LED_BOOT);
    
    // Inicializar BLE
    initBLESimple();
    startBLEServer();
    
    // Inicializar modo
    currentMode = MODE_BLE_ONLY;
    modeStart = millis();
    
    setLEDPattern(LED_BLE_READY);
    Serial.println("✅ Central inicializada");
}

void loop() {
    updateLED();
    
    switch (currentMode) {
        case MODE_BLE_ONLY:
            handleBLEMode();
            break;
        case MODE_WIFI_SYNC:
            handleWiFiMode();
            break;
        case MODE_SHUTDOWN:
            handleShutdownMode();
            break;
        default:
            currentMode = MODE_BLE_ONLY;
            break;
    }
    
    delay(100);
}