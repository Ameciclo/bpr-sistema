#include <Arduino.h>
#include <LittleFS.h>
#include "config_manager.h"
#include "constants.h"
#include "config_ap.h"
#include "bike_pairing.h"
#include "cloud_sync.h"
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

// Variáveis globais
unsigned long lastHeartbeat = 0;

// Controle de sync
static uint32_t lastSyncCheck = 0;
static uint8_t syncFailureCount = 0;
static uint32_t tempConfigApStartTime = 0;

void changeState(SystemState newState)
{
    if (currentState == newState)
        return;

    Serial.printf("🔄 %s -> %s\n", getStateName(currentState), getStateName(newState));

    // Exit current state
    switch (currentState)
    {
    case STATE_INITIAL_CONFIG_AP:
    case STATE_TEMP_CONFIG_AP:
        ConfigAP::exit();
        break;
    case STATE_BIKE_PAIRING:
        BikePairing::exit();
        break;
    case STATE_INITIAL_SYNC:
    case STATE_CLOUD_SYNC:
        CloudSync::exit();
        break;
    default:
        break;
    }

    currentState = newState;
    stateStartTime = millis();

    // Enter new state
    switch (newState)
    {
    case STATE_INITIAL_CONFIG_AP:
        ledController.configPattern();
        SyncMonitor::reset();
        ConfigAP::enter(true);  // Modo obrigatório
        break;
    case STATE_TEMP_CONFIG_AP:
        ledController.configPattern();
        SyncMonitor::reset();
        ConfigAP::enter(false); // Modo temporário
        tempConfigApStartTime = millis();
        break;
    case STATE_BIKE_PAIRING:
        ledController.pairingPattern();
        BikePairing::enter();
        break;
    case STATE_INITIAL_SYNC:
    case STATE_CLOUD_SYNC:
        ledController.syncPattern();
        CloudSync::enter();
        break;
    default:
        break;
    }
}

void printStatus()
{
    Serial.println("==================================================");
    Serial.printf("🏢 %s | Estado: %s | Uptime: %lus\n",
                  configManager.getConfig().base_id,
                  getStateName(currentState),
                  millis() / 1000);

    switch (currentState)
    {
    case STATE_BOOT:
        Serial.printf("💾 Heap: %d bytes\n", ESP.getFreeHeap());
        break;
    case STATE_INITIAL_CONFIG_AP:
    case STATE_TEMP_CONFIG_AP:
        ConfigAP::printStatus();
        break;
    case STATE_BIKE_PAIRING:
        BikePairing::printStatus();
        break;
    case STATE_INITIAL_SYNC:
    case STATE_CLOUD_SYNC:
        CloudSync::printStatus();
        break;
    default:
        Serial.printf("⚠️ Estado desconhecido: %d\n", currentState);
        break;
    }

    Serial.printf("⏱️ Estado há: %lus\n",
                  (millis() - stateStartTime) / 1000);
    Serial.println("==================================================");
}

void setup()
{
    Serial.begin(115200);
    Serial.println("🚀 Iniciando Serial...");

    int waitTime = 5;
    for (int i = 0; i < waitTime; ++i)
    {
        Serial.println("🔄 Iniciando em: " + String(waitTime - i));
        delay(1000);
    }

    Serial.println("=====================");
    Serial.println("\n🏢 BPR Central v1.0");
    Serial.println("=====================");

    // Inicializar LittleFS
    Serial.println("📂 Inicializando LittleFS...");
    if (!LittleFS.begin())
    {
        Serial.println("❌ Falha no LittleFS. Reiniciando...");
        ESP.restart();
    }
    Serial.printf("✅ LittleFS OK: %lu/%lu bytes\n", LittleFS.usedBytes(), LittleFS.totalBytes());

    // Self-check do sistema
    Serial.println("🔍 Executando self-check...");
    SelfCheck selfCheck;
    if (!selfCheck.systemCheck())
    {
        Serial.println("⚠️ Falha no system check. Iniciando mesmo assim");
    }

    // Inicializar módulos
    Serial.println("⚙️ Carregando configurações...");
    bool configLoaded = configManager.loadConfig();
    Serial.printf("📋 Config loaded: %s\n", configLoaded ? "OK" : "FALHA");

    Serial.println("💾 Inicializando buffer manager...");
    bufferManager.begin();
    Serial.println("✅ Buffer manager OK");

    Serial.println("💡 Inicializando LED controller...");
    ledController.begin();
    ledController.bootPattern();
    Serial.println("✅ LED controller OK");

    // Verificar se precisa de configuração
    if (!configLoaded || !configManager.isConfigValid())
    {
        Serial.println("⚠️ Config inválida - entrando em modo CONFIG_AP obrigatório");
        changeState(STATE_INITIAL_CONFIG_AP);
    }
    else
    {
        Serial.println("✅ Config válida encontrada");
        Serial.println("🔄 Iniciando sync obrigatório para validar configuração...");
        changeState(STATE_INITIAL_SYNC);
    }

    Serial.println("✅ Central inicializado com sucesso");
    Serial.printf("💾 Heap livre: %d bytes\n", ESP.getFreeHeap());
}

