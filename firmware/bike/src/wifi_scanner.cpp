#include "wifi_scanner.h"
#include "status_tracker.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <Arduino.h>

String getBasePassword(String ssid) {
  if (ssid == String(config.baseSSID1))
    return String(config.basePassword1);
  if (ssid == String(config.baseSSID2))
    return String(config.basePassword2);
  if (ssid == String(config.baseSSID3))
    return String(config.basePassword3);
  return "";
}

void scanWiFiNetworks() {
  Serial.println("🔍 Iniciando scan WiFi...");
  Serial.printf("WiFi Mode: %d, Status: %d\n", WiFi.getMode(), WiFi.status());
  
  // Garantir que WiFi está em modo STA e limpar cache
  if (WiFi.getMode() != WIFI_STA) {
    Serial.println("⚠️ WiFi não está em modo STA, corrigindo...");
    WiFi.mode(WIFI_STA);
    delay(100);
  }
  
  // Limpar cache de redes antigas
  WiFi.scanDelete();
  
  int n = WiFi.scanNetworks();
  Serial.printf("📡 Scan retornou: %d redes\n", n);
  
  if (n == -1) {
    Serial.println("❌ ERRO: Scan falhou (-1)");
    return;
  }
  if (n == -2) {
    Serial.println("⚠️ Scan em progresso, aguardando...");
    delay(2000);
    n = WiFi.scanComplete();
    if (n < 0) {
      Serial.println("❌ ERRO: Scan ainda em progresso após espera");
      return;
    }
    Serial.printf("📡 Scan completado: %d redes\n", n);
  }
  
  networkCount = 0;

  for (int i = 0; i < n && networkCount < 30; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() > 0) {
      ssid.toCharArray(networks[networkCount].ssid, 32);
      WiFi.BSSIDstr(i).toCharArray(networks[networkCount].bssid, 18);
      networks[networkCount].rssi = WiFi.RSSI(i);
      networks[networkCount].channel = WiFi.channel(i);
      networks[networkCount].encryption = WiFi.encryptionType(i);
      Serial.printf("  %d: %s (%d dBm, Ch %d)\n", i+1, networks[networkCount].ssid, 
                    networks[networkCount].rssi, networks[networkCount].channel);
      networkCount++;
    }
  }
  
  if (networkCount == 0) {
    Serial.println("❌ PROBLEMA: Nenhuma rede WiFi detectada!");
    Serial.println("   - Verifique antena WiFi");
    Serial.println("   - Verifique se há redes próximas");
    Serial.println("   - Possível problema de hardware");
  }
}

bool checkAtBase() {
  // Verificar se já está conectado em uma base
  if (WiFi.status() == WL_CONNECTED) {
    String currentSSID = WiFi.SSID();
    if (currentSSID == String(config.baseSSID1) || 
        currentSSID == String(config.baseSSID2) || 
        currentSSID == String(config.baseSSID3)) {
      Serial.printf("🏠 Já conectado na base: %s (RSSI: %d)\n", currentSSID.c_str(), WiFi.RSSI());
      return true;
    }
  }
  
  // Se não conectado, verificar proximidade das bases
  for (int i = 0; i < networkCount; i++) {
    if (strlen(config.baseSSID1) > 0 && strcmp(networks[i].ssid, config.baseSSID1) == 0) {
      Serial.printf("Base1 encontrada: %s (RSSI: %d, min: %d)\n", networks[i].ssid, networks[i].rssi, config.baseProximityRssi);
      if (networks[i].rssi > config.baseProximityRssi) return true;
    }
    if (strlen(config.baseSSID2) > 0 && strcmp(networks[i].ssid, config.baseSSID2) == 0) {
      Serial.printf("Base2 encontrada: %s (RSSI: %d, min: %d)\n", networks[i].ssid, networks[i].rssi, config.baseProximityRssi);
      if (networks[i].rssi > config.baseProximityRssi) return true;
    }
    if (strlen(config.baseSSID3) > 0 && strcmp(networks[i].ssid, config.baseSSID3) == 0) {
      Serial.printf("Base3 encontrada: %s (RSSI: %d, min: %d)\n", networks[i].ssid, networks[i].rssi, config.baseProximityRssi);
      if (networks[i].rssi > config.baseProximityRssi) return true;
    }
  }
  
  Serial.println("🚶 Nenhuma base próxima - modo coleta ativo");
  return false;
}

bool connectToBase() {
  for (int i = 0; i < networkCount; i++) {
    String basePass = getBasePassword(String(networks[i].ssid));
    if (basePass != "") {
      Serial.printf("Conectando à base: %s (RSSI: %d)\n", networks[i].ssid, networks[i].rssi);
      
      // Limpar conexão anterior
      WiFi.disconnect();
      delay(100);
      
      WiFi.begin(networks[i].ssid, basePass.c_str());

      int attempts = 0;
      while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
        Serial.printf("  Tentativa %d/20 - Status: %d\n", attempts, WiFi.status());
      }

      if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("Conectado à base %s!\n", networks[i].ssid);
        Serial.printf("IP obtido: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
        trackConnection(networks[i].ssid, WiFi.localIP().toString().c_str(), true);
        return true;
      } else {
        Serial.printf("❌ Falha ao conectar em %s (Status final: %d)\n", networks[i].ssid, WiFi.status());
        Serial.println("   → Rede pode estar inativa ou com senha incorreta");
      }
    }
  }
  return false;
}

