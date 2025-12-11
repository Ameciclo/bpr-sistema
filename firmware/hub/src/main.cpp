#include <Arduino.h>
#include <LittleFS.h>
#include "config_manager.h"
#include "state_machine.h"
#include "led_controller.h"
#include "ble_simple.h"
#include "buffer_manager.h"

// Instâncias globais
ConfigManager configManager;
StateMachine stateMachine;
LEDController ledController;
BufferManager bufferManager;

// Variáveis globais
int connectedBikes = 0;
unsigned long lastHeartbeat = 0;

// Declarações de funções
void printStatus();
void sendHeartbeat();

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n🏢 BPR Hub Station v2.0");
    Serial.println("========================");
    
    // Inicializar LittleFS
    if (!LittleFS.begin()) {
        Serial.println("❌ Falha no LittleFS");
        ESP.restart();
    }
    
    // Inicializar módulos
    configManager.loadConfig();
    ledController.begin();
    ledController.bootPattern();
    
    // Inicializar BLE
    if (!initBLESimple()) {
        Serial.println("❌ Falha no BLE");
        ESP.restart();
    }
    startBLEServer();
    
    // Iniciar máquina de estados
    stateMachine.setState(STATE_BLE_ONLY);
    ledController.bleReadyPattern();
    
    Serial.println("✅ Hub inicializado");
}

void loop() {
    static unsigned long lastStatusPrint = 0;
    
    // Atualizar módulos
    ledController.update();
    stateMachine.update();
    
    // Processar BLE
    connectedBikes = getConnectedClients();
    
    // Verificar transições
    if (stateMachine.getCurrentState() == STATE_BLE_ONLY && 
        stateMachine.getStateTime() > configManager.getConfig().sync_interval_ms) {
        stateMachine.setState(STATE_WIFI_SYNC);
        ledController.syncPattern();
    }
    
    // Status periódico (30s)
    if (millis() - lastStatusPrint > 30000) {
        printStatus();
        lastStatusPrint = millis();
    }
    
    delay(100);
}

void sendHeartbeat() {
    if (millis() - lastHeartbeat > 60000) { // 1 min
        Serial.printf("💓 Heartbeat - Bikes: %d, Heap: %d\n", 
                     connectedBikes, ESP.getFreeHeap());
        lastHeartbeat = millis();
    }
}

void printStatus() {
    Serial.println("==================================================");
    Serial.printf("🏢 %s | Estado: %s | Uptime: %lus\n", 
                 configManager.getConfig().base_id, 
                 stateMachine.getStateName(stateMachine.getCurrentState()), 
                 millis() / 1000);
    Serial.printf("🚲 Bikes conectadas: %d | 💾 Heap: %d bytes\n", 
                 connectedBikes, ESP.getFreeHeap());
    Serial.printf("⏱️ Estado há: %lus\n", 
                 stateMachine.getStateTime() / 1000);
    Serial.println("==================================================");
}