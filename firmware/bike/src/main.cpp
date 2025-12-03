#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <soc/rtc_cntl_reg.h>
#include "config.h"
#include "wifi_scanner.h"
#include "web_server.h"
#include "firebase.h"
#include "led_control.h"
#include "serial_menu.h"
#include "status_tracker.h"
#include "diagnostics.h"
#include "power_management.h"
#include "online_config.h"
#include "operation_modes.h"

// Função para repetir caracteres
String repeatChar(char c, int count) {
  String result = "";
  for (int i = 0; i < count; i++) {
    result += c;
  }
  return result;
}

// Declaração da função parseConfigFromJson
bool parseConfigFromJson(String jsonStr);

// Global variables
Config config;
WiFiNetwork networks[30];
int networkCount = 0;
ScanData dataBuffer[20];
int dataCount = 0;
WebServer server(80);
bool configMode = false;
bool timeSync = false;
unsigned long lastLedBlink = 0;
int ledState = LOW;
int ledStep = 0;
unsigned long lastStatusUpload = 0;
unsigned long configModeStart = 0;
unsigned long lastSessionUpload = 0;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -3 * 3600, 60000);
unsigned long currentRealTime = 0;

// Variáveis para interrupção do botão
volatile bool bootButtonPressed = false;
volatile unsigned long lastButtonPress = 0;

