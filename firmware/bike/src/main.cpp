#include <Arduino.h>
#include <LittleFS.h>
#include "bike_config.h"
#include "battery_monitor.h"
#include "wifi_scanner.h"
#include "ble_client.h"
#include "power_manager.h"

// Estado global
BikeState currentState = BOOT;
BikeConfig config;
char bikeId[16] = "bike_001";

// Módulos
BatteryMonitor battery;
WiFiScanner wifiScanner;
BLEClient bleClient;
PowerManager powerManager;

// Timers
uint32_t lastScanTime = 0;
uint32_t stateStartTime = 0;
uint32_t systemTime = 0; // Timestamp sincronizado

// Flags
bool lowBatteryMode = false;
volatile bool buttonPressed = false;

// ISR do botão
void IRAM_ATTR buttonISR() {
  buttonPressed = true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n🚲 BPR Bike System v2.0");
  Serial.println("========================");
  
  // Inicializar LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  
  // Inicializar botão
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonISR, FALLING);
  
  // Inicializar LittleFS
  if (!LittleFS.begin(true)) {
    Serial.println("❌ Falha no LittleFS");
    ESP.restart();
  }
  
  // Inicializar módulos
  powerManager.init();
  powerManager.printWakeupReason();
  
  battery.init();
  wifiScanner.init();
  bleClient.init(bikeId);
  
  // Carregar configuração local
  loadConfig();
  
  Serial.printf("🆔 Bike ID: %s\n", bikeId);
  Serial.printf("🔋 Bateria: %.2fV (%d%%)\n", battery.readVoltage(), battery.getPercentage());
  
  // Estado inicial
  currentState = BOOT;
  stateStartTime = millis();
  
  digitalWrite(LED_PIN, LOW);
  Serial.println("✅ Sistema inicializado\n");
}

void loop() {
  // Atualizar tempo do sistema
  systemTime = millis() / 1000;
  
  // Ler bateria
  float batteryVoltage = battery.readVoltage();
  lowBatteryMode = battery.isLowBattery(config.min_battery_voltage);
  
  // Verificar botão de emergência
  if (buttonPressed) {
    buttonPressed = false;
    Serial.println("🔘 Botão pressionado - Modo emergência");
    handleEmergencyMode();
    return;
  }
  
  // Máquina de estados
  switch (currentState) {
    case BOOT:
      handleBootState();
      break;
      
    case AT_BASE:
      handleAtBaseState();
      break;
      
    case SCANNING:
      handleScanningState();
      break;
      
    case LOW_POWER:
      handleLowPowerState();
      break;
      
    case DEEP_SLEEP:
      handleDeepSleepState();
      break;
  }
  
  // Status periódico
  static uint32_t lastStatus = 0;
  if (millis() - lastStatus > 30000) { // A cada 30s
    printStatus();
    lastStatus = millis();
  }
  
  delay(100);
}

void handleBootState() {
  Serial.println("🔄 Estado: BOOT");
  
  // Tentar encontrar base
  if (bleClient.scanForBase(config.base_ble_name)) {
    changeState(AT_BASE);
  } else {
    // Não encontrou base, iniciar scanning
    Serial.println("📡 Base não encontrada, iniciando modo scanning");
    changeState(SCANNING);
  }
}

void handleAtBaseState() {
  Serial.println("🏠 Estado: AT_BASE");
  
  powerManager.optimizeForBLE();
  
  // Conectar à base
  if (!bleClient.isConnected()) {
    if (!bleClient.connectToBase()) {
      Serial.println("❌ Falha na conexão, voltando ao scanning");
      changeState(SCANNING);
      return;
    }
  }
  
  // Enviar status
  BikeStatus status;
  strncpy(status.bike_id, bikeId, sizeof(status.bike_id));
  status.battery_voltage = battery.readVoltage();
  status.last_scan_timestamp = lastScanTime;
  status.flags = lowBatteryMode ? 1 : 0;
  status.records_count = wifiScanner.getRecordCount();
  
  bleClient.sendStatus(status);
  
  // Receber configurações
  BikeConfig newConfig;
  if (bleClient.receiveConfig(newConfig)) {
    config = newConfig;
    saveConfig();
    systemTime = config.timestamp;
  }
  
  // Enviar dados WiFi se houver
  if (wifiScanner.hasRecords()) {
    if (bleClient.sendWifiData(wifiScanner.getRecords())) {
      wifiScanner.clearRecords();
    }
  }
  
  // Permanecer conectado ou entrar em sleep leve
  Serial.println("💤 Entrando em light sleep na base");
  powerManager.enterLightSleep(60); // 1 minuto
  
  // Verificar se ainda está conectado
  if (!bleClient.isConnected()) {
    Serial.println("🚴 Saiu da base");
    changeState(SCANNING);
  }
}

