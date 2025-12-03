#include "diagnostics.h"
#include "config.h"
#include <LittleFS.h>
#include <WiFi.h>

void runDiagnostics() {
  Serial.println("\n" + String('=') + String('=') + String('=') + String('=') + String('='));
  Serial.println("🔍 DIAGNÓSTICO RÁPIDO - BOTÃO FLASH");
  Serial.println(String('=') + String('=') + String('=') + String('=') + String('='));
  
  // Diagnóstico rápido para botão
  Serial.printf("🆔 Bike: %s | 🔋 %.1f%% | ⏰ %ds uptime\n", 
                config.bikeId, getBatteryLevel(), millis()/1000);
  Serial.printf("📡 WiFi: %d redes | 📦 %d arquivos | 🏠 %s\n", 
                networkCount, dataCount, config.isAtBase ? "BASE" : "MOVIMENTO");
  Serial.printf("💾 LittleFS: %d/%d bytes\n", LittleFS.usedBytes(), LittleFS.totalBytes());
  
  Serial.println("\n🔍 DIAGNÓSTICO COMPLETO DO SISTEMA");
  Serial.println(String('=') + String('=') + String('=') + String('=') + String('='));
  
  // 1. Sistema de arquivos
  Serial.println("📁 SISTEMA DE ARQUIVOS:");
  if (LittleFS.begin()) {
    Serial.println("✅ LittleFS montado com sucesso");
    
    File root = LittleFS.open("/");
    Serial.println("📂 Arquivos encontrados:");
    File file = root.openNextFile();
    int fileCount = 0;
    while (file) {
      Serial.printf("   📄 %s (%d bytes)\n", file.name(), file.size());
      fileCount++;
      file.close();
      file = root.openNextFile();
    }
    root.close();
    Serial.printf("📊 Total: %d arquivos\n", fileCount);
    
    // Verificar espaço livre
    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes = LittleFS.usedBytes();
    Serial.printf("💾 Espaço: %d/%d bytes (%.1f%% usado)\n", 
                  usedBytes, totalBytes, (float)usedBytes/totalBytes*100);
  } else {
    Serial.println("❌ Falha ao montar LittleFS");
  }
  
  // 2. Configurações
  Serial.println("\n⚙️ CONFIGURAÇÕES:");
  Serial.printf("🆔 Bike ID: %s\n", config.bikeId);
  Serial.printf("📡 Base 1: %s\n", strlen(config.baseSSID1) > 0 ? config.baseSSID1 : "Não configurada");
  Serial.printf("📡 Base 2: %s\n", strlen(config.baseSSID2) > 0 ? config.baseSSID2 : "Não configurada");
  Serial.printf("📡 Base 3: %s\n", strlen(config.baseSSID3) > 0 ? config.baseSSID3 : "Não configurada");
  Serial.printf("🔥 Firebase: %s\n", strlen(config.firebaseUrl) > 0 ? "Configurado" : "Não configurado");
  Serial.printf("⏱️ Timing: %d/%d ms\n", config.scanTimeActive, config.scanTimeInactive);
  Serial.printf("🧹 Cleanup: %s\n", config.cleanupEnabled ? "Ativado" : "Desativado");
  
  // 3. WiFi
  Serial.println("\n📶 WIFI:");
  Serial.printf("📊 Status: %s\n", WiFi.status() == WL_CONNECTED ? "Conectado" : "Desconectado");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("🌐 IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("📡 SSID: %s\n", WiFi.SSID().c_str());
    Serial.printf("📶 RSSI: %d dBm\n", WiFi.RSSI());
  }
  
  // 4. Teste de scan
  Serial.println("\n🔍 TESTE DE SCAN:");
  int n = WiFi.scanNetworks();
  Serial.printf("📡 Redes encontradas: %d\n", n);
  for (int i = 0; i < min(n, 5); i++) {
    Serial.printf("   %d. %s (%d dBm)\n", i+1, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
  }
  
  // 5. Teste de escrita
  Serial.println("\n💾 TESTE DE ESCRITA:");
  String testFile = "/test_" + String(millis()) + ".txt";
  File file = LittleFS.open(testFile.c_str(), "w");
  if (file) {
    file.println("Teste de escrita: " + String(millis()));
    file.close();
    Serial.println("✅ Escrita OK");
    
    // Ler de volta
    file = LittleFS.open(testFile.c_str(), "r");
    if (file) {
      String content = file.readString();
      file.close();
      Serial.println("✅ Leitura OK: " + content.substring(0, 30) + "...");
      
      // Remover arquivo de teste
      LittleFS.remove(testFile);
      Serial.println("✅ Remoção OK");
    } else {
      Serial.println("❌ Falha na leitura");
    }
  } else {
    Serial.println("❌ Falha na escrita");
  }
  
  // 6. Variáveis globais
  Serial.println("\n🔢 VARIÁVEIS GLOBAIS:");
  Serial.printf("📊 networkCount: %d\n", networkCount);
  Serial.printf("📦 dataCount: %d\n", dataCount);
  Serial.printf("🏠 isAtBase: %s\n", config.isAtBase ? "Sim" : "Não");
  Serial.printf("⏰ timeSync: %s\n", timeSync ? "Sim" : "Não");
  
  Serial.println("\n" + String('=').substring(0,50));
  Serial.println("🏁 DIAGNÓSTICO CONCLUÍDO");
  Serial.println(String('=').substring(0,50) + "\n");
}

void testDataStorage() {
  Serial.println("\n🧪 TESTE DE ARMAZENAMENTO DE DADOS");
  
  // Simular dados de scan
  Serial.println("📡 Simulando scan...");
  WiFi.scanNetworks(); // Scan real
  
  Serial.println("💾 Testando storeData()...");
  storeData();
  
  Serial.printf("📊 dataCount após store: %d\n", dataCount);
  
  // Listar arquivos criados
  File root = LittleFS.open("/");
  Serial.println("📂 Arquivos após storeData():");
  File file = root.openNextFile();
  while (file) {
    String fileName = file.name();
    if (fileName.startsWith("/scan_")) {
      Serial.printf("   📄 %s (%d bytes)\n", fileName.c_str(), file.size());
      
      // Mostrar conteúdo do arquivo
      String content = file.readString();
      Serial.printf("   📝 Conteúdo: %s\n", content.substring(0, 100).c_str());
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
}