#include "config.h"
#include <LittleFS.h>
#include <Arduino.h>

String readFile(const char* path) {
  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial.printf("Falha ao abrir arquivo: %s\n", path);
    return "";
  }
  String content = file.readString();
  file.close();
  return content;
}

void writeFile(const char* path, const String& content) {
  File file = LittleFS.open(path, "w");
  if (!file) {
    Serial.printf("Falha ao criar arquivo: %s\n", path);
    return;
  }
  file.print(content);
  file.close();
}

String getConfigValue(const String& content, const String& key) {
  int keyIndex = content.indexOf(key + "=");
  if (keyIndex == -1) return "";
  
  int startIndex = keyIndex + key.length() + 1;
  int endIndex = content.indexOf('\n', startIndex);
  if (endIndex == -1) endIndex = content.length();
  
  String value = content.substring(startIndex, endIndex);
  value.trim();
  return value;
}

void applyCollectMode(const char* mode) {
  if (strcmp(mode, "mega_economico") == 0) {
    config.scanTimeActive = 30000;   // 30s
    config.scanTimeInactive = 120000; // 2min
    Serial.println("Modo: Mega Econômico (30s/2min)");
  } else if (strcmp(mode, "economico") == 0) {
    config.scanTimeActive = 15000;   // 15s
    config.scanTimeInactive = 60000; // 1min
    Serial.println("Modo: Econômico (15s/1min)");
  } else if (strcmp(mode, "normal") == 0) {
    config.scanTimeActive = 5000;    // 5s
    config.scanTimeInactive = 30000; // 30s
    Serial.println("Modo: Normal (5s/30s)");
  } else if (strcmp(mode, "intensivo") == 0) {
    config.scanTimeActive = 2000;    // 2s
    config.scanTimeInactive = 15000; // 15s
    Serial.println("Modo: Intensivo (2s/15s)");
  } else if (strcmp(mode, "extremo") == 0) {
    config.scanTimeActive = 1000;    // 1s
    config.scanTimeInactive = 5000;  // 5s
    Serial.println("Modo: Extremo (1s/5s)");
  } else if (strcmp(mode, "personalizado") == 0) {
    Serial.printf("Modo: Personalizado (%dms/%dms)\n", config.scanTimeActive, config.scanTimeInactive);
  } else {
    Serial.println("Modo desconhecido, usando Normal");
    strcpy(config.collectMode, "normal");
    applyCollectMode("normal");
  }
}

