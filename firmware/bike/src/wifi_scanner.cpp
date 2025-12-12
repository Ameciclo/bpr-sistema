#include "wifi_scanner.h"
#include "bike_config.h"
#include <WiFi.h>
#include <ArduinoJson.h>

#define MAX_RAM_RECORDS 50  // Buffer menor na RAM
#define MAX_RECORDS_PER_FILE 1000  // 1000 registros por arquivo

WiFiScanner::WiFiScanner() : lastScanTime(0), scanCount(0), fileIndex(0) {}

void WiFiScanner::init() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  // Carregar índice do arquivo
  File indexFile = LittleFS.open("/wifi_index.txt", "r");
  if (indexFile) {
    fileIndex = indexFile.parseInt();
    indexFile.close();
  } else {
    fileIndex = 0;
    // Criar arquivo de índice
    indexFile = LittleFS.open("/wifi_index.txt", "w");
    if (indexFile) {
      indexFile.println(0);
      indexFile.close();
    }
  }
  
  // Criar primeiro arquivo WiFi se não existir
  String firstFile = getDataFileName(0);
  if (!LittleFS.exists(firstFile)) {
    File file = LittleFS.open(firstFile, "w");
    if (file) {
      file.println("{\"records\":[]}");
      file.close();
    }
  }
  
  Serial.printf("📁 WiFi storage iniciado - arquivo atual: %d\n", fileIndex);
}

bool WiFiScanner::performScan(uint32_t currentTime) {
  Serial.println("📡 Iniciando scan WiFi...");
  
  int networkCount = WiFi.scanNetworks(false, false, false, 300);
  
  if (networkCount <= 0) {
    Serial.println("❌ Nenhuma rede encontrada");
    return false;
  }
  
  Serial.printf("✅ %d redes encontradas\n", networkCount);
  
  // Limita a 20 redes mais fortes
  int maxNetworks = min(networkCount, 20);
  
  for (int i = 0; i < maxNetworks; i++) {
    WifiRecord record;
    record.timestamp = currentTime;
    record.rssi = WiFi.RSSI(i);
    record.channel = WiFi.channel(i);
    
    // Copia BSSID
    uint8_t* bssid = WiFi.BSSID(i);
    memcpy(record.bssid, bssid, 6);
    
    records.push_back(record);
  }
  
  WiFi.scanDelete();
  lastScanTime = currentTime;
  scanCount++;
  
  // Flush para arquivo se buffer cheio
  if (records.size() >= MAX_RAM_RECORDS) {
    flushToFile();
  }
  
  Serial.printf("📦 RAM: %d | Total: %d registros\n", records.size(), getTotalStoredCount());
  return true;
}

std::vector<WifiRecord>& WiFiScanner::getRecords() {
  return records;
}

void WiFiScanner::clearRecords() {
  records.clear();
}

bool WiFiScanner::hasRecords() {
  return !records.empty() || getTotalStoredCount() > 0;
}

size_t WiFiScanner::getRecordCount() {
  return records.size();
}

size_t WiFiScanner::getTotalStoredCount() {
  size_t total = records.size();
  
  // Contar registros em arquivos
  for (uint16_t i = 0; i <= fileIndex; i++) {
    File file = LittleFS.open(getDataFileName(i), "r");
    if (file) {
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, file);
      total += doc["records"].size();
      file.close();
    }
  }
  
  return total;
}

void WiFiScanner::flushToFile() {
  if (records.empty()) return;
  
  String filename = getDataFileName(fileIndex);
  
  // Carregar arquivo existente ou criar novo
  DynamicJsonDocument doc(8192);
  File file = LittleFS.open(filename, "r");
  if (file) {
    deserializeJson(doc, file);
    file.close();
  } else {
    doc["records"] = JsonArray();
  }
  
  JsonArray recordsArray = doc["records"];
  
  // Adicionar novos registros
  for (const auto& record : records) {
    JsonObject obj = recordsArray.createNestedObject();
    obj["ts"] = record.timestamp;
    obj["rssi"] = record.rssi;
    obj["ch"] = record.channel;
    
    String bssidStr = "";
    for (int i = 0; i < 6; i++) {
      if (i > 0) bssidStr += ":";
      bssidStr += String(record.bssid[i], HEX);
    }
    obj["bssid"] = bssidStr;
  }
  
  // Salvar arquivo
  file = LittleFS.open(filename, "w");
  if (file) {
    serializeJson(doc, file);
    file.close();
    Serial.printf("💾 Salvos %d registros em %s\n", records.size(), filename.c_str());
  }
  
  records.clear();
  
  // Próximo arquivo se necessário
  if (recordsArray.size() >= MAX_RECORDS_PER_FILE) {
    fileIndex++;
    File indexFile = LittleFS.open("/wifi_index.txt", "w");
    if (indexFile) {
      indexFile.println(fileIndex);
      indexFile.close();
    }
  }
}

String WiFiScanner::getDataFileName(uint16_t index) {
  return "/wifi_" + String(index) + ".json";
}

bool WiFiScanner::exportAllData(String& jsonData) {
  // Forçar flush dos dados em RAM
  if (!records.empty()) {
    flushToFile();
  }
  
  DynamicJsonDocument allData(16384);
  JsonArray allRecords = allData.createNestedArray("wifi_scans");
  
  // Carregar todos os arquivos
  for (uint16_t i = 0; i <= fileIndex; i++) {
    File file = LittleFS.open(getDataFileName(i), "r");
    if (file) {
      DynamicJsonDocument doc(8192);
      deserializeJson(doc, file);
      
      JsonArray records = doc["records"];
      for (JsonVariant record : records) {
        allRecords.add(record);
      }
      
      file.close();
    }
  }
  
  serializeJson(allData, jsonData);
  Serial.printf("📤 Exportados %d registros WiFi\n", allRecords.size());
  return true;
}

void WiFiScanner::clearAllData() {
  records.clear();
  
  // Deletar todos os arquivos
  for (uint16_t i = 0; i <= fileIndex; i++) {
    LittleFS.remove(getDataFileName(i));
  }
  
  // Reset índice
  fileIndex = 0;
  File indexFile = LittleFS.open("/wifi_index.txt", "w");
  if (indexFile) {
    indexFile.println(0);
    indexFile.close();
  }
  
  Serial.println("🗑️ Todos os dados WiFi removidos");
}