// Função para contar arquivos de scan
int countScanFiles() {
  File root = LittleFS.open("/");
  if (!root) return 0;
  
  int count = 0;
  File file = root.openNextFile();
  while (file) {
    String fileName = file.name();
    if (fileName.startsWith("/scan_") || fileName.startsWith("scan_")) {
      count++;
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
  return count;
}

// Função para salvar logs de debug em arquivo
void saveDebugLog(String message) {
  File debugFile = LittleFS.open("/debug.log", "a");
  if (debugFile) {
    debugFile.printf("[%lu] %s\n", millis(), message.c_str());
    debugFile.close();
  }
}

// ISR para botão BOOT
void IRAM_ATTR bootButtonISR() {
  unsigned long now = millis();
  if (now - lastButtonPress > 200) { // Debounce de 200ms
    bootButtonPressed = true;
    lastButtonPress = now;
  }
}

// Função para verificar botão durante operações longas
bool checkBootButtonAndStartAP() {
  if (bootButtonPressed) {
    bootButtonPressed = false;
    Serial.println("\n🔧 BOTÃO BOOT - Interrompendo para criar AP...");
    
    WiFi.mode(WIFI_AP);
    String apName = "Bike-" + String(config.bikeId);
    WiFi.softAP(apName.c_str(), config.apPassword);
    Serial.println("📶 AP criado: " + apName);
    Serial.println("Acesse: http://192.168.4.1");
    Serial.println("Pressione BOOT novamente ou use botão web para sair");
    
    server.on("/", handleRoot);
    server.on("/config", handleConfig);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/wifi", handleWifi);
    server.on("/dados", handleDados);
    server.on("/download", handleDownload);
    server.on("/test", handleTest);
    server.on("/exit", handleExit);
    server.begin();
    
    configMode = true;
    configModeStart = millis();
    return true;
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  pinMode(8, OUTPUT);
  digitalWrite(8, HIGH);
  
  delay(1000);
  
  // OTIMIZAÇÕES CRÍTICAS PARA BATERIA - após inicialização
  setCpuFrequencyMhz(40);
  WiFi.setTxPower(WIFI_POWER_MINUS_1dBm);  // Potência mínima WiFi (será ajustada pela config online)
  btStop();
  
  setupPowerManagement();
  
  // Desabilitar brownout APÓS tudo inicializado
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  
  // Log inicial de boot
  Serial.println("\n=== BOOT INICIADO - NOVO FLUXO ===");
  Serial.printf("Millis: %lu\n", millis());
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("Chip ID: %08X\n", (uint32_t)ESP.getEfuseMac());
  
  if (!LittleFS.begin()) {
    Serial.println("Falha ao montar sistema de arquivos. Formatando...");
    saveDebugLog("ERRO: LittleFS falhou, formatando");
    LittleFS.format();
    if (!LittleFS.begin()) {
      Serial.println("Falha ao montar sistema de arquivos mesmo após formatação");
      saveDebugLog("ERRO CRITICO: LittleFS falhou mesmo após format");
    } else {
      saveDebugLog("LittleFS OK após format");
    }
  } else {
    saveDebugLog("LittleFS montado com sucesso");
  }
  
  // Carregar configuração local básica
  saveDebugLog("Carregando configuração local");
  loadConfig();
  saveDebugLog("Configuração local carregada - BikeID: " + String(config.bikeId));
  
  // Carregar estado NTP salvo
  loadNTPState();

  pinMode(9, INPUT_PULLUP); // BOOT button no ESP32-C3
  attachInterrupt(digitalPinToInterrupt(9), bootButtonISR, FALLING);
  delay(500);
  
  // Verificar botão para modo de emergência (fallback para AP)
  bool emergencyMode = false;
  for (int i = 0; i < 5; i++) {
    if (digitalRead(9) == LOW) {
      emergencyMode = true;
      break;
    }
    delay(100);
  }
  
  Serial.println("\n" + repeatChar('=', 50));
  Serial.println("🚲 WIFI RANGE SCANNER v2.0 - ESP32 🚲");
  Serial.println(repeatChar('=', 50));
  Serial.printf("🆔 Bicicleta: %s\n", config.bikeId);
  Serial.printf("🔘 Botão FLASH: %s\n", emergencyMode ? "🔴 MODO EMERGÊNCIA" : "🟢 NORMAL");
  Serial.printf("💾 Sistema de arquivos: %s\n", LittleFS.begin() ? "🟢 OK" : "🔴 ERRO");
  
  if (emergencyMode) {
    Serial.println("🚨 MODO EMERGÊNCIA - Criando AP para configuração");
    configModeStart = millis();
    startConfigMode();
    return;
  }
  
  // NOVO FLUXO: Inicializar sistema de modos de operação
  Serial.println("\n🚀 INICIANDO NOVO SISTEMA DE OPERAÇÃO...");
  Serial.println("📋 Fluxo:");
  Serial.println("   1️⃣ Conectar no Firebase");
  Serial.println("   2️⃣ Sincronizar configurações online");
  Serial.println("   3️⃣ Sincronizar NTP inicial");
  Serial.println("   4️⃣ Detectar modo (Base/Viagem)");
  Serial.println("   5️⃣ Iniciar operação");
  
  // Inicializar modos de operação
  initializeOperationModes();
  
  // Contar arquivos existentes
  dataCount = countScanFiles();
  Serial.printf("📦 Arquivos de scan encontrados: %d\n", dataCount);
  
  Serial.println("💡 LED indicará:");
  Serial.println("   🔴 3 piscadas = Inicialização");
  Serial.println("   🟡 2 piscadas = Modo Viagem");
  Serial.println("   🟢 1 piscada = Modo Base");
  Serial.println(repeatChar('=', 50));
}

void loop() {
  updateLED();
  
  // Atualizar dataCount com arquivos reais
  dataCount = countScanFiles();
  
  // MODO EMERGÊNCIA - AP de configuração
  if (configMode) {
    server.handleClient();
    yield();
    
    if (!configMode) {
      Serial.println("🌐 Saindo do modo AP via web...");
      server.stop();
      WiFi.disconnect();
      WiFi.mode(WIFI_STA);
      delay(1000);
      return;
    }
    
    // Menu serial no modo configuração
    if (Serial.available()) {
      char cmd = Serial.peek();
      if (cmd == 'm' || cmd == 'M') {
        Serial.read();
        Serial.println("\n=== MENU CONFIGURAÇÃO ===");
        showMenu();
        
        unsigned long menuStart = millis();
        while (millis() - menuStart < 30000) {
          handleSerialMenu();
          if (Serial.available() && (Serial.peek() == 'q' || Serial.peek() == 'Q')) {
            Serial.read();
            Serial.println("\nVoltando ao modo configuração...");
            break;
          }
          delay(50);
          yield();
        }
      } else {
        Serial.read();
      }
    }
    
    // Verificar botão BOOT
    if (bootButtonPressed) {
      bootButtonPressed = false;
      Serial.println("🔧 BOOT - Desligando servidor web...");
      configMode = false;
      server.stop();
      WiFi.disconnect();
      WiFi.mode(WIFI_STA);
      delay(500);
      return;
    }
    
    // Timeout do modo configuração
    if (millis() - configModeStart > 600000) {
      Serial.println("⏰ TIMEOUT - Saindo do modo configuração...");
      configMode = false;
      server.stop();
      WiFi.disconnect();
      WiFi.mode(WIFI_STA);
      delay(1000);
      return;
    }
    
    static unsigned long lastMsg = 0;
    if (millis() - lastMsg > 10000) {
      unsigned long remaining = (600000 - (millis() - configModeStart)) / 1000;
      float battery = getBatteryLevel();
      Serial.printf("🔧 MODO EMERGÊNCIA - %lu segundos restantes | 🔋 %.1f%%\n", remaining, battery);
      lastMsg = millis();
    }
    
    delay(100);
    yield();
    return;
  }

  // NOVO SISTEMA DE MODOS DE OPERAÇÃO
  switch (modeState.currentMode) {
    case MODE_STARTUP:
      handleStartupMode();
      break;
      
    case MODE_BASE:
      handleBaseMode();
      break;
      
    case MODE_TRAVEL:
      handleTravelMode();
      break;
  }
  
  // Verificar botão BOOT para modo emergência
  if (checkBootButtonAndStartAP()) {
    return;
  }
  
  // Menu serial
  if (Serial.available()) {
    char cmd = Serial.peek();
    if (cmd == 'm' || cmd == 'M') {
      Serial.read();
      Serial.println("\n🔧 MENU INTERATIVO...");
      showMenu();
      
      unsigned long menuStart = millis();
      while (millis() - menuStart < 30000) {
        handleSerialMenu();
        if (Serial.available() && (Serial.peek() == 'q' || Serial.peek() == 'Q')) {
          Serial.read();
          Serial.println("\nVoltando ao modo normal...");
          return;
        }
        delay(50);
        yield();
      }
      return;
    } else {
      Serial.read();
    }
  }
  
  // Status detalhado
  float battery = getBatteryLevel();
  String modeStr = getModeString(modeState.currentMode);
  
  Serial.println("\n" + repeatChar('=', 60));
  Serial.printf("🚲 Bike %s | 📍 %s | 🔋 %.1f%% | 📦 %d arquivos\n", 
                config.bikeId, modeStr.c_str(), battery, dataCount);
  
  if (modeState.currentMode == MODE_BASE) {
    Serial.printf("🏠 Base: %s | IP: %s | RSSI: %d\n", 
                  WiFi.SSID().c_str(), 
                  WiFi.localIP().toString().c_str(), 
                  WiFi.RSSI());
  } else if (modeState.currentMode == MODE_TRAVEL) {
    Serial.printf("🚴 Viagem: %d redes detectadas | Coletando dados\n", networkCount);
  } else {
    Serial.printf("⚡ Inicialização: Configurando sistema...\n");
  }
  
  Serial.printf("⏱️ Uptime: %lu s | 💾 Heap: %d bytes | Config: %s\n", 
                millis()/1000, ESP.getFreeHeap(), 
                onlineConfig.configSynced ? "✅ Online" : "⚠️ Local");
  Serial.println("💬 Digite 'm' para menu");
  Serial.println(repeatChar('=', 60) + "\n");
  
  // Delay baseado no modo atual
  int delayTime = getDelayForCurrentMode();
  unsigned long delayStart = millis();
  
  while (millis() - delayStart < delayTime) {
    if (checkBootButtonAndStartAP()) return;
    
    if (Serial.available()) {
      char cmd = Serial.peek();
      if (cmd == 'm' || cmd == 'M') {
        Serial.read();
        return; // Interromper delay para menu
      } else {
        Serial.read();
      }
    }
    
    yield();
    delay(100);
  }
}