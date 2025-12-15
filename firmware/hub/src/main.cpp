#include <Arduino.h>
#include <LittleFS.h>
#include "config_manager.h"
#include "state_machine.h"
#include "led_controller.h"
#include "ble_only.h"
#include "buffer_manager.h"
#include "self_check.h"

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
    
    // Self-check do sistema
    SelfCheck selfCheck;
    if (!selfCheck.systemCheck()) {
        Serial.println("⚠️ System check failed - continuing anyway");
    }
    
    // Inicializar módulos
    bool configLoaded = configManager.loadConfig();
    bufferManager.begin();
    ledController.begin();
    ledController.bootPattern();
    
    // Verificar se precisa de configuração
    if (!configLoaded || !configManager.isConfigValid()) {
        Serial.println("🔧 Config inválida, entrando no modo AP");
        Serial.println("📱 Conecte-se ao WiFi: BPR_Hub_Config (senha: botaprarodar)");
        Serial.println("🌐 Acesse: http://192.168.4.1 para configurar");
        Serial.println("⏰ Timeout: 15 minutos");
        stateMachine.setState(STATE_CONFIG_AP);
    } else {
        // Forçar sync inicial para validar configuração
        Serial.println("🔄 Iniciando sync obrigatório para validar configuração...");
        stateMachine.setFirstSync(true);
        stateMachine.setState(STATE_WIFI_SYNC);
        ledController.syncPattern();
    }
    
    Serial.println("✅ Hub inicializado");
}

void loop() {
    static unsigned long lastStatusPrint = 0;
    
    // Atualizar módulos
    ledController.update();
    stateMachine.update();
    
    // Atualizar contadores
    if (stateMachine.getCurrentState() == STATE_BLE_ONLY) {
        connectedBikes = BLEOnly::getConnectedBikes();
    }
    
    // Verificar transições
    if (stateMachine.getCurrentState() == STATE_BLE_ONLY && 
        stateMachine.getStateTime() > configManager.getConfig().sync_interval_ms()) {
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
    
    if (stateMachine.getCurrentState() == STATE_CONFIG_AP) {
        Serial.println("📱 Modo Configuração Ativo:");
        Serial.println("   WiFi: BPR_Hub_Config (senha: botaprarodar)");
        Serial.println("   URL: http://192.168.4.1");
    } else {
        Serial.printf("🚲 Bikes conectadas: %d | 💾 Heap: %d bytes\n", 
                     connectedBikes, ESP.getFreeHeap());
        
        // Mostrar informações de sincronização
        if (stateMachine.getCurrentState() == STATE_BLE_ONLY) {
            uint32_t stateTime = stateMachine.getStateTime();
            uint32_t syncInterval = configManager.getConfig().sync_interval_ms();
            uint32_t nextSync = (syncInterval - stateTime) / 1000;
            
            if (stateTime < syncInterval) {
                Serial.printf("🔄 Próxima sync em: %lus\n", nextSync);
            } else {
                Serial.println("🔄 Sync pendente...");
            }
        }
    }
    
    Serial.printf("⏱️ Estado há: %lus\n", 
                 stateMachine.getStateTime() / 1000);
    Serial.println("==================================================");
}