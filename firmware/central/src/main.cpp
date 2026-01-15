/*
 * BPR Sistema - Firmware Central v1.0
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
#include <LittleFS.h>
#include <WiFi.h>
#include "bike_pairing.h"
#include "ble_server.h"
#include "buffer_manager.h"
#include "cloud_sync.h"
#include "config_ap.h"
#include "config_manager.h"
#include "config_credentials.h"
#include "constants.h"
#include "led_controller.h"
#include "self_check.h"
#include "sync_monitor.h"

// Instâncias globais
ConfigManager configManager;
ConfigCredentials configCredentials;
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

// Flag para restart seguro
bool pendingRestart = false;
uint32_t restartRequestTime = 0;

// Buffer initialization flag
static bool bufferInitialized = false;

// Callback para eventos de bike
void onBikeEvent(BikeEvent event, uint8_t bikeCount) {
    switch (event) {
        case BIKE_ARRIVED:
            ledController.bikeArrivedPattern();
            break;
        case BIKE_LEFT:
            ledController.bikeLeftPattern();
            break;
        case BIKE_COUNT_CHANGED:
            ledController.countPattern(bikeCount);
            break;
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
    case STATE_INITIAL_CONFIG_AP:
    case STATE_TEMP_CONFIG_AP:
        ConfigAP::exit();
        break;
    case STATE_BIKE_PAIRING:
        BikePairing::exit();
        // Limpar buffer dinâmico ao sair do pairing
        bufferManager.cleanup();
        // Reset buffer initialization flag
        bufferInitialized = false;
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
        ConfigAP::enter(true); // Modo obrigatório
        break;
    case STATE_TEMP_CONFIG_AP:
        ledController.configPattern();
        SyncMonitor::reset();
        ConfigAP::enter(false); // Modo temporário
        tempConfigApStartTime = millis();
        break;
    case STATE_BIKE_PAIRING:
        ledController.pairingPattern();
        BikePairing::setEventCallback(onBikeEvent);
        
        // Inicializar buffer dinâmico baseado no heap disponível
        if (!bufferInitialized) {
            if (bufferManager.beginWithAvailableHeap()) {
                Serial.println("✅ Buffer dinâmico inicializado");
                bufferInitialized = true;
            } else {
                Serial.println("⚠️ Buffer dinâmico falhou - modo degradado");
            }
        }
        
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
                  configCredentials.getBaseId().c_str(),
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

    int waitTime = 3;
    for (int i = 0; i < waitTime; ++i)
    {
        Serial.println("🔄 Iniciando em: " + String(waitTime - i));
        delay(1000);
    }

    Serial.println("=====================");
    Serial.println("\n🏢 BPR Central v1.0");
    Serial.println("=====================");

    // Inicializar LittleFS com formatação automática se corrompido
    Serial.println("📂 Inicializando LittleFS...");
    if (!LittleFS.begin())
    {
        Serial.println("❌ LittleFS corrompido - formatando...");
        if (!LittleFS.begin(true)) // true = format if mount fails
        {
            Serial.println("❌ Falha crítica no LittleFS. Reiniciando...");
            ESP.restart();
        }
        Serial.println("✅ LittleFS formatado com sucesso");
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
    Serial.println("⚙️ Carregando credenciais...");
    bool credsLoaded = configCredentials.loadCredentials();
    Serial.printf("🔑 Credentials loaded: %s\n", credsLoaded ? "OK" : "FALHA");
    
    Serial.println("⚙️ Carregando configurações...");
    bool configLoaded = configManager.loadConfig();
    Serial.printf("📋 Config loaded: %s\n", configLoaded ? "OK" : "FALHA");

    Serial.println("💾 Buffer manager initialization deferred to BIKE_PAIRING state");
    Serial.println("✅ Buffer manager deferred (LAZY INIT)");

    Serial.println("💡 Inicializando LED controller...");
    ledController.begin();
    ledController.bootPattern();
    Serial.println("✅ LED controller OK");

    // Verificar se precisa de configuração
    if (!credsLoaded || !configCredentials.isCredentialsValid())
    {
        Serial.println("⚠️ Credentials inválidas - entrando em modo CONFIG_AP obrigatório");
        changeState(STATE_INITIAL_CONFIG_AP);
    }
    else if (!configLoaded || !configManager.isConfigValid())
    {
        Serial.println("⚠️ Config inválida - criando config padrão e fazendo primeiro sync");
        // Salvar config padrão para evitar loop infinito
        if (!configManager.saveConfig()) {
            Serial.println("❌ Erro ao salvar config padrão - entrando em CONFIG_AP");
            changeState(STATE_INITIAL_CONFIG_AP);
            return;
        }
        Serial.println("🔄 Iniciando primeiro sync obrigatório...");
        changeState(STATE_INITIAL_SYNC);
    }
    else
    {
        Serial.println("✅ Config e Credentials válidas encontradas");
        // Verificar se é primeiro sync
        if (configCredentials.isFirstSync()) {
            Serial.println("🔄 Primeiro sync necessário...");
            changeState(STATE_INITIAL_SYNC);
        } else {
            Serial.println("🔄 Iniciando modo BIKE_PAIRING...");
            changeState(STATE_BIKE_PAIRING);
        }
    }

    Serial.println("✅ Central inicializado com sucesso");
    Serial.printf("💾 Heap livre: %d bytes\n", ESP.getFreeHeap());
}

void loop()
{
    static unsigned long lastStatusPrint = 0;

    // Verificar se há restart pendente
    if (pendingRestart && (millis() - restartRequestTime) > 2000) {
        Serial.println("🔄 Executando restart seguro...");
        BPRBLEServer::stop();
        WiFi.disconnect(true);
        delay(1000);
        ESP.restart();
    }

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
    {
        // Se não tem timeout ativo, executar update normalmente
        if (tempConfigApStartTime <= 0) {
            ConfigAP::update();
            break;
        }
        
        // Verificar se timeout foi atingido
        uint32_t configApTimeout = configManager.getConfig().fallback.config_ap_timeout_sec * 1000;
        if (millis() - tempConfigApStartTime > configApTimeout) {
            // Timeout atingido - voltar ao funcionamento normal
            Serial.printf("⏰ TEMP_CONFIG_AP timeout (%ds) - voltando ao funcionamento normal\n",
                          configManager.getConfig().fallback.config_ap_timeout_sec);
            tempConfigApStartTime = 0;
            changeState(STATE_BIKE_PAIRING);
            return;
        }
        
        // Timeout ativo mas não atingido - continuar no CONFIG_AP
        ConfigAP::update();
        break;
    }
    case STATE_BIKE_PAIRING:
    {
        BikePairing::update();
        
        // Verificar se precisa fazer sync apenas a cada 5 segundos
        static uint32_t lastSyncCheck = 0;
        uint32_t now = millis();
        
        if (now - lastSyncCheck > 5000) {
            lastSyncCheck = now;
            
            if (BikePairing::isSafeToExit()) {
                uint32_t elapsed = now - stateStartTime;
                uint32_t syncInterval = configManager.getConfig().sync_interval_ms();
                
                if (elapsed >= syncInterval || bufferManager.isFull()) {
                    Serial.println("🔄 Iniciando sync programada...");
                    changeState(STATE_CLOUD_SYNC);
                }
            }
        }
        break;
    }

    case STATE_INITIAL_SYNC:
    case STATE_CLOUD_SYNC:
        {
            SyncResult result = CloudSync::update();
            if (result == SyncResult::SUCCESS) {
                Serial.println("✅ Sync concluída com sucesso");
                SyncMonitor::recordSuccess();
                changeState(STATE_BIKE_PAIRING);
            } else if (result == SyncResult::FAILURE) {
                Serial.println("❌ Sync falhou");
                SyncMonitor::recordFailure();
                
                if (SyncMonitor::shouldFallback()) {
                    Serial.println("⚠️ Muitas falhas de sync - entrando em modo CONFIG_AP temporário");
                    changeState(STATE_TEMP_CONFIG_AP);
                } else {
                    // Retry após delay não-bloqueante
                    static uint32_t lastRetryTime = 0;
                    if (millis() - lastRetryTime > 30000) {
                        Serial.println("🔄 Tentando sync novamente...");
                        lastRetryTime = millis();
                        // Reset para tentar novamente
                        changeState(STATE_CLOUD_SYNC);
                    }
                }
            }
            // Se IN_PROGRESS, continua no próximo loop
        }
        break;

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
