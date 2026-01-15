#include "at_base.h"
#include <ArduinoJson.h>
#include "power_manager.h"

extern PowerManager powerManager;

AtBaseState::AtBaseState(ConfigManager& configMgr, BufferManager& bufferMgr) 
    : configManager(configMgr), bufferManager(bufferMgr), 
      pClient(nullptr), pDataChar(nullptr), pConfigChar(nullptr), 
      bleConnected(false), lastStatusSent(0) {}

bool AtBaseState::scanForBase() {
    BikeConfig& config = configManager.getConfig();
    Serial.printf("🔍 Scanning for BLE base '%s*' (timeout: %ds)...\n", 
                  config.ble.base_name, config.ble.scan_time_sec);
    
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setActiveScan(true);
    NimBLEScanResults results = pScan->start(config.ble.scan_time_sec, false);
    
    for (int i = 0; i < results.getCount(); i++) {
        NimBLEAdvertisedDevice device = results.getDevice(i);
        
        if (device.getName().find(config.ble.base_name) != std::string::npos) {
            Serial.printf("🔍 Found base: %s\n", device.getName().c_str());
            
            if (connectToBase(&device)) {
                pScan->clearResults();
                return true;
            }
        }
    }
    
    pScan->clearResults();
    Serial.println("❌ No BLE base found");
    return false;
}

bool AtBaseState::connectToBase(NimBLEAdvertisedDevice* device) {
    BikeConfig& config = configManager.getConfig();
    pClient = NimBLEDevice::createClient();
    
    Serial.printf("🔗 Attempting BLE connection (timeout: %dms)...\n", config.ble.connection_timeout_ms);
    if (pClient->connect(device)) {
        Serial.println("✅ BLE connection established");
        
        // Get service and characteristics
        NimBLERemoteService* pService = pClient->getService(BLE_SERVICE_UUID);
        if (!pService) {
            Serial.println("❌ Service not found");
            pClient->disconnect();
            return false;
        }
        
        pDataChar = pService->getCharacteristic(BLE_CHAR_DATA_UUID);
        pConfigChar = pService->getCharacteristic(BLE_CHAR_CONFIG_UUID);
        
        if (!pDataChar || !pConfigChar) {
            Serial.println("❌ Required characteristics not found");
            pClient->disconnect();
            return false;
        }
        
        bleConnected = true;
        return true;
    }
    
    Serial.println("❌ BLE connection failed");
    return false;
}

void AtBaseState::sendStatus() {
    if (!pClient || !bleConnected || !pDataChar) return;
    
    BikeConfig& config = configManager.getConfig();
    String json = powerManager.getBatteryReport(config);
    
    pDataChar->writeValue(json.c_str());
    Serial.printf("📤 Heartbeat: %s\n", json.c_str());
}

void AtBaseState::sendWiFiData() {
    if (!pClient || !bleConnected || !pDataChar || bufferManager.isEmpty()) return;
    
    String json = bufferManager.toJson();
    pDataChar->writeValue(json.c_str());
    Serial.printf("📡 WiFi data sent\n");
}

bool AtBaseState::requestConfig() {
    if (!pClient || !bleConnected || !pConfigChar) {
        Serial.println("❌ No BLE connection to request config");
        return false;
    }
    
    BikeConfig& config = configManager.getConfig();
    Serial.println("📡 Requesting configuration from base...");
    
    DynamicJsonDocument request(256);
    request["type"] = "config_request";
    request["bike_id"] = config.bike_id;
    
    String requestStr;
    serializeJson(request, requestStr);
    
    pConfigChar->writeValue(requestStr.c_str());
    return true;
}

BikeState AtBaseState::update() {
    if (!bleConnected) {
        return STATE_SCANNING;
    }
    
    Serial.println("🏠 AT_BASE - Syncing data");
    
    // Send status periodically
    if (millis() - lastStatusSent > 5000) {
        sendStatus();
        lastStatusSent = millis();
    }
    
    // Send WiFi data if available
    if (!bufferManager.isEmpty()) {
        sendWiFiData();
        bufferManager.clear();
    }
    
    // Check connection
    if (!pClient || !pClient->isConnected()) {
        onBLEDisconnected();
        return STATE_SCANNING;
    }
    
    return STATE_AT_BASE;
}

void AtBaseState::onBLEDisconnected() {
    Serial.println("🔴 BLE disconnected");
    bleConnected = false;
    pDataChar = nullptr;
    pConfigChar = nullptr;
    if (pClient) {
        pClient = nullptr;
    }
}

float AtBaseState::getBatteryVoltage() {
    static unsigned long lastRead = 0;
    static float lastVoltage = 4.0;
    
    if (millis() - lastRead < 5000) {
        return lastVoltage;
    }
    
    int adc = analogRead(BATTERY_PIN);
    lastRead = millis();
    
    if (adc < 1500) {
        if (lastVoltage != 4.0) {
            Serial.printf("⚠️ ADC=%d - USB power (4.0V)\n", adc);
        }
        lastVoltage = 4.0;
        return 4.0;
    }
    
    lastVoltage = (adc / 4095.0) * 3.3 * 2.0;
    return lastVoltage;
}

uint8_t AtBaseState::getBatteryPercent() {
    float voltage = getBatteryVoltage();
    
    // USB power detection
    if (voltage >= 3.9) {
        return 100;
    }
    
    // Convert voltage to percentage (3.2V = 0%, 4.2V = 100%)
    float percent = ((voltage - 3.2) / (4.2 - 3.2)) * 100.0;
    
    // Clamp to 0-100%
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    
    return (uint8_t)percent;
}

// ADICIONAR: Função para extrair tempo do status BUSY
int AtBaseState::extractWaitTime(const String& status) {
    // "BUSY:240" → 240
    int colonPos = status.indexOf(':');
    if (colonPos > 0 && colonPos < status.length() - 1) {
        return status.substring(colonPos + 1).toInt();
    }
    return 60; // Default 1min se não conseguir parsear
}

// ADICIONAR: processHeartbeatResponse()
void AtBaseState::processHeartbeatResponse(String response) {
    if (response.indexOf("config_update") >= 0) {
        // Aplicar nova config
        Serial.println("⚙️ Config update received");
        configManager.processUpdate(response);
    }
    if (response.indexOf("next_checkin_sec") >= 0) {
        // Ajustar próximo "dar oi"
        Serial.println("⏰ Next checkin time adjusted");
    }
}

bool AtBaseState::isConnected() const {
    return bleConnected && pClient && pClient->isConnected();
}

void ClientCallbacks::onDisconnect(NimBLEClient* pClient) {
    if (atBaseState) {
        atBaseState->onBLEDisconnected();
    }
}