void storeData() {
  if (networkCount == 0) {
    Serial.println("⚠️ Nenhuma rede encontrada - não salvando dados");
    return;
  }
  
  String filename = "/scan_" + String(millis()) + ".json";
  
  // Formato: [timestamp,realTime,batteryLevel,[[ssid,bssid,rssi,channel]]]
  String data = "[" + String(millis());
  
  // Usar tempo real se NTP estiver sincronizado (definido externamente)
  extern bool timeSync;
  extern unsigned long currentRealTime;
  
  if (timeSync && currentRealTime > 0) {
    data += "," + String(currentRealTime);
    Serial.printf("🕰️ Usando tempo NTP: %lu\n", currentRealTime);
  } else {
    data += ",0"; // Tempo real não disponível
    Serial.println("⚠️ NTP não sincronizado - usando timestamp 0");
  }
  
  // Debug para verificar estado NTP
  Serial.printf("🔍 Debug NTP: timeSync=%s, currentRealTime=%lu\n", 
                timeSync ? "true" : "false", currentRealTime);
  
  // Adicionar nível de bateria
  float battery = getBatteryLevel();
  data += "," + String(battery, 1);
  
  data += ",[";
  
  int maxNets = min(networkCount, 10);
  for (int i = 0; i < maxNets; i++) {
    if (i > 0) data += ",";
    data += "[\"" + String(networks[i].ssid) + "\",\"";
    data += String(networks[i].bssid) + "\",";
    data += String(networks[i].rssi) + ",";
    data += String(networks[i].channel) + "]";
  }
  data += "]]";
  
  Serial.printf("💾 Salvando: %s (%d bytes)\n", filename.c_str(), data.length());
  
  File file = LittleFS.open(filename.c_str(), "w");
  if (file) {
    size_t written = file.print(data);
    file.close();
    
    if (written > 0) {
      dataCount++;
      Serial.printf("✅ Dados salvos! Buffer: %d arquivos\n", dataCount);
    } else {
      Serial.println("❌ Falha ao escrever dados");
    }
  } else {
    Serial.printf("❌ Falha ao criar arquivo: %s\n", filename.c_str());
  }
  
  // Track battery separately
  trackBattery(battery);
  // Debug já é mostrado na função getBatteryLevel()
}

float getBatteryVoltage() {
  uint32_t totalVoltage = 0;
  for(int i = 0; i < 16; i++) {
    totalVoltage += analogReadMilliVolts(A0);
  }
  float avgVoltage = totalVoltage / 16.0;
  return (avgVoltage * 2.0 * config.batteryCalibration) / 1000.0;
}

float getBatteryLevel() {
  // Leitura correta para XIAO ESP32-C3 com divisor de tensão
  // Usar analogReadMilliVolts() com correção automática do chip
  // Média de 16 leituras para remover ruído/spikes
  
  uint32_t totalVoltage = 0;
  for(int i = 0; i < 16; i++) {
    totalVoltage += analogReadMilliVolts(A0); // ADC com correção
  }
  
  // Média das leituras em mV
  float avgVoltage = totalVoltage / 16.0;
  
  // Divisor de tensão 1:1 (220k + 220k), então tensão real é o dobro
  float batteryVoltage = (avgVoltage * 2.0) / 1000.0; // Converter mV para V
  
  // Converter tensão para porcentagem (Li-ion: 3.0V = 0%, 4.2V = 100%)
  float percentage = ((batteryVoltage - 3.0) / (4.2 - 3.0)) * 100.0;
  
  // Limitar entre 0% e 100%
  if (percentage < 0) percentage = 0;
  if (percentage > 100) percentage = 100;
  
  // Debug com calibração
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug > 10000) { // Debug a cada 10s
    Serial.printf("🔋 Bateria: %.3fV (%.1f%%) - ADC: %.0fmV - Cal: %.3f\n", 
                  batteryVoltage, percentage, avgVoltage, config.batteryCalibration);
    lastDebug = millis();
  }
  
  return percentage;
}

bool needsLowBatteryAlert() {
  if (!config.lowBatteryAlertEnabled) return false;
  
  float batteryLevel = getBatteryLevel();
  
  // Verificar se bateria está baixa
  if (batteryLevel <= config.batteryLowThreshold) {
    // Verificar se já foi enviado alerta recente (evitar spam)
    File alertFile = LittleFS.open("/last_battery_alert.txt", "r");
    if (alertFile) {
      unsigned long lastAlert = alertFile.readString().toInt();
      alertFile.close();
      
      unsigned long now = millis() / 1000;
      if (now - lastAlert < 1800) { // 30 minutos
        return false; // Alerta muito recente
      }
    }
    
    Serial.printf("🚨 BATERIA BAIXA: %.1f%% (threshold: %.1f%%)\n", 
                  batteryLevel, config.batteryLowThreshold);
    return true;
  }
  
  return false;
}

bool needsScheduledStatusUpdate() {
  if (!config.statusUpdateEnabled) return false;
  
  // Verificar última atualização de status
  File statusFile = LittleFS.open("/last_status_update.txt", "r");
  if (statusFile) {
    unsigned long lastUpdate = statusFile.readString().toInt();
    statusFile.close();
    
    unsigned long now = millis() / 1000;
    unsigned long minutesSince = (now - lastUpdate) / 60;
    
    if (minutesSince >= config.statusUpdateIntervalMinutes) {
      Serial.printf("📈 STATUS UPDATE: %lu min desde última atualização\n", minutesSince);
      return true;
    }
    return false;
  }
  
  // Primeira execução - sempre atualizar
  Serial.println("📈 STATUS UPDATE: Primeira atualização");
  return true;
}