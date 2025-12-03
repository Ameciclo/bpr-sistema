#include "operation_modes.h"
#include "config.h"
#include "wifi_scanner.h"
#include "firebase.h"
#include "online_config.h"
#include "status_tracker.h"
#include <WiFi.h>

ModeState modeState;

// Declaração de funções auxiliares
String getModeString(OperationMode mode);
String repeatChar(char c, int count);

void initializeOperationModes() {
  Serial.println("🚀 Inicializando modos de operação...");
  
  modeState.currentMode = MODE_STARTUP;
  modeState.modeStartTime = millis();
  modeState.lastModeCheck = 0;
  modeState.ntpSyncedAtStart = false;
  modeState.ntpSyncedAtEnd = false;
  modeState.consecutiveBaseDetections = 0;
  modeState.consecutiveTravelDetections = 0;
  
  Serial.println("📍 Modo inicial: STARTUP");
}

OperationMode detectCurrentMode() {
  bool atBase = isAtBaseLocation();
  
  if (atBase) {
    modeState.consecutiveBaseDetections++;
    modeState.consecutiveTravelDetections = 0;
    
    // Confirmar com 2 detecções consecutivas para evitar oscilação
    if (modeState.consecutiveBaseDetections >= 2) {
      return MODE_BASE;
    }
  } else {
    modeState.consecutiveTravelDetections++;
    modeState.consecutiveBaseDetections = 0;
    
    // Confirmar com 1 detecção para resposta rápida ao sair da base
    if (modeState.consecutiveTravelDetections >= 1) {
      return MODE_TRAVEL;
    }
  }
  
  // Manter modo atual se não há confirmação
  return modeState.currentMode;
}

void switchToMode(OperationMode newMode) {
  if (newMode == modeState.currentMode) {
    return; // Já está no modo correto
  }
  
  OperationMode oldMode = modeState.currentMode;
  modeState.currentMode = newMode;
  modeState.modeStartTime = millis();
  
  Serial.println("\n" + repeatChar('=', 50));
  Serial.printf("🔄 MUDANÇA DE MODO: %s → %s\n", 
                getModeString(oldMode).c_str(), 
                getModeString(newMode).c_str());
  Serial.println(repeatChar('=', 50));
  
  // Ações específicas na mudança de modo
  switch (newMode) {
    case MODE_BASE:
      Serial.println("🏠 Entrando no MODO BASE");
      Serial.println("   • Sincronização NTP");
      Serial.println("   • Upload de dados");
      Serial.println("   • Modo economia de energia");
      break;
      
    case MODE_TRAVEL:
      Serial.println("🚴 Entrando no MODO VIAGEM");
      Serial.println("   • Coleta ativa de dados WiFi");
      Serial.println("   • Armazenamento local");
      Serial.println("   • Monitoramento de bateria");
      break;
      
    case MODE_STARTUP:
      Serial.println("⚡ Entrando no MODO INICIALIZAÇÃO");
      Serial.println("   • Sincronização de configurações");
      Serial.println("   • Sincronização NTP inicial");
      Serial.println("   • Detecção de modo operacional");
      break;
  }
}

void handleStartupMode() {
  Serial.println("⚡ Processando modo STARTUP...");
  
  // 1. Sincronizar configurações online
  if (!onlineConfig.configSynced) {
    Serial.println("🔧 Sincronizando configurações...");
    if (initializeOnlineConfig()) {
      Serial.println("✅ Configurações sincronizadas!");
    } else {
      Serial.println("⚠️ Usando configurações locais");
    }
  }
  
  // 2. Sincronizar NTP inicial
  if (!modeState.ntpSyncedAtStart) {
    Serial.println("🕰️ Sincronização NTP inicial...");
    performNTPSync(true);
    modeState.ntpSyncedAtStart = true;
  }
  
  // 3. Detectar modo operacional
  OperationMode detectedMode = detectCurrentMode();
  if (detectedMode != MODE_STARTUP) {
    switchToMode(detectedMode);
  }
}

void handleBaseMode() {
  Serial.println("🏠 Processando modo BASE...");
  
  // Verificar se ainda está na base
  if (!isAtBaseLocation()) {
    Serial.println("🚶 Saindo da base - mudando para modo VIAGEM");
    switchToMode(MODE_TRAVEL);
    return;
  }
  
  // Conectar na base se não estiver conectado
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("🔌 Conectando na base...");
    if (!connectToBase()) {
      Serial.println("❌ Falha ao conectar - mudando para modo VIAGEM");
      switchToMode(MODE_TRAVEL);
      return;
    }
  }
  
  // Realizar sincronizações necessárias
  performBaseSynchronization();
  
  Serial.printf("😴 Modo BASE - dormindo por %d segundos...\n", 
                config.scanTimeInactive / 1000);
}

