#include "config_manager.h"

ConfigManager::ConfigManager() {
  setDefaults();
}

void ConfigManager::setDefaults() {
  bikeId = "bike_001";
  baseBleName = "BPR Base Station";
  scanIntervalSec = 300;
  scanIntervalLowBattSec = 900;
  deepSleepSec = 3600;
  minBatteryVoltage = 3.45;
  maxWifiRecords = 200;
  bleScanTimeoutSec = 5;
  emergencyTimeoutSec = 10;
  statusReportIntervalSec = 30;
  ledEnabled = true;
  debugEnabled = true;
  configTimestamp = 0;
}

bool ConfigManager::loadFromFile(const char* filename) {
  if (!LittleFS.exists(filename)) {
    Serial.printf("⚠️ Config file %s não existe, usando padrões\n", filename);
    return false;
  }
  
  File file = LittleFS.open(filename, "r");
  if (!file) {
    Serial.printf("❌ Erro ao abrir %s\n", filename);
    return false;
  }
  
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error) {
    Serial.printf("❌ Erro JSON: %s\n", error.c_str());
    return false;
  }
  
  // Carregar configurações
  bikeId = doc["bike_id"] | bikeId;
  baseBleName = doc["base_ble_name"] | baseBleName;
  scanIntervalSec = doc["scan_interval_sec"] | scanIntervalSec;
  scanIntervalLowBattSec = doc["scan_interval_low_batt_sec"] | scanIntervalLowBattSec;
  deepSleepSec = doc["deep_sleep_sec"] | deepSleepSec;
  minBatteryVoltage = doc["min_battery_voltage"] | minBatteryVoltage;
  maxWifiRecords = doc["max_wifi_records"] | maxWifiRecords;
  bleScanTimeoutSec = doc["ble_scan_timeout_sec"] | bleScanTimeoutSec;
  emergencyTimeoutSec = doc["emergency_timeout_sec"] | emergencyTimeoutSec;
  statusReportIntervalSec = doc["status_report_interval_sec"] | statusReportIntervalSec;
  ledEnabled = doc["led_enabled"] | ledEnabled;
  debugEnabled = doc["debug_enabled"] | debugEnabled;
  configTimestamp = doc["config_timestamp"] | configTimestamp;
  
  Serial.printf("✅ Config carregada: %s\n", filename);
  return true;
}

bool ConfigManager::saveToFile(const char* filename) {
  DynamicJsonDocument doc(1024);
  
  doc["bike_id"] = bikeId;
  doc["base_ble_name"] = baseBleName;
  doc["scan_interval_sec"] = scanIntervalSec;
  doc["scan_interval_low_batt_sec"] = scanIntervalLowBattSec;
  doc["deep_sleep_sec"] = deepSleepSec;
  doc["min_battery_voltage"] = minBatteryVoltage;
  doc["max_wifi_records"] = maxWifiRecords;
  doc["ble_scan_timeout_sec"] = bleScanTimeoutSec;
  doc["emergency_timeout_sec"] = emergencyTimeoutSec;
  doc["status_report_interval_sec"] = statusReportIntervalSec;
  doc["led_enabled"] = ledEnabled;
  doc["debug_enabled"] = debugEnabled;
  doc["config_timestamp"] = configTimestamp;
  
  File file = LittleFS.open(filename, "w");
  if (!file) {
    Serial.printf("❌ Erro ao salvar %s\n", filename);
    return false;
  }
  
  serializeJson(doc, file);
  file.close();
  
  Serial.printf("💾 Config salva: %s\n", filename);
  return true;
}

bool ConfigManager::updateFromBLE(const String& jsonConfig) {
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, jsonConfig);
  
  if (error) {
    Serial.printf("❌ Config BLE inválida: %s\n", error.c_str());
    return false;
  }
  
  // Atualizar apenas campos presentes
  if (doc.containsKey("scan_interval_sec")) {
    scanIntervalSec = doc["scan_interval_sec"];
  }
  if (doc.containsKey("scan_interval_low_batt_sec")) {
    scanIntervalLowBattSec = doc["scan_interval_low_batt_sec"];
  }
  if (doc.containsKey("deep_sleep_sec")) {
    deepSleepSec = doc["deep_sleep_sec"];
  }
  if (doc.containsKey("min_battery_voltage")) {
    minBatteryVoltage = doc["min_battery_voltage"];
  }
  if (doc.containsKey("base_ble_name")) {
    baseBleName = doc["base_ble_name"].as<String>();
  }
  if (doc.containsKey("config_timestamp")) {
    configTimestamp = doc["config_timestamp"];
  }
  
  Serial.println("📥 Config atualizada via BLE");
  saveToFile(); // Persistir mudanças
  return true;
}

void ConfigManager::printConfig() {
  Serial.println("\n📋 CONFIGURAÇÃO ATUAL:");
  Serial.printf("🆔 Bike ID: %s\n", bikeId.c_str());
  Serial.printf("🏠 Base BLE: %s\n", baseBleName.c_str());
  Serial.printf("📡 Scan interval: %ds / %ds (low batt)\n", scanIntervalSec, scanIntervalLowBattSec);
  Serial.printf("💤 Deep sleep: %ds\n", deepSleepSec);
  Serial.printf("🔋 Min battery: %.2fV\n", minBatteryVoltage);
  Serial.printf("📦 Max records: %d\n", maxWifiRecords);
  Serial.printf("⏱️ Timeouts: BLE %ds, Emergency %ds\n", bleScanTimeoutSec, emergencyTimeoutSec);
  Serial.printf("💡 LED: %s | 🐛 Debug: %s\n", ledEnabled ? "ON" : "OFF", debugEnabled ? "ON" : "OFF");
  Serial.printf("🕐 Config timestamp: %u\n", configTimestamp);
  Serial.println();
}