void handleScanningState() {
  Serial.println("📡 Estado: SCANNING");
  
  powerManager.optimizeForScanning();
  
  // Verificar se deve fazer scan
  uint32_t scanInterval = lowBatteryMode ? config.scan_interval_low_batt_sec : config.scan_interval_sec;
  
  if (systemTime - lastScanTime >= scanInterval) {
    if (wifiScanner.performScan(systemTime)) {
      lastScanTime = systemTime;
    }
  }
  
  // Verificar se encontrou base novamente
  if (bleClient.scanForBase(config.base_ble_name)) {
    Serial.println("🏠 Base encontrada, retornando");
    changeState(AT_BASE);
    return;
  }
  
  // Verificar condições para low power
  uint32_t timeInScanning = (millis() - stateStartTime) / 1000;
  
  if (lowBatteryMode || timeInScanning > 3600) { // 1 hora
    changeState(LOW_POWER);
    return;
  }
  
  // Sleep entre scans
  uint32_t sleepTime = min(scanInterval / 4, 300U); // Max 5 minutos
  Serial.printf("😴 Sleep por %d segundos\n", sleepTime);
  powerManager.enterLightSleep(sleepTime);
}

void handleLowPowerState() {
  Serial.println("🔋 Estado: LOW_POWER");
  
  // Scan menos frequente
  if (systemTime - lastScanTime >= config.scan_interval_low_batt_sec) {
    if (wifiScanner.performScan(systemTime)) {
      lastScanTime = systemTime;
    }
  }
  
  // Verificar se encontrou base
  if (bleClient.scanForBase(config.base_ble_name)) {
    changeState(AT_BASE);
    return;
  }
  
  // Verificar se deve entrar em deep sleep
  if (battery.readVoltage() < (config.min_battery_voltage - 0.1)) {
    changeState(DEEP_SLEEP);
    return;
  }
  
  // Sleep longo
  Serial.println("💤 Long sleep em low power");
  powerManager.enterLightSleep(config.scan_interval_low_batt_sec);
}

void handleDeepSleepState() {
  Serial.println("💤 Estado: DEEP_SLEEP");
  
  // Salvar dados críticos
  saveConfig();
  
  // Entrar em deep sleep
  powerManager.enterDeepSleep(config.deep_sleep_sec);
  
  // Não retorna - reinicia após wake-up
}

void handleEmergencyMode() {
  Serial.println("🚨 MODO EMERGÊNCIA");
  Serial.println("Pressione 'r' para reiniciar ou 'c' para continuar");
  
  unsigned long start = millis();
  while (millis() - start < 10000) { // 10 segundos
    if (Serial.available()) {
      char cmd = Serial.read();
      if (cmd == 'r' || cmd == 'R') {
        ESP.restart();
      } else if (cmd == 'c' || cmd == 'C') {
        return;
      }
    }
    delay(100);
  }
}

void changeState(BikeState newState) {
  Serial.printf("🔄 %d -> %d\n", currentState, newState);
  currentState = newState;
  stateStartTime = millis();
}

void printStatus() {
  const char* stateNames[] = {"BOOT", "AT_BASE", "SCANNING", "LOW_POWER", "DEEP_SLEEP"};
  
  Serial.println("\n" + String("=").repeat(50));
  Serial.printf("🚲 %s | Estado: %s | Uptime: %ds\n", 
                bikeId, stateNames[currentState], powerManager.getUptimeSeconds());
  Serial.printf("🔋 %.2fV (%d%%) %s | 📡 %d registros\n", 
                battery.readVoltage(), battery.getPercentage(),
                lowBatteryMode ? "⚠️" : "✅", wifiScanner.getRecordCount());
  Serial.printf("🔵 BLE: %s | ⏱️ Último scan: %ds atrás\n",
                bleClient.isConnected() ? "Conectado" : "Desconectado",
                systemTime - lastScanTime);
  Serial.println(String("=").repeat(50) + "\n");
}

void loadConfig() {
  // Implementação básica - usar valores padrão
  Serial.println("📁 Carregando configuração padrão");
}

void saveConfig() {
  // Implementação básica
  Serial.println("💾 Salvando configuração");
}