void loop()
{
    static unsigned long lastStatusPrint = 0;

    // Atualizar módulos
    ledController.update();

    // Update do estado atual
    switch (currentState)
    {
    case STATE_INITIAL_CONFIG_AP:
        ConfigAP::update();
        // Não tem timeout - fica até ser configurado
        break;
        
    case STATE_TEMP_CONFIG_AP:
        ConfigAP::update();
        
        // Check timeout for temporary CONFIG_AP mode
        if (tempConfigApStartTime > 0)
        {
            uint32_t configApTimeout = configManager.getConfig().config_ap_timeout_sec * 1000;
            if (millis() - tempConfigApStartTime > configApTimeout)
            {
                Serial.printf("⏰ TEMP_CONFIG_AP timeout (%ds) - voltando ao funcionamento normal\n", 
                             configManager.getConfig().config_ap_timeout_sec);
                tempConfigApStartTime = 0;
                changeState(STATE_BIKE_PAIRING);
                return;
            }
        }
        break;
    case STATE_BIKE_PAIRING:
        // Verificar se precisa sync urgente (buffer crítico)
        if (bufferManager.isCriticallyFull())
        {
            Serial.println("🚨 Buffer crítico - sync urgente!");
            changeState(STATE_CLOUD_SYNC);
            return;
        }
        // Verificar timer de sync periódico
        if (millis() - lastSyncCheck > configManager.getConfig().sync_interval_ms())
        {
            lastSyncCheck = millis();

            if (!BikePairing::isSafeToExit())
            {
                Serial.printf("⏳ Sync pendente - aguardando fim da atividade (status: %d)\n", BikePairing::getStatus());
            }
            else
            {
                // Add memory check before sync
                if (ESP.getFreeHeap() < 50000)
                {
                    Serial.printf("⚠️ Low memory before sync: %d bytes - forcing GC\n", ESP.getFreeHeap());
                    delay(100); // Allow cleanup
                }
                Serial.println("🔄 Tempo de sync - transitioning to CLOUD_SYNC");
                changeState(STATE_CLOUD_SYNC);
                return;
            }
        }
        BikePairing::update();
        break;

    case STATE_INITIAL_SYNC:
    {
        SyncResult result = CloudSync::update();
        if (result == SyncResult::SUCCESS)
        {
            Serial.println("✅ INITIAL_SYNC successful - transitioning to BIKE_PAIRING");
            changeState(STATE_BIKE_PAIRING);
            break;
        }

        if (result == SyncResult::IN_PROGRESS)
        {
            break;
        }

        if (result == SyncResult::FAILURE)
        {
            Serial.println("🚨 INITIAL_SYNC FALHOU - config inválida ou sem conectividade");
            Serial.println("🚨 ERRO CRÍTICO: Retornando ao modo CONFIG_AP obrigatório");
            changeState(STATE_INITIAL_CONFIG_AP);
        }
        break;
    }
    
    case STATE_CLOUD_SYNC:
    {
        SyncResult result = CloudSync::update();
        if (result == SyncResult::SUCCESS)
        {
            Serial.println("✅ CLOUD_SYNC successful - transitioning to BIKE_PAIRING");
            syncFailureCount = 0; // Reset counter on success
            changeState(STATE_BIKE_PAIRING);
            break;
        }

        if (result == SyncResult::IN_PROGRESS)
        {
            break;
        }

        if (result == SyncResult::FAILURE)
        {
            syncFailureCount++;
            Serial.printf("⚠️ CLOUD_SYNC falhou (tentativa %d/%d)\n", 
                         syncFailureCount, 
                         configManager.getConfig().sync_max_retries);

            if (syncFailureCount >= configManager.getConfig().sync_max_retries)
            {
                Serial.printf("🚨 %d falhas consecutivas - entrando em CONFIG_AP temporário\n", 
                             syncFailureCount);
                syncFailureCount = 0;
                changeState(STATE_TEMP_CONFIG_AP);
            }
            else
            {
                Serial.println("⚠️ Continuando com última config válida");
                changeState(STATE_BIKE_PAIRING);
            }
        }
        break;
    }

    default:
        break;
    }

    // Status periódico (30s)
    if (millis() - lastStatusPrint > 30000)
    {
        printStatus();
        lastStatusPrint = millis();
    }
}
