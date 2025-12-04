#include "ble_client.h"
#include "bike_config.h"
#include <ArduinoJson.h>

BikeClient::BikeClient() : pClient(nullptr), connected(false), baseFound(false), registered(false) {}

void BikeClient::init(const String& bikeId) {
  this->bikeId = bikeId;
  
  NimBLEDevice::init(("BPR_Bike_" + bikeId).c_str());
  pClient = NimBLEDevice::createClient();
  pClient->setClientCallbacks(this);
  
  Serial.printf("🔵 BLE inicializado: BPR_Bike_%s\n", bikeId.c_str());
}

bool BikeClient::scanForBase(const String& baseName) {
  this->baseBleName = baseName;
  Serial.printf("🔍 Procurando base: %s\n", baseName.c_str());
  
  NimBLEScan* pScan = NimBLEDevice::getScan();
  pScan->setActiveScan(true);
  pScan->setInterval(100);
  pScan->setWindow(99);
  
  NimBLEScanResults results = pScan->start(5, false);
  
  for (int i = 0; i < results.getCount(); i++) {
    NimBLEAdvertisedDevice device = results.getDevice(i);
    
    if (device.getName() == baseName.c_str()) {
      Serial.printf("✅ Base encontrada: %s RSSI:%d\n", 
                    device.getAddress().toString().c_str(), device.getRSSI());
      
      baseFound = true;
      return true;
    }
  }
  
  Serial.println("❌ Base não encontrada");
  baseFound = false;
  return false;
}

bool BikeClient::connectToBase() {
  if (!baseFound || !pClient) return false;
  
  Serial.println("🔗 Conectando à base...");
  
  // Usar o mesmo padrão do simulator que funcionava
  NimBLEScan* pScan = NimBLEDevice::getScan();
  pScan->setActiveScan(true);
  pScan->setInterval(100);
  pScan->setWindow(99);
  
  NimBLEScanResults results = pScan->start(5, false);
  
  for (int i = 0; i < results.getCount(); i++) {
    NimBLEAdvertisedDevice device = results.getDevice(i);
    
    if (device.getName() == baseBleName.c_str()) {
      Serial.printf("✅ Conectando: %s RSSI:%d\n", 
                    device.getAddress().toString().c_str(), device.getRSSI());
      
      if (pClient->connect(&device)) {
        Serial.println("✅ Conectado à base");
        connected = true;
        
        // Enviar dados imediatamente como no simulator
        sendBikeInfo();
        return true;
      }
    }
  }
  
  Serial.println("❌ Falha na conexão");
  connected = false;
  return false;
}

bool BikeClient::registerWithBase() {
  if (!connected) return false;
  
  DynamicJsonDocument doc(512);
  doc["type"] = "bike_registration";
  doc["bike_id"] = bikeId;
  doc["timestamp"] = millis() / 1000;
  doc["version"] = "2.0";
  
  String registrationJson;
  serializeJson(doc, registrationJson);
  
  auto pService = pClient->getService("BAAD");
  if (!pService) {
    Serial.println("❌ Serviço BAAD não encontrado");
    return false;
  }
  
  auto pChar = pService->getCharacteristic("F00D");
  if (!pChar) {
    Serial.println("❌ Característica F00D não encontrada");
    return false;
  }
  
  bool success = pChar->writeValue(registrationJson.c_str());
  
  if (success) {
    Serial.printf("📝 Registro enviado: %s\n", registrationJson.c_str());
    registered = true;
  } else {
    Serial.println("❌ Falha no registro");
  }
  
  return success;
}

bool BikeClient::sendBikeInfo() {
  if (!connected) return false;
  
  DynamicJsonDocument doc(1024);
  doc["uid"] = bikeId;
  doc["status"] = "active";
  doc["last_ble_contact"] = millis() / 1000;
  doc["last_wifi_scan"] = millis() / 1000;
  
  String bikeJson;
  serializeJson(doc, bikeJson);
  
  auto pService = pClient->getService("BAAD");
  if (!pService) return false;
  
  auto pChar = pService->getCharacteristic("F00D");
  if (!pChar) return false;
  
  bool success = pChar->writeValue(bikeJson.c_str());
  
  if (success) {
    Serial.printf("📝 Info da bike enviada: %s\n", bikeJson.c_str());
  }
  
  return success;
}

bool BikeClient::sendStatus(float batteryVoltage, uint16_t recordsCount) {
  if (!connected) return false;
  
  DynamicJsonDocument doc(512);
  doc["type"] = "status";
  doc["bike_id"] = bikeId;
  doc["battery_voltage"] = batteryVoltage;
  doc["records_count"] = recordsCount;
  doc["timestamp"] = millis() / 1000;
  doc["heap"] = ESP.getFreeHeap();
  
  String statusJson;
  serializeJson(doc, statusJson);
  
  auto pService = pClient->getService("BAAD");
  if (!pService) return false;
  
  auto pChar = pService->getCharacteristic("BEEF");
  if (!pChar) return false;
  
  bool success = pChar->writeValue(statusJson.c_str());
  
  if (success) {
    Serial.printf("📤 Status enviado: Bat:%.2fV Records:%d\n", 
                  batteryVoltage, recordsCount);
  }
  
  return success;
}

bool BikeClient::receiveConfig(String& configJson) {
  if (!connected) return false;
  
  auto pService = pClient->getService("BAAD");
  if (!pService) return false;
  
  auto pChar = pService->getCharacteristic("F00D");
  if (!pChar) return false;
  
  std::string value = pChar->readValue();
  
  if (value.length() > 0) {
    configJson = String(value.c_str());
    Serial.printf("📥 Config recebida: %s\n", configJson.c_str());
    return true;
  }
  
  return false;
}

bool BikeClient::sendWifiData(const std::vector<WifiRecord>& records) {
  if (!connected || records.empty()) return false;
  
  DynamicJsonDocument doc(2048);
  JsonArray networks = doc.createNestedArray("networks");
  
  for (const auto& record : records) {
    JsonObject net = networks.createNestedObject();
    
    char bssidStr[18];
    sprintf(bssidStr, "%02X:%02X:%02X:%02X:%02X:%02X",
            record.bssid[0], record.bssid[1], record.bssid[2],
            record.bssid[3], record.bssid[4], record.bssid[5]);
    
    net["bssid"] = bssidStr;
    net["rssi"] = record.rssi;
    net["channel"] = record.channel;
    net["timestamp"] = record.timestamp;
  }
  
  doc["bike_id"] = bikeId;
  doc["total_records"] = records.size();
  doc["timestamp"] = millis() / 1000;
  
  String wifiJson;
  serializeJson(doc, wifiJson);
  
  auto pService = pClient->getService("BAAD");
  if (!pService) return false;
  
  auto pChar = pService->getCharacteristic("BEEF");
  if (!pChar) return false;
  
  bool success = pChar->writeValue(wifiJson.c_str());
  
  if (success) {
    Serial.printf("📶 Dados WiFi enviados: %d registros\n", records.size());
  }
  
  return success;
}

void BikeClient::disconnect() {
  if (pClient && connected) {
    pClient->disconnect();
    connected = false;
    registered = false;
    Serial.println("🔴 Desconectado da base");
  }
}

bool BikeClient::isConnected() {
  return connected;
}

void BikeClient::onConnect(NimBLEClient* pClient) {
  Serial.println("🔵 Callback: Conectado à Central BPR");
  connected = true;
}

void BikeClient::onDisconnect(NimBLEClient* pClient) {
  Serial.println("🔴 Callback: Desconectado da Central BPR");
  connected = false;
  registered = false;
}