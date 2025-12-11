#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include "config_manager.h"
#include "ble_simple.h"
#include "led_controller.h"
#include "state_machine.h"
#include "wifi_manager.h"
#include "firebase_manager.h"
#include "ntp_manager.h"

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
    if (!loadConfigCache()) {
        Serial.println("⚠️ Config cache não encontrado, usando padrões");
    }
    
    // Inicializar LED
    initLED();
    setLEDPattern(LED_BOOT);
    
    // Inicializar BLE
    if (!loadBLEConfig()) {
        Serial.println("⚠️ BLE config não encontrado, usando padrões");
    }
    
    if (!initBLE()) {
        Serial.println("❌ Falha na inicialização BLE");
        setLEDPattern(LED_ERROR);
        ESP.restart();
    }
    
    // Inicializar NTP
    initNTP();
    
    // Inicializar máquina de estados
    initStateMachine();
    
    Serial.println("✅ Central inicializada");
    setLEDPattern(LED_BLE_READY);
}

void loop() {
    // Atualizar LED
    updateLED();
    
    // Executar máquina de estados
    updateStateMachine();
    
    delay(100);
}