#include "ble_client.h"
#include "bike_config.h"
#include <ArduinoJson.h>

BLEClient::BLEClient() : pClient(nullptr), connected(false), baseFound(false) {}

void BLEClient::init(const char* bikeId) {
  strncpy(this->bikeId, bikeId, sizeof(this->bikeId) - 1);
  
  NimBLEDevice::init(String("BPR_Bike_") + bikeId);
  NimBLEDevice::setPower(ESP_PWR_LVL_P3); // Potência mínima
  
  pClient = NimBLEDevice::createClient();
  pClient->setClientCallbacks(this);
  pClient->setConnectionParams(12, 12, 0, 51); // Intervalo otimizado
  
  Serial.printf("🔵 BLE inicializado: BPR_Bike_%s\n", bikeId);
}

bool BLEClient::scanForBase(const char* baseName) {
  Serial.printf("🔍 Procurando base: %s\n", baseName);
  
  NimBLEScan* pScan = NimBLEDevice::getScan();
  pScan->setActiveScan(false); // Passive scan para economia
  pScan->setInterval(100);
  pScan->setWindow(50);
  
  NimBLEScanResults results = pScan->start(5, false);
  
  for (int i = 0; i < results.getCount(); i++) {
    NimBLEAdvertisedDevice device = results.getDevice(i);
    
    if (device.getName() == baseName) {
      Serial.printf("✅ Base encontrada: %s RSSI:%d\n", 
                    device.getAddress().toString().c_str(), device.getRSSI());
      
      baseAddress = device.getAddress();
      baseFound = true;
      return true;
    }
  }
  
  Serial.println("❌ Base não encontrada");
  baseFound = false;
  return false;
}

bool BLEClient::connectToBase() {
  if (!baseFound || !pClient) return false;
  
  Serial.println("🔗 Conectando à base...");
  
  if (pClient->connect(baseAddress)) {
    Serial.println("✅ Conectado à base");
    connected = true;
    return true;
  }
  
  Serial.println("❌ Falha na conexão");
  connected = false;
  return false;
}

bool BLEClient::sendStatus(const BikeStatus& status) {
  if (!connected) return false;
  
  auto pService = pClient->getService(BLE_SERVICE_UUID);
  if (!pService) {
    Serial.println("❌ Serviço não encontrado");
    return false;
  }
  
  auto pChar = pService->getCharacteristic(BLE_STATUS_CHAR_UUID);
  if (!pChar) {
    Serial.println("❌ Característica STATUS não encontrada");
    return false;
  }
  
  bool success = pChar->writeValue((uint8_t*)&status, sizeof(status));
  
  if (success) {
    Serial.printf("📤 Status enviado: Bat:%.2fV Records:%d\n", 
                  status.battery_voltage, status.records_count);
  } else {
    Serial.println("❌ Falha ao enviar status");
  }
  
  return success;
}

bool BLEClient::receiveConfig(BikeConfig& config) {
  if (!connected) return false;
  
  auto pService = pClient->getService(BLE_SERVICE_UUID);
  if (!pService) return false;
  
  auto pChar = pService->getCharacteristic(BLE_CONFIG_CHAR_UUID);
  if (!pChar) return false;
  
  std::string value = pChar->readValue();
  
  if (value.length() == sizeof(BikeConfig)) {
    memcpy(&config, value.data(), sizeof(BikeConfig));
    Serial.printf("📥 Config recebida: Scan:%ds Sleep:%ds\n", 
                  config.scan_interval_sec, config.deep_sleep_sec);
    return true;
  }
  
  Serial.println("❌ Config inválida");
  return false;
}

bool BLEClient::sendWifiData(const std::vector<WifiRecord>& records) {
  if (!connected || records.empty()) return false;
  
  auto pService = pClient->getService(BLE_SERVICE_UUID);
  if (!pService) return false;
  
  auto pChar = pService->getCharacteristic(BLE_DATA_CHAR_UUID);
  if (!pChar) return false;
  
  // Envia em lotes de 10 registros
  const size_t batchSize = 10;
  size_t totalSent = 0;
  
  for (size_t i = 0; i < records.size(); i += batchSize) {
    size_t endIdx = min(i + batchSize, records.size());
    size_t batchCount = endIdx - i;
    size_t dataSize = batchCount * sizeof(WifiRecord);
    
    bool success = pChar->writeValue((uint8_t*)&records[i], dataSize);
    
    if (success) {
      totalSent += batchCount;
      Serial.printf("📤 Lote %d/%d enviado (%d registros)\n", 
                    (int)(i/batchSize + 1), (int)((records.size() + batchSize - 1)/batchSize), 
                    (int)batchCount);
      delay(100); // Pequena pausa entre lotes
    } else {
      Serial.printf("❌ Falha no lote %d\n", (int)(i/batchSize + 1));
      return false;
    }
  }
  
  Serial.printf("✅ %d registros WiFi enviados\n", (int)totalSent);
  return true;
}

void BLEClient::disconnect() {
  if (pClient && connected) {
    pClient->disconnect();
    connected = false;
    Serial.println("🔴 Desconectado da base");
  }
}

bool BLEClient::isConnected() {
  return connected;
}

// Callbacks
void BLEClient::onConnect(NimBLEClient* pClient) {
  Serial.println("🔵 Callback: Conectado");
  connected = true;
}

void BLEClient::onDisconnect(NimBLEClient* pClient) {
  Serial.println("🔴 Callback: Desconectado");
  connected = false;
}