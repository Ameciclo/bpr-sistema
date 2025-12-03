#include "ble_simple.h"
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include "firebase_client.h"

static bool bleReady = false;
static NimBLEServer* pServer = nullptr;
static int connectedClients = 0;
static String deviceName = "BPR Base Station";
static String serviceUUID = "BAAD";
static String bikeIdUUID = "F00D";
static String batteryUUID = "BEEF";

bool loadBLEConfig() {
    if (!SPIFFS.begin(true)) {
        Serial.println("❌ SPIFFS mount failed");
        return false;
    }
    
    File file = SPIFFS.open("/ble_config.json", "r");
    if (!file) {
        Serial.println("⚠️ BLE config not found, using defaults");
        return true;
    }
    
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        Serial.println("❌ BLE config parse error");
        return false;
    }
    
    deviceName = doc["device_name"].as<String>();
    serviceUUID = doc["service_uuid"].as<String>();
    bikeIdUUID = doc["characteristics"]["bike_id"]["uuid"].as<String>();
    batteryUUID = doc["characteristics"]["battery"]["uuid"].as<String>();
    
    Serial.printf("✅ BLE config loaded: %s\n", deviceName.c_str());
    return true;
}

class ServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer) {
        connectedClients++;
        Serial.printf("🔵 ✅ BIKE CONECTADA! Total: %d\n", connectedClients);
    }
    
    void onDisconnect(NimBLEServer* pServer) {
        connectedClients--;
        Serial.printf("🔴 ❌ BIKE DESCONECTADA! Total: %d\n", connectedClients);
        NimBLEDevice::startAdvertising();
    }
};

class CharCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar) {
        std::string uuid = pChar->getUUID().toString();
        std::string value = pChar->getValue();
        
        Serial.printf("📝 ✅ DADOS RECEBIDOS! UUID: %s\n", uuid.c_str());
        
        // Processar JSON recebido
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, value.c_str());
        
        if (error) {
            Serial.printf("❌ Erro ao processar JSON: %s\n", error.c_str());
            Serial.printf("Dados brutos: %s\n", value.c_str());
            return;
        }
        
        // Verificar tipo de dados
        if (doc.containsKey("uid")) {
            // Dados da bicicleta
            Serial.println("😲 === DADOS DA BICICLETA ===");
            Serial.printf("🆔 ID: %s\n", doc["uid"].as<String>().c_str());
            Serial.printf("🏠 Base: %s\n", doc["base_id"].as<String>().c_str());
            Serial.printf("🔋 Bateria: %.2fV\n", doc["battery_voltage"].as<float>());
            Serial.printf("🟢 Status: %s\n", doc["status"].as<String>().c_str());
            
            if (doc.containsKey("last_position")) {
                Serial.printf("📍 Posição: %.6f, %.6f (%s)\n", 
                             doc["last_position"]["lat"].as<float>(),
                             doc["last_position"]["lng"].as<float>(),
                             doc["last_position"]["source"].as<String>().c_str());
            }
            
            // Upload para Firebase
            if (isFirebaseReady()) {
                if (uploadBikeData(value.c_str())) {
                    Serial.println("🔥 ✅ Dados enviados para Firebase!");
                } else {
                    Serial.println("🔥 ❌ Falha no upload Firebase");
                }
            } else {
                Serial.println("🔥 ⚠️ Firebase offline - dados em cache");
            }
            
            // Verificar bateria baixa
            float battery = doc["battery_voltage"].as<float>();
            if (battery < 3.45) {
                Serial.println("⚠️ 🔴 ALERTA: BATERIA BAIXA!");
            }
            
        } else if (doc.containsKey("networks")) {
            // Dados de scan WiFi
            Serial.println("📶 === SCAN WIFI ===");
            Serial.printf("😲 Bike: %s\n", doc["bike_id"].as<String>().c_str());
            Serial.printf("⏰ Timestamp: %lu\n", doc["timestamp"].as<unsigned long>());
            
            JsonArray networks = doc["networks"];
            Serial.printf("📶 Redes encontradas: %d\n", networks.size());
            
            for (JsonObject network : networks) {
                Serial.printf("  • %s | %s | RSSI: %d\n",
                             network["ssid"].as<String>().c_str(),
                             network["bssid"].as<String>().c_str(),
                             network["rssi"].as<int>());
            }
            
            // Upload para Firebase
            if (isFirebaseReady()) {
                if (uploadWifiScan(value.c_str())) {
                    Serial.println("🔥 ✅ WiFi scan enviado para Firebase!");
                } else {
                    Serial.println("🔥 ❌ Falha no upload WiFi scan");
                }
            } else {
                Serial.println("🔥 ⚠️ Firebase offline - scan em cache");
            }
            
        } else if (doc.containsKey("type") && doc["type"] == "battery_alert") {
            // Alerta de bateria
            Serial.println("⚠️ === ALERTA DE BATERIA ===");
            Serial.printf("😲 Bike: %s\n", doc["bike_id"].as<String>().c_str());
            Serial.printf("🔋 Voltagem: %.2fV\n", doc["battery_voltage"].as<float>());
            Serial.printf("🔴 Crítico: %s\n", doc["critical"].as<bool>() ? "SIM" : "NÃO");
            
            // Upload alerta para Firebase
            if (isFirebaseReady()) {
                if (uploadBatteryAlert(value.c_str())) {
                    Serial.println("🔥 ✅ Alerta enviado para Firebase!");
                } else {
                    Serial.println("🔥 ❌ Falha no upload alerta");
                }
            } else {
                Serial.println("🔥 ⚠️ Firebase offline - alerta em cache");
            }
            
            if (doc["critical"].as<bool>()) {
                Serial.println("😨 🔴 BATERIA CRÍTICA - AÇÃO NECESSÁRIA!");
            }
        }
        
        Serial.println("================================\n");
    }
    
    void onRead(NimBLECharacteristic* pChar) {
        Serial.printf("📄 Characteristic lida: %s\n", pChar->getUUID().toString().c_str());
    }
};

bool initBLESimple() {
    if (bleReady) return true;
    
    Serial.println("🔵 Initializing BLE (simple)...");
    
    try {
        // Minimal NimBLE init
        NimBLEDevice::init("BPR Base Station");
        
        // Set low power
        NimBLEDevice::setPower(ESP_PWR_LVL_P3); // 3dBm
        
        bleReady = true;
        Serial.println("✅ BLE Simple initialized");
        return true;
        
    } catch (...) {
        Serial.println("❌ BLE Simple init failed");
        return false;
    }
}

void bleScanOnce() {
    if (!bleReady) {
        Serial.println("⚠️ BLE not ready");
        return;
    }
    
    Serial.println("🔍 Starting simple BLE scan...");
    
    try {
        NimBLEScan* pScan = NimBLEDevice::getScan();
        pScan->setActiveScan(false); // Passive scan
        pScan->setInterval(1349);
        pScan->setWindow(449);
        
        NimBLEScanResults results = pScan->start(3, false); // 3 second scan
        
        Serial.printf("📊 Found %d devices\n", results.getCount());
        
        for (int i = 0; i < results.getCount() && i < 5; i++) { // Max 5 devices
            NimBLEAdvertisedDevice device = results.getDevice(i);
            std::string name = device.getName();
            if (name.empty()) name = "[No Name]";
            Serial.printf("  %d: %s (RSSI: %d)\n", 
                         i, 
                         name.c_str(), 
                         device.getRSSI());
        }
        
    } catch (...) {
        Serial.println("❌ BLE scan failed");
    }
}

bool startBLEServer() {
    if (!bleReady) return false;
    
    Serial.println("📡 Starting BLE Server...");
    
    try {
        pServer = NimBLEDevice::createServer();
        pServer->setCallbacks(new ServerCallbacks());
        
        // Create service with config UUID
        NimBLEService* pService = pServer->createService(serviceUUID.c_str());
        
        // Create characteristics
        NimBLECharacteristic* pBikeIdChar = pService->createCharacteristic(
            bikeIdUUID.c_str(),
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
        );
        
        NimBLECharacteristic* pBatteryChar = pService->createCharacteristic(
            batteryUUID.c_str(),
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
        );
        
        pBikeIdChar->setValue("central_ready");
        pBikeIdChar->setCallbacks(new CharCallbacks());
        
        pBatteryChar->setValue("4.2");
        pBatteryChar->setCallbacks(new CharCallbacks());
        pService->start();
        
        // Start advertising
        NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
        pAdvertising->addServiceUUID(pService->getUUID());
        pAdvertising->setScanResponse(true);
        pAdvertising->start();
        
        Serial.println("✅ BLE Server started");
        return true;
        
    } catch (...) {
        Serial.println("❌ BLE Server failed");
        return false;
    }
}

int getConnectedClients() {
    return connectedClients;
}

bool isBLEReady() {
    return bleReady;
}