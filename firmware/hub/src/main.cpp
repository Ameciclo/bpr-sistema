#include <Arduino.h>
#include <LittleFS.h>
#include "config_manager.h"
#include "constants.h"
#include "config_ap.h"
#include "ble_only.h"
#include "wifi_sync.h"
#include "led_controller.h"
#include "buffer_manager.h"
#include "self_check.h"
#include "sync_monitor.h"

// Instâncias globais
ConfigManager configManager;
LEDController ledController;
BufferManager bufferManager;

// State machine variables
SystemState currentState = STATE_BOOT;
uint32_t stateStartTime = 0;
bool firstSync = false;

// Variáveis globais
unsigned long lastHeartbeat = 0;
bool isInitialConfigMode = false;

// Declarações de funções
void printStatus();
void sendHeartbeat();
void changeState(SystemState newState);
void handleSyncResult(SyncResult result);
const char *getStateName(SystemState state);

void setup()
{
    Serial.begin(115200);

    Serial.println("\n🏢 BPR Hub Station v1.0");
    Serial.println("========================");

    // Inicializar LittleFS
    if (!LittleFS.begin())
    {
        Serial.println("❌ Falha no LittleFS");
        ESP.restart();
    }

    // Self-check do sistema
    SelfCheck selfCheck;
    if (!selfCheck.systemCheck())
    {
        Serial.println("⚠️ System check failed - continuing anyway");
    }

    // Inicializar módulos
    bool configLoaded = configManager.loadConfig();
    bufferManager.begin();
    ledController.begin();
    ledController.bootPattern();

    // Verificar se precisa de configuração
    if (!configLoaded || !configManager.isConfigValid())
    {
        isInitialConfigMode = true;
        changeState(STATE_CONFIG_AP);
    }

    // Se config é válida, forçar sync inicial
    if (configLoaded && configManager.isConfigValid())
    {
        Serial.println("🔄 Iniciando sync obrigatório para validar configuração...");
        firstSync = true;
        changeState(STATE_WIFI_SYNC);
    }

    Serial.println("✅ Hub inicializado");
}

void loop()
{
    static unsigned long lastStatusPrint = 0;

    // Atualizar módulos
    ledController.update();

    // Update do estado atual
    switch (currentState)
    {
    case STATE_CONFIG_AP:
        ConfigAP::update();
        break;
    case STATE_BLE_ONLY:
        BLEOnly::update();
        break;
    case STATE_WIFI_SYNC:
        WiFiSync::update();
        // Check for timeout
        if (millis() - stateStartTime > configManager.getConfig().timeouts.wifi_sec * 1000) {
            Serial.println("⏰ WiFi sync timeout");
            handleSyncResult(SyncResult::FAILURE);
        }
        break;
    default:
        break;
    }

    // Fallback por falhas de sync
    if (currentState == STATE_BLE_ONLY && SyncMonitor::shouldFallback())
    {
        Serial.println("⚠️ Fallback to AP mode");
        isInitialConfigMode = false;
        changeState(STATE_CONFIG_AP);
    }

    // Status periódico (30s)
    if (millis() - lastStatusPrint > 30000)
    {
        printStatus();
        lastStatusPrint = millis();
    }

    // DEBUG
    sendHeartbeat();
}

void sendHeartbeat()
{
    if (millis() - lastHeartbeat > 60000)
    { // 1 min
        int bikes = (currentState == STATE_BLE_ONLY) ? BLEOnly::getConnectedBikes() : 0;
        Serial.printf("💓 Heartbeat - Bikes: %d, Heap: %d\n",
                      bikes, ESP.getFreeHeap());
        lastHeartbeat = millis();
    }
}

void changeState(SystemState newState)
{
    if (currentState == newState)
        return;

    Serial.printf("🔄 %s -> %s\n", getStateName(currentState), getStateName(newState));

    // Exit current state
    switch (currentState)
    {
    case STATE_CONFIG_AP:
        ConfigAP::exit();
        break;
    case STATE_BLE_ONLY:
        BLEOnly::exit();
        break;
    case STATE_WIFI_SYNC:
        WiFiSync::exit();
        break;
    default:
        break;
    }

    currentState = newState;
    stateStartTime = millis();

    // Enter new state
    switch (newState)
    {
    case STATE_CONFIG_AP:
        SyncMonitor::reset();
        ConfigAP::enter(isInitialConfigMode);
        break;
    case STATE_BLE_ONLY:
        BLEOnly::enter();
        break;
    case STATE_WIFI_SYNC:
        {
            SyncResult result = WiFiSync::enter();
            handleSyncResult(result);
        }
        break;
    default:
        break;
    }
}

const char *getStateName(SystemState state)
{
    switch (state)
    {
    case STATE_BOOT:
        return "BOOT";
    case STATE_CONFIG_AP:
        return "CONFIG_AP";
    case STATE_BLE_ONLY:
        return "BLE_ONLY";
    case STATE_WIFI_SYNC:
        return "WIFI_SYNC";
    default:
        return "UNKNOWN";
    }
}

void printStatus()
{
    Serial.println("==================================================");
    Serial.printf("🏢 %s | Estado: %s | Uptime: %lus\n",
                  configManager.getConfig().base_id,
                  getStateName(currentState),
                  millis() / 1000);

    if (currentState == STATE_CONFIG_AP)
    {
        Serial.println("📱 Modo Configuração Ativo:");
        Serial.println("   WiFi: BPR_Hub_Config (senha: botaprarodar)");
        Serial.println("   URL: http://192.168.4.1");
    }
    else
    {
        int bikes = (currentState == STATE_BLE_ONLY) ? BLEOnly::getConnectedBikes() : 0;
        Serial.printf("🚲 Bikes conectadas: %d | 💾 Heap: %d bytes\n",
                      bikes, ESP.getFreeHeap());

        // Mostrar informações de sincronização
        if (currentState == STATE_BLE_ONLY)
        {
            uint32_t stateTime = millis() - stateStartTime;
            uint32_t syncInterval = configManager.getConfig().sync_interval_ms();
            uint32_t nextSync = (syncInterval - stateTime) / 1000;

            if (stateTime < syncInterval)
            {
                Serial.printf("🔄 Próxima sync em: %lus\n", nextSync);
            }
            else
            {
                Serial.println("🔄 Sync pendente...");
            }
        }
    }

    Serial.printf("⏱️ Estado há: %lus\n",
                  (millis() - stateStartTime) / 1000);
    Serial.println("==================================================");
}

void handleSyncResult(SyncResult result) {
    switch(result) {
        case SyncResult::SUCCESS:
            Serial.println("✅ Sync successful - transitioning to BLE_ONLY");
            firstSync = false;
            changeState(STATE_BLE_ONLY);
            break;
            
        case SyncResult::FAILURE:
            if (firstSync) {
                Serial.println("🚨 ERRO CRÍTICO: Primeiro sync falhou!");
                Serial.println("   - Não foi possível baixar configurações do Firebase");
                Serial.println("   - Sistema não pode funcionar sem config válida");
                Serial.println("   - Retornando ao modo CONFIG_AP para reconfigurar");
                firstSync = false;
                changeState(STATE_CONFIG_AP);
            } else {
                Serial.println("⚠️ Sync falhou - continuando com última config válida");
                changeState(STATE_BLE_ONLY);
            }
            break;
    }
}