void handleTravelMode() {
  Serial.println("🚴 Processando modo VIAGEM...");
  
  // Verificar se chegou na base
  OperationMode detectedMode = detectCurrentMode();
  if (detectedMode == MODE_BASE) {
    // Sincronizar NTP ao chegar na base
    Serial.println("🏠 Chegando na base - sincronizando NTP...");
    performNTPSync(false);
    modeState.ntpSyncedAtEnd = true;
    switchToMode(MODE_BASE);
    return;
  }
  
  // Coletar dados WiFi
  performTravelDataCollection();
  
  Serial.printf("🚴 Modo VIAGEM - próximo scan em %d segundos...\n", 
                config.scanTimeActive / 1000);
}

bool isAtBaseLocation() {
  // Escanear redes WiFi para detectar bases
  scanWiFiNetworks();
  
  // Verificar se alguma das bases está próxima
  for (int i = 0; i < networkCount; i++) {
    // Verificar Base 1
    if (strlen(config.baseSSID1) > 0 && 
        strcmp(networks[i].ssid, config.baseSSID1) == 0 &&
        networks[i].rssi > config.baseProximityRssi) {
      Serial.printf("🏠 Base 1 detectada: %s (RSSI: %d)\n", 
                    networks[i].ssid, networks[i].rssi);
      return true;
    }
    
    // Verificar Base 2
    if (strlen(config.baseSSID2) > 0 && 
        strcmp(networks[i].ssid, config.baseSSID2) == 0 &&
        networks[i].rssi > config.baseProximityRssi) {
      Serial.printf("🏠 Base 2 detectada: %s (RSSI: %d)\n", 
                    networks[i].ssid, networks[i].rssi);
      return true;
    }
    
    // Verificar Base 3
    if (strlen(config.baseSSID3) > 0 && 
        strcmp(networks[i].ssid, config.baseSSID3) == 0 &&
        networks[i].rssi > config.baseProximityRssi) {
      Serial.printf("🏠 Base 3 detectada: %s (RSSI: %d)\n", 
                    networks[i].ssid, networks[i].rssi);
      return true;
    }
  }
  
  return false;
}

void performNTPSync(bool isStartup) {
  if (isStartup) {
    Serial.println("🕰️ Sincronização NTP inicial...");
  } else {
    Serial.println("🕰️ Sincronização NTP final...");
  }
  
  // Tentar conectar se não estiver conectado
  if (WiFi.status() != WL_CONNECTED) {
    if (!connectToBase()) {
      Serial.println("⚠️ Não foi possível conectar para NTP");
      return;
    }
  }
  
  syncTime();
  
  if (timeSync) {
    if (isStartup) {
      Serial.println("✅ NTP inicial sincronizado!");
    } else {
      Serial.println("✅ NTP final sincronizado!");
    }
  } else {
    Serial.println("⚠️ Falha na sincronização NTP");
  }
}

void performBaseSynchronization() {
  Serial.println("🔄 Realizando sincronizações da base...");
  
  // 1. Sincronizar horário se necessário
  if (needsNTPSync()) {
    performNTPSync(false);
  }
  
  // 2. Fazer check-in
  uploadCheckIn();
  
  // 3. Verificar alertas de bateria
  if (needsLowBatteryAlert()) {
    Serial.println("🚨 Enviando alerta de bateria baixa...");
    uploadLowBatteryAlert();
  }
  
  // 4. Verificar status programado
  if (needsScheduledStatusUpdate()) {
    Serial.println("📈 Enviando status programado...");
    uploadScheduledStatus();
  }
  
  // 5. Upload de dados se houver
  if (dataCount > 0) {
    Serial.println("⬆️ Fazendo upload de dados coletados...");
    uploadSessionData();
  }
  
  Serial.println("✅ Sincronizações da base concluídas");
}

void performTravelDataCollection() {
  Serial.println("📡 Coletando dados de viagem...");
  
  // Escanear redes WiFi
  scanWiFiNetworks();
  
  if (networkCount > 0) {
    Serial.printf("✅ Encontradas %d redes\n", networkCount);
    
    // Armazenar dados
    storeData();
    Serial.printf("💾 Dados armazenados - Total: %d arquivos\n", dataCount);
  } else {
    Serial.println("⚠️ Nenhuma rede encontrada");
  }
}

int getDelayForCurrentMode() {
  switch (modeState.currentMode) {
    case MODE_BASE:
      return config.scanTimeInactive;
    case MODE_TRAVEL:
      return config.scanTimeActive;
    case MODE_STARTUP:
      return 2000; // 2 segundos para startup
    default:
      return config.scanTimeActive;
  }
}

String getModeString(OperationMode mode) {
  switch (mode) {
    case MODE_BASE: return "BASE";
    case MODE_TRAVEL: return "VIAGEM";
    case MODE_STARTUP: return "STARTUP";
    default: return "DESCONHECIDO";
  }
}