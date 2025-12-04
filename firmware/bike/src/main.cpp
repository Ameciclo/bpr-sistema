#include <Arduino.h>
#include <LittleFS.h>
#include "bike_config.h"
#include "battery_monitor.h"
#include "wifi_scanner.h"
#include "ble_client.h"
#include "power_manager.h"
#include "config_manager.h"

// Estado global
BikeState currentState = BOOT;

// Módulos
ConfigManager config;
BatteryMonitor battery;
WiFiScanner wifiScanner;
BikeClient bleClient;
PowerManager powerManager;

// Timers
uint32_t lastScanTime = 0;
uint32_t stateStartTime = 0;
uint32_t systemTime = 0; // Timestamp sincronizado

// Flags
bool lowBatteryMode = false;
volatile bool buttonPressed = false;

// Declarações de funções
void handleBootState();
void handleAtBaseState();
void handleScanningState();
void handleLowPowerState();
void handleDeepSleepState();
void handleEmergencyMode();
void changeState(BikeState newState);
void printStatus();

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
  
  // Carregar configuração
  config.loadFromFile();
  config.printConfig();
  
  bleClient.init(config.getBikeId());
  
  Serial.printf("🆔 Bike ID: %s\n", config.getBikeId().c_str());
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
  lowBatteryMode = battery.isLowBattery(config.getMinBatteryVoltage());
  
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
  if (bleClient.scanForBase(config.getBaseBleName())) {
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
  
  // Registrar com a base se necessário
  if (!bleClient.isRegistered()) {
    bleClient.registerWithBase();
  }
  
  // Enviar info da bike
  bleClient.sendBikeInfo();
  
  // Enviar status
  bleClient.sendStatus(battery.readVoltage(), wifiScanner.getRecordCount());
  
  // Receber configurações
  String configJson;
  if (bleClient.receiveConfig(configJson)) {
    if (config.updateFromBLE(configJson)) {
      systemTime = config.getConfigTimestamp();
    }
  }
  
  // Enviar dados WiFi se houver
  if (wifiScanner.hasRecords()) {
    String wifiData;
    if (wifiScanner.exportAllData(wifiData)) {
      if (bleClient.sendWifiDataJson(wifiData)) {
        wifiScanner.clearAllData();
      }
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
  uint32_t scanInterval = lowBatteryMode ? config.getScanIntervalLowBatt() : config.getScanInterval();
  
  if (systemTime - lastScanTime >= scanInterval) {
    if (wifiScanner.performScan(systemTime)) {
      lastScanTime = systemTime;
    }
  }
  
  // Verificar se encontrou base novamente
  if (bleClient.scanForBase(config.getBaseBleName())) {
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
  uint32_t sleepTime = (scanInterval / 4 < 300) ? scanInterval / 4 : 300; // Max 5 minutos
  Serial.printf("😴 Sleep por %d segundos\n", sleepTime);
  powerManager.enterLightSleep(sleepTime);
}

void handleLowPowerState() {
  Serial.println("🔋 Estado: LOW_POWER");
  
  // Scan menos frequente
  if (systemTime - lastScanTime >= config.getScanIntervalLowBatt()) {
    if (wifiScanner.performScan(systemTime)) {
      lastScanTime = systemTime;
    }
  }
  
  // Verificar se encontrou base
  if (bleClient.scanForBase(config.getBaseBleName())) {
    changeState(AT_BASE);
    return;
  }
  
  // Verificar se deve entrar em deep sleep
  if (battery.readVoltage() < (config.getMinBatteryVoltage() - 0.1)) {
    changeState(DEEP_SLEEP);
    return;
  }
  
  // Sleep longo
  Serial.println("💤 Long sleep em low power");
  powerManager.enterLightSleep(config.getScanIntervalLowBatt());
}

void handleDeepSleepState() {
  Serial.println("💤 Estado: DEEP_SLEEP");
  
  // Salvar dados críticos
  config.saveToFile();
  
  // Entrar em deep sleep
  powerManager.enterDeepSleep(config.getDeepSleepSec());
  
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
  
  Serial.println("\n==================================================");
  Serial.printf("🚲 %s | Estado: %s | Uptime: %ds\n", 
                config.getBikeId().c_str(), stateNames[currentState], powerManager.getUptimeSeconds());
  Serial.printf("🔋 %.2fV (%d%%) %s | 📡 %d registros\n", 
                battery.readVoltage(), battery.getPercentage(),
                lowBatteryMode ? "⚠️" : "✅", wifiScanner.getTotalStoredCount());
  Serial.printf("🔵 BLE: %s | ⏱️ Último scan: %ds atrás\n",
                bleClient.isConnected() ? "Conectado" : "Desconectado",
                systemTime - lastScanTime);
  Serial.println("==================================================\n");
}