void loadConfig() {
  // Valores padrão
  strcpy(config.bikeId, "sl01");
  strcpy(config.collectMode, "normal");
  config.scanTimeActive = 5000;
  config.scanTimeInactive = 30000;
  strcpy(config.baseSSID1, "");
  strcpy(config.basePassword1, "");
  strcpy(config.baseSSID2, "");
  strcpy(config.basePassword2, "");
  strcpy(config.baseSSID3, "");
  strcpy(config.basePassword3, "");
  strcpy(config.firebaseUrl, "");
  strcpy(config.firebaseKey, "");
  config.cleanupEnabled = false;
  config.maxUploadsHistory = 10;
  config.baseProximityRssi = -80;
  config.batteryCalibration = 1.0;
  strcpy(config.apPassword, "12345678");
  config.batteryLowThreshold = 15.0;
  config.batteryCriticalThreshold = 5.0;
  config.lowBatteryAlertEnabled = true;
  config.statusUpdateIntervalMinutes = 60;
  config.statusUpdateEnabled = true;
  
  if (!LittleFS.begin()) {
    Serial.println("Sistema de arquivos não disponível - usando configuração padrão");
    return;
  }
  
  Serial.println("Carregando configurações do config.txt...");
  
  String configContent = readFile("/config.txt");
  if (configContent.length() == 0) {
    Serial.println("Arquivo config.txt não encontrado - usando configuração padrão");
    return;
  }
  
  // Carregar todas as configurações
  String value;
  
  value = getConfigValue(configContent, "BIKE_ID");
  if (value.length() > 0) {
    value.toCharArray(config.bikeId, 10);
    Serial.printf("Bike ID: %s\n", config.bikeId);
  }
  
  // Carregar modo de coleta e aplicar timings
  value = getConfigValue(configContent, "COLLECT_MODE");
  if (value.length() > 0) {
    value.toCharArray(config.collectMode, 20);
    applyCollectMode(config.collectMode);
  } else {
    applyCollectMode("normal");
  }
  
  // Permitir override manual dos timings se especificados
  value = getConfigValue(configContent, "SCAN_TIME_ACTIVE");
  if (value.length() > 0) {
    config.scanTimeActive = value.toInt();
    Serial.println("Override manual do SCAN_TIME_ACTIVE");
  }
  
  value = getConfigValue(configContent, "SCAN_TIME_INACTIVE");
  if (value.length() > 0) {
    config.scanTimeInactive = value.toInt();
    Serial.println("Override manual do SCAN_TIME_INACTIVE");
  }
  
  Serial.printf("Timing final: %d/%d ms\n", config.scanTimeActive, config.scanTimeInactive);
  
  value = getConfigValue(configContent, "BASE1_SSID");
  if (value.length() > 0) value.toCharArray(config.baseSSID1, 32);
  
  value = getConfigValue(configContent, "BASE1_PASSWORD");
  if (value.length() > 0) value.toCharArray(config.basePassword1, 32);
  
  value = getConfigValue(configContent, "BASE2_SSID");
  if (value.length() > 0) value.toCharArray(config.baseSSID2, 32);
  
  value = getConfigValue(configContent, "BASE2_PASSWORD");
  if (value.length() > 0) value.toCharArray(config.basePassword2, 32);
  
  value = getConfigValue(configContent, "BASE3_SSID");
  if (value.length() > 0) value.toCharArray(config.baseSSID3, 32);
  
  value = getConfigValue(configContent, "BASE3_PASSWORD");
  if (value.length() > 0) value.toCharArray(config.basePassword3, 32);
  
  Serial.printf("Base 1: %s\n", config.baseSSID1);
  Serial.printf("Base 2: %s\n", config.baseSSID2);
  Serial.printf("Base 3: %s\n", config.baseSSID3);
  
  value = getConfigValue(configContent, "FIREBASE_URL");
  if (value.length() > 0) value.toCharArray(config.firebaseUrl, 128);
  
  value = getConfigValue(configContent, "FIREBASE_KEY");
  if (value.length() > 0) value.toCharArray(config.firebaseKey, 64);
  
  value = getConfigValue(configContent, "CLEANUP_ENABLED");
  if (value.length() > 0) config.cleanupEnabled = value.toInt() == 1;
  
  value = getConfigValue(configContent, "MAX_UPLOADS_HISTORY");
  if (value.length() > 0) config.maxUploadsHistory = value.toInt();
  
  value = getConfigValue(configContent, "BASE_PROXIMITY_RSSI");
  if (value.length() > 0) config.baseProximityRssi = value.toInt();
  
  value = getConfigValue(configContent, "BATTERY_CALIBRATION");
  if (value.length() > 0) config.batteryCalibration = value.toFloat();
  
  value = getConfigValue(configContent, "AP_PASSWORD");
  if (value.length() > 0) value.toCharArray(config.apPassword, 32);
  
  value = getConfigValue(configContent, "BATTERY_LOW_THRESHOLD");
  if (value.length() > 0) config.batteryLowThreshold = value.toFloat();
  
  value = getConfigValue(configContent, "BATTERY_CRITICAL_THRESHOLD");
  if (value.length() > 0) config.batteryCriticalThreshold = value.toFloat();
  
  value = getConfigValue(configContent, "LOW_BATTERY_ALERT_ENABLED");
  if (value.length() > 0) config.lowBatteryAlertEnabled = value.toInt() == 1;
  
  value = getConfigValue(configContent, "STATUS_UPDATE_INTERVAL_MINUTES");
  if (value.length() > 0) config.statusUpdateIntervalMinutes = value.toInt();
  
  value = getConfigValue(configContent, "STATUS_UPDATE_ENABLED");
  if (value.length() > 0) config.statusUpdateEnabled = value.toInt() == 1;
  
  Serial.printf("Firebase: %s\n", strlen(config.firebaseUrl) > 0 ? "Configurado" : "Não configurado");
  Serial.printf("Cleanup: %s, Histórico: %d\n", config.cleanupEnabled ? "Ativado" : "Desativado", config.maxUploadsHistory);
  Serial.printf("RSSI Proximidade: %d dBm\n", config.baseProximityRssi);
  Serial.println("Configurações carregadas do config.txt");
}

void saveConfig() {
  if (!LittleFS.begin()) {
    Serial.println("Falha ao montar sistema de arquivos");
    return;
  }
  
  String configContent = "# Configurações da Bicicleta WiFi Scanner\n";
  configContent += "BIKE_ID=" + String(config.bikeId) + "\n\n";
  
  configContent += "# Modo de coleta (mega_economico, economico, normal, intensivo, extremo)\n";
  configContent += "COLLECT_MODE=" + String(config.collectMode) + "\n\n";
  
  configContent += "# Tempos de scan (em milissegundos) - configurados automaticamente pelo modo\n";
  configContent += "SCAN_TIME_ACTIVE=" + String(config.scanTimeActive) + "\n";
  configContent += "SCAN_TIME_INACTIVE=" + String(config.scanTimeInactive) + "\n\n";
  
  configContent += "# Bases WiFi (até 3 bases)\n";
  configContent += "BASE1_SSID=" + String(config.baseSSID1) + "\n";
  configContent += "BASE1_PASSWORD=" + String(config.basePassword1) + "\n";
  configContent += "BASE2_SSID=" + String(config.baseSSID2) + "\n";
  configContent += "BASE2_PASSWORD=" + String(config.basePassword2) + "\n";
  configContent += "BASE3_SSID=" + String(config.baseSSID3) + "\n";
  configContent += "BASE3_PASSWORD=" + String(config.basePassword3) + "\n\n";
  
  configContent += "# Firebase\n";
  configContent += "FIREBASE_URL=" + String(config.firebaseUrl) + "\n";
  configContent += "FIREBASE_KEY=" + String(config.firebaseKey) + "\n\n";
  
  configContent += "# Detecção de proximidade (RSSI mínimo para considerar próximo da base)\n";
  configContent += "BASE_PROXIMITY_RSSI=" + String(config.baseProximityRssi) + "\n\n";
  
  configContent += "# Calibração da bateria\n";
  configContent += "BATTERY_CALIBRATION=" + String(config.batteryCalibration, 3) + "\n\n";
  
  configContent += "# Senha do AP\n";
  configContent += "AP_PASSWORD=" + String(config.apPassword) + "\n\n";
  
  configContent += "# Limpeza de dados após upload\n";
  configContent += "CLEANUP_ENABLED=" + String(config.cleanupEnabled ? 1 : 0) + "\n";
  configContent += "MAX_UPLOADS_HISTORY=" + String(config.maxUploadsHistory) + "\n\n";
  
  configContent += "# Alerta de bateria baixa\n";
  configContent += "BATTERY_LOW_THRESHOLD=" + String(config.batteryLowThreshold, 1) + "\n";
  configContent += "BATTERY_CRITICAL_THRESHOLD=" + String(config.batteryCriticalThreshold, 1) + "\n";
  configContent += "LOW_BATTERY_ALERT_ENABLED=" + String(config.lowBatteryAlertEnabled ? 1 : 0) + "\n\n";
  
  configContent += "# Atualizações programadas de status\n";
  configContent += "STATUS_UPDATE_INTERVAL_MINUTES=" + String(config.statusUpdateIntervalMinutes) + "\n";
  configContent += "STATUS_UPDATE_ENABLED=" + String(config.statusUpdateEnabled ? 1 : 0) + "\n";
  
  writeFile("/config.txt", configContent);
  Serial.println("Configurações salvas em config.txt");
}