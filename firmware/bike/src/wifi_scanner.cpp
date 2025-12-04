#include "wifi_scanner.h"
#include "bike_config.h"
#include <WiFi.h>

WiFiScanner::WiFiScanner() : lastScanTime(0), scanCount(0) {}

void WiFiScanner::init() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
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
    
    Serial.printf("  %02X:%02X:%02X:%02X:%02X:%02X RSSI:%d CH:%d\n",
                  bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
                  record.rssi, record.channel);
  }
  
  WiFi.scanDelete();
  lastScanTime = currentTime;
  scanCount++;
  
  Serial.printf("📦 Total de registros: %d\n", records.size());
  return true;
}

std::vector<WifiRecord>& WiFiScanner::getRecords() {
  return records;
}

void WiFiScanner::clearRecords() {
  records.clear();
  Serial.println("🗑️ Buffer WiFi limpo");
}

bool WiFiScanner::hasRecords() {
  return !records.empty();
}

size_t WiFiScanner::getRecordCount() {
  return records.size();
}