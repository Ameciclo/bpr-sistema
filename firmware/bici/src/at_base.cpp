#include "at_base.h"
#include <ArduinoJson.h>

AtBaseState::AtBaseState(ConfigManager& configMgr, BufferManager& bufferMgr) 
    : configManager(configMgr), bufferManager(bufferMgr), 
      pClient(nullptr), pDataChar(nullptr), pConfigChar(nullptr), 
      bleConnected(false), lastStatusSent(0) {}

bool AtBaseState::scanForBase() {
    Config& config = configManager.getConfig();
    Serial.printf("🔍 Scanning for BLE base '%s*' (timeout: %ds)...\n", 
                  config.base_ble_name, config.ble_scan_time_sec);
    
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setActiveScan(true);
    NimBLEScanResults results = pScan->start(config.ble_scan_time_sec, false);
    
    for (int i = 0; i < results.getCount(); i++) {
        NimBLEAdvertisedDevice device = results.getDevice(i);
        if (device.getName().find(config.base_ble_name) != std::string::npos) {
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
    Config& config = configManager.getConfig();
    pClient = NimBLEDevice::createClient();
    
    Serial.printf("🔗 Attempting BLE connection (timeout: %dms)...\n", config.ble_connection_timeout_ms);
    if (pClient->connect(device)) {
        Serial.println("✅ BLE connection established");
        
        // Get service and characteristics with retry
        Serial.printf("🔍 Looking for service: %s\n", BLE_SERVICE_UUID);
        NimBLERemoteService* pService = pClient->getService(BLE_SERVICE_UUID);
        if (!pService) {
            Serial.println("❌ Service not found");
            pClient->disconnect();
            return false;
        }
        
        // Try to get characteristics with retries
        for (int retry = 0; retry < 3; retry++) {
            pDataChar = pService->getCharacteristic(BLE_CHAR_DATA_UUID);
            pConfigChar = pService->getCharacteristic(BLE_CHAR_CONFIG_UUID);
            
            if (pDataChar && pConfigChar) {
                break; // Success
            }
            
            Serial.printf("⏳ Characteristics not ready, retry %d/3...\n", retry + 1);
            delay(2000);
        }
        
        if (!pDataChar || !pConfigChar) {
            Serial.println("❌ Required characteristics not found after retries");
            pClient->disconnect();
            return false;
        }
        
        bleConnected = true;
        
        // Setup config notifications
        if (pConfigChar->canNotify()) {
            pConfigChar->subscribe(true, [this](NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
                String config = String((char*)pData, length);
                Serial.printf("📥 Config received: %s\n", config.c_str());
                configManager.processUpdate(config);
            });
        }
        
        // Setup disconnect callback
        pClient->setClientCallbacks(new ClientCallbacks(this));
        
        return true;
    }
    
    Serial.println("❌ BLE connection failed");
    return false;
}

void AtBaseState::sendStatus() {
    if (!pClient || !bleConnected || !pDataChar) return;
    
    Config& config = configManager.getConfig();
    DynamicJsonDocument doc(256);
    doc["bike_id"] = config.bike_id;
    doc["battery"] = getBatteryVoltage();
    doc["records"] = bufferManager.getCount();
    doc["timestamp"] = millis() / 1000;
    doc["heap"] = ESP.getFreeHeap();
    
    String json;
    serializeJson(doc, json);
    
    pDataChar->writeValue(json.c_str());
    Serial.printf("📤 Status: %s\n", json.c_str());
}

void AtBaseState::sendWiFiData() {
    if (!pClient || !bleConnected || !pDataChar || bufferManager.isEmpty()) return;
    
    Config& config = configManager.getConfig();
    DynamicJsonDocument doc(2048);
    doc["bike_id"] = config.bike_id;
    doc["battery"] = getBatteryVoltage();
    doc["records"] = bufferManager.getCount();
    doc["timestamp"] = millis() / 1000;
    
    JsonArray scans = doc.createNestedArray("wifi_scans");
    
    const WiFiRecord* records = bufferManager.getRecords();
    for (int i = 0; i < bufferManager.getCount(); i++) {
        JsonObject scan = scans.createNestedObject();
        scan["ssid"] = records[i].ssid;
        scan["bssid"] = bssidToString(records[i].bssid);
        scan["rssi"] = records[i].rssi;
        scan["channel"] = records[i].channel;
    }
    
    String json;
    serializeJson(doc, json);
    
    pDataChar->writeValue(json.c_str());
    Serial.printf("📡 WiFi data: %d records sent\n", bufferManager.getCount());
}

bool AtBaseState::requestConfig() {
    if (!pClient || !bleConnected || !pConfigChar) {
        Serial.println("❌ No BLE connection to request config");
        return false;
    }
    
    Config& config = configManager.getConfig();
    Serial.println("📡 Requesting configuration from base...");
    
    DynamicJsonDocument request(256);
    request["type"] = MSG_TYPE_CONFIG_REQUEST;
    request["bike_id"] = config.bike_id;
    
    String requestStr;
    serializeJson(request, requestStr);
    
    Serial.printf("📤 Config request: %s\n", requestStr.c_str());
    pConfigChar->writeValue(requestStr.c_str());
    
    delay(3000);
    Serial.println("⏳ Config request sent, waiting for notification...");
    
    return true;
}

BikeState AtBaseState::update() {
    if (!bleConnected) {
        return STATE_SCANNING;
    }
    
    Serial.println("🏠 AT_BASE - Syncing data");
    
    // Send status periodically
    if (millis() - lastStatusSent > configManager.getConfig().status_report_interval_ms) {
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

bool AtBaseState::isConnected() const {
    return bleConnected && pClient && pClient->isConnected();
}

void ClientCallbacks::onDisconnect(NimBLEClient* pClient) {
    if (atBaseState) {
        atBaseState->onBLEDisconnected();
    }
}