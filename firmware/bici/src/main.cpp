#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "constants.h"

// Hardware pins
#define LED_PIN 8
#define BUTTON_PIN 9
#define BATTERY_PIN A0

// Forward declarations
void onBLEDisconnected();

// BLE Client Callbacks class
class ClientCallbacks : public NimBLEClientCallbacks {
public:
    void onDisconnect(NimBLEClient* pClient) {
        onBLEDisconnected();
    }
};

// States
enum State { BOOT, CONFIG_REQUEST, SCANNING, AT_BASE, SLEEP };
State currentState = BOOT;

// BLE
NimBLEClient* pClient = nullptr;
NimBLERemoteCharacteristic* pDataChar = nullptr;
NimBLERemoteCharacteristic* pConfigChar = nullptr;
bool bleConnected = false;

// WiFi buffer
struct WiFiRecord {
    uint32_t timestamp;
    char ssid[33];
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
};
WiFiRecord wifiBuffer[50];
int bufferCount = 0;

// Config
struct Config {
    // Basic
    char bike_id[32] = "";        // ID único gerado (bpr-1xaos912)
    char bike_name[32] = "";      // Nome dado pela base
    char base_ble_name[32] = CENTRAL_BLE_NAME;
    int version = 1;
    bool dev_mode = true;
    
    // WiFi
    int scan_interval_sec = 300;
    int scan_interval_low_batt_sec = 900;
    int wifi_scan_timeout_ms = 5000;
    int wifi_max_networks = 20;
    int wifi_rssi_threshold = -90;
    
    // BLE
    int ble_scan_time_sec = 5;
    int ble_connection_timeout_ms = 10000;
    
    // Power
    int radio_coordination_delay_ms = 300;
    int light_sleep_duration_ms = 1000;
    int deep_sleep_sec = 3600;
    int max_time_without_base_sec = 7200;
    
    // Battery
    float battery_critical_voltage = 3.2;
    float min_battery_voltage = 3.45;
    float battery_full_voltage = 4.2;
    
    // Timing
    int status_report_interval_ms = 30000;
    int emergency_button_hold_ms = 3000;
    
    // Buffers
    int max_wifi_records = 100;
} config;

// Function declarations
void handleBoot();
void handleScanning();
void handleAtBase();
void handleSleep();
bool scanForBase();
bool connectToBase(NimBLEAdvertisedDevice* device);
void sendStatus();
void sendWiFiData();
float getBatteryVoltage();
void saveBuffer();
void loadBuffer();
bool loadConfig();
void saveConfig();
String getChipID();
void generateUniqueID();
void handleConfigRequest();
bool requestConfigFromBase();
void processConfigUpdate(const String& configJson);
void onBLEDisconnected();

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    
    if (!LittleFS.begin(true)) {
        Serial.println("🔧 LittleFS corrompido - formatando...");
        LittleFS.format();
        LittleFS.begin(true);
    }
    WiFi.mode(WIFI_STA);
    
    Serial.println("🚲 BPR Bici Simple v1.0");
    
    // Gerar ID único se não existir
    generateUniqueID();
    
    // Inicializar BLE com o bike_id gerado
    NimBLEDevice::init(config.bike_id);
    
    // Load config first
    if (!loadConfig()) {
        Serial.println("⚙️ No config found - entering CONFIG_REQUEST");
        currentState = CONFIG_REQUEST;
    } else {
        // Load saved buffer if exists
        loadBuffer();
        currentState = BOOT;
    }
}

void loop() {
    switch (currentState) {
        case BOOT:
            handleBoot();
            break;
        case CONFIG_REQUEST:
            handleConfigRequest();
            break;
        case SCANNING:
            handleScanning();
            break;
        case AT_BASE:
            handleAtBase();
            break;
        case SLEEP:
            handleSleep();
            break;
    }
    
    delay(100);
}

void handleBoot() {
    Serial.println("🔄 BOOT");
    
    // Show current config
    Serial.println("📋 Current Configuration:");
    Serial.printf("   🆔 ID: %s (v%d)\n", config.bike_id, config.version);
    Serial.printf("   📡 WiFi Scan: %ds interval, %dms timeout\n", config.scan_interval_sec, config.wifi_scan_timeout_ms);
    Serial.printf("   🔵 BLE: %ds scan, base='%s'\n", config.ble_scan_time_sec, config.base_ble_name);
    Serial.printf("   🔋 Battery: %.2fV critical, %.2fV low\n", config.battery_critical_voltage, config.min_battery_voltage);
    Serial.printf("   🛠️ Dev Mode: %s\n", config.dev_mode ? "ON" : "OFF");
    
    // Check battery with debug
    int rawADC = analogRead(BATTERY_PIN);
    float voltage = getBatteryVoltage();
    
    Serial.printf("🔋 Battery: ADC=%d, V=%.2fV (min=%.2fV)\n", 
                  rawADC, voltage, 3.2f);
    
    if (voltage < config.battery_critical_voltage && !config.dev_mode) {
        Serial.println("🔋 Bateria crítica - sleep");
        Serial.println("💡 Tip: Check battery connection or use USB power for testing");
        currentState = SLEEP;
        return;
    } else if (voltage < config.battery_critical_voltage && config.dev_mode) {
        Serial.println("🛠️ DEV MODE: Ignoring low battery");
    }
    
    // Try to find base
    if (scanForBase()) {
        currentState = AT_BASE;
    } else {
        currentState = SCANNING;
    }
}

void handleScanning() {
    static unsigned long lastScan = 0;
    
    if (millis() - lastScan > config.scan_interval_sec * 1000) {
        Serial.printf("📡 Starting WiFi scan (timeout: %dms, max: %d networks)...\n", 
                      config.wifi_scan_timeout_ms, config.wifi_max_networks);
        
        // WiFi scan
        int networks = WiFi.scanNetworks();
        int savedCount = 0;
        for (int i = 0; i < networks && bufferCount < config.max_wifi_records && savedCount < config.wifi_max_networks; i++) {
            if (WiFi.RSSI(i) > config.wifi_rssi_threshold) {
                WiFiRecord record;
                record.timestamp = millis() / 1000;
                
                // Copy SSID (truncate if too long)
                String ssid = WiFi.SSID(i);
                strncpy(record.ssid, ssid.c_str(), sizeof(record.ssid) - 1);
                record.ssid[sizeof(record.ssid) - 1] = '\0';
                
                // Copy BSSID
                memcpy(record.bssid, WiFi.BSSID(i), 6);
                record.rssi = WiFi.RSSI(i);
                record.channel = WiFi.channel(i);
                
                wifiBuffer[bufferCount++] = record;
                savedCount++;
            }
        }
        WiFi.scanDelete();
        
        Serial.printf("📶 Found %d networks, saved %d (buffer: %d/%d)\n", 
                      networks, savedCount, bufferCount, config.max_wifi_records);
        
        // Radio coordination delay
        Serial.printf("⏱️ Radio coordination delay: %dms\n", config.radio_coordination_delay_ms);
        delay(config.radio_coordination_delay_ms);
        if (scanForBase()) {
            currentState = AT_BASE;
            return;
        }
        
        lastScan = millis();
    }
    
    // Check battery
    if (getBatteryVoltage() < config.min_battery_voltage && !config.dev_mode) {
        currentState = SLEEP;
    }
}

void handleAtBase() {
    if (!bleConnected) {
        currentState = SCANNING;
        return;
    }
    
    Serial.println("🏠 AT_BASE - Syncing data");
    
    // Send status
    sendStatus();
    
    // Send WiFi data if available
    if (bufferCount > 0) {
        sendWiFiData();
        bufferCount = 0; // Clear buffer
    }
    
    delay(5000);
    
    // Check connection
    if (!pClient || !pClient->isConnected()) {
        onBLEDisconnected();
        currentState = SCANNING;
    }
}

void handleSleep() {
    Serial.println("💤 Deep sleep for 1 hour");
    
    // Save buffer
    saveBuffer();
    
    // Deep sleep
    esp_sleep_enable_timer_wakeup(3600 * 1000000ULL); // 1 hour
    esp_deep_sleep_start();
}

bool scanForBase() {
    Serial.printf("🔍 Scanning for BLE base '%s*' (timeout: %ds)...\n", 
                  config.base_ble_name, config.ble_scan_time_sec);
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setActiveScan(true);
    NimBLEScanResults results = pScan->start(config.ble_scan_time_sec, false);
    
    for (int i = 0; i < results.getCount(); i++) {
        NimBLEAdvertisedDevice device = results.getDevice(i); // Remove pointer
        if (device.getName().find(config.base_ble_name) != std::string::npos) {
            Serial.printf("🔍 Found base: %s\n", device.getName().c_str());
            
            if (connectToBase(&device)) { // Pass address
                pScan->clearResults();
                return true;
            }
        }
    }
    
    pScan->clearResults();
    Serial.println("❌ No BLE base found");
    return false;
}

bool connectToBase(NimBLEAdvertisedDevice* device) {
    pClient = NimBLEDevice::createClient();
    
    Serial.printf("🔗 Attempting BLE connection (timeout: %dms)...\n", config.ble_connection_timeout_ms);
    if (pClient->connect(device)) {
        Serial.println("✅ BLE connection established");
        
        // Get service and characteristics with retry
        Serial.printf("🔍 Looking for service: %s\n", BLE_SERVICE_UUID);
        NimBLERemoteService* pService = pClient->getService(BLE_SERVICE_UUID);
        if (!pService) {
            Serial.println("❌ Service not found - listing available services:");
            std::vector<NimBLERemoteService*>* services = pClient->getServices(true);
            for (auto service : *services) {
                Serial.printf("   Available: %s\n", service->getUUID().toString().c_str());
            }
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
            if (retry == 2) {
                Serial.println("🔍 Listing available characteristics:");
                std::vector<NimBLERemoteCharacteristic*>* chars = pService->getCharacteristics(true);
                for (auto ch : *chars) {
                    Serial.printf("   Available: %s\n", ch->getUUID().toString().c_str());
                }
                Serial.printf("   Looking for Data: %s\n", BLE_CHAR_DATA_UUID);
                Serial.printf("   Looking for Config: %s\n", BLE_CHAR_CONFIG_UUID);
            }
            delay(2000);
        }
        
        if (!pDataChar) {
            Serial.println("❌ Data characteristic not found after retries");
            pClient->disconnect();
            return false;
        }
        
        if (!pConfigChar) {
            Serial.println("❌ Config characteristic not found after retries");
            pClient->disconnect();
            return false;
        }
        
        bleConnected = true;
        
        // Setup config notifications
        if (pConfigChar->canNotify()) {
            pConfigChar->subscribe(true, [](NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
                String config = String((char*)pData, length);
                Serial.printf("📥 Config received: %s\n", config.c_str());
                processConfigUpdate(config);
            });
        }
        
        // Setup disconnect callback
        pClient->setClientCallbacks(new ClientCallbacks());
        
        // Always request config on first connection
        if (currentState == CONFIG_REQUEST) {
            Serial.println("🔄 First connection - requesting config...");
            if (requestConfigFromBase()) {
                Serial.println("✅ Configuration received!");
            } else {
                Serial.println("⚠️ Config request failed, using defaults");
            }
        }
        
        return true;
    }
    
    Serial.println("❌ BLE connection failed");
    return false;
}

void sendStatus() {
    if (!pClient || !bleConnected || !pDataChar) return;
    
    DynamicJsonDocument doc(256);
    doc["bike_id"] = config.bike_id;
    doc["battery"] = getBatteryVoltage();
    doc["records"] = bufferCount;
    doc["timestamp"] = millis() / 1000;
    doc["heap"] = ESP.getFreeHeap();
    
    String json;
    serializeJson(doc, json);
    
    pDataChar->writeValue(json.c_str());
    Serial.printf("📤 Status: %s\n", json.c_str());
}

void sendWiFiData() {
    if (!pClient || !bleConnected || !pDataChar || bufferCount == 0) return;
    
    DynamicJsonDocument doc(2048);
    doc["bike_id"] = config.bike_id;
    doc["battery"] = getBatteryVoltage();
    doc["records"] = bufferCount;
    doc["timestamp"] = millis() / 1000;
    
    JsonArray scans = doc.createNestedArray("wifi_scans");
    
    for (int i = 0; i < bufferCount; i++) {
        JsonObject scan = scans.createNestedObject();
        scan["ssid"] = wifiBuffer[i].ssid;
        
        char bssid[18];
        sprintf(bssid, "%02X:%02X:%02X:%02X:%02X:%02X",
                wifiBuffer[i].bssid[0], wifiBuffer[i].bssid[1], wifiBuffer[i].bssid[2],
                wifiBuffer[i].bssid[3], wifiBuffer[i].bssid[4], wifiBuffer[i].bssid[5]);
        scan["bssid"] = bssid;
        scan["rssi"] = wifiBuffer[i].rssi;
        scan["channel"] = wifiBuffer[i].channel;
    }
    
    String json;
    serializeJson(doc, json);
    
    pDataChar->writeValue(json.c_str());
    Serial.printf("📡 WiFi data: %d records sent\n", bufferCount);
}

float getBatteryVoltage() {
    static unsigned long lastRead = 0;
    static float lastVoltage = 4.0;
    
    // Só lê a cada 5 segundos para evitar spam
    if (millis() - lastRead < 5000) {
        return lastVoltage;
    }
    
    int adc = analogRead(BATTERY_PIN);
    lastRead = millis();
    
    // Para desenvolvimento: assume USB se ADC baixo
    if (adc < 1500) {
        if (lastVoltage != 4.0) { // Só printa uma vez
            Serial.printf("⚠️ ADC=%d - USB power (4.0V)\n", adc);
        }
        lastVoltage = 4.0;
        return 4.0;
    }
    
    lastVoltage = (adc / 4095.0) * 3.3 * 2.0;
    return lastVoltage;
}

void saveBuffer() {
    File file = LittleFS.open("/buffer.dat", "w");
    if (file) {
        file.write((uint8_t*)&bufferCount, sizeof(bufferCount));
        file.write((uint8_t*)wifiBuffer, sizeof(WiFiRecord) * bufferCount);
        file.close();
        Serial.println("💾 Buffer saved");
    }
}

void loadBuffer() {
    File file = LittleFS.open("/buffer.dat", "r");
    if (file) {
        file.read((uint8_t*)&bufferCount, sizeof(bufferCount));
        file.read((uint8_t*)wifiBuffer, sizeof(WiFiRecord) * bufferCount);
        file.close();
        LittleFS.remove("/buffer.dat");
        Serial.printf("📂 Buffer loaded: %d records\n", bufferCount);
    }
}

bool loadConfig() {
    Serial.println("📂 Loading config from LittleFS...");
    File file = LittleFS.open("/config.json", "r");
    if (!file) {
        Serial.println("❌ Config file not found");
        return false;
    }
    
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        Serial.printf("❌ JSON parse error: %s\n", error.c_str());
        return false;
    }
    
    // Basic
    strcpy(config.bike_id, doc["bike_id"] | "bici_001");
    config.version = doc["version"] | 1;
    config.dev_mode = doc["dev_mode"] | true;
    
    // WiFi
    JsonObject wifi = doc["wifi"];
    config.scan_interval_sec = wifi["scan_interval_sec"] | 300;
    config.scan_interval_low_batt_sec = wifi["scan_interval_low_batt_sec"] | 900;
    config.wifi_scan_timeout_ms = wifi["scan_timeout_ms"] | 5000;
    config.wifi_max_networks = wifi["max_networks"] | 20;
    config.wifi_rssi_threshold = wifi["rssi_threshold"] | -90;
    
    // BLE
    JsonObject ble = doc["ble"];
    strcpy(config.base_ble_name, ble["base_name"] | "BPR");
    config.ble_scan_time_sec = ble["scan_time_sec"] | 5;
    config.ble_connection_timeout_ms = ble["connection_timeout_ms"] | 10000;
    
    // Power
    JsonObject power = doc["power"];
    config.radio_coordination_delay_ms = power["radio_coordination_delay_ms"] | 300;
    config.light_sleep_duration_ms = power["light_sleep_duration_ms"] | 1000;
    config.deep_sleep_sec = power["deep_sleep_duration_sec"] | 3600;
    config.max_time_without_base_sec = power["max_time_without_base_sec"] | 7200;
    
    // Battery
    JsonObject battery = doc["battery"];
    config.battery_critical_voltage = battery["critical_voltage"] | 3.2;
    config.min_battery_voltage = battery["low_voltage"] | 3.45;
    config.battery_full_voltage = battery["full_voltage"] | 4.2;
    
    // Timing
    JsonObject timing = doc["timing"];
    config.status_report_interval_ms = timing["status_report_interval_ms"] | 30000;
    config.emergency_button_hold_ms = timing["emergency_button_hold_ms"] | 3000;
    
    // Buffers
    JsonObject buffers = doc["buffers"];
    config.max_wifi_records = buffers["max_wifi_records"] | 100;
    
    Serial.printf("✅ Config loaded: %s v%d\n", config.bike_id, config.version);
    Serial.printf("📡 WiFi: %ds interval, %dms timeout, %d networks max\n", 
                  config.scan_interval_sec, config.wifi_scan_timeout_ms, config.wifi_max_networks);
    Serial.printf("🔵 BLE: %ds scan, %dms timeout, base='%s'\n", 
                  config.ble_scan_time_sec, config.ble_connection_timeout_ms, config.base_ble_name);
    Serial.printf("🔋 Battery: %.2fV critical, %.2fV low, %.2fV full\n", 
                  config.battery_critical_voltage, config.min_battery_voltage, config.battery_full_voltage);
    Serial.printf("⚡ Power: %dms coord delay, %ds deep sleep\n", 
                  config.radio_coordination_delay_ms, config.deep_sleep_sec);
    
    return true;
}

void saveConfig() {
    Serial.println("💾 Saving config to LittleFS...");
    DynamicJsonDocument doc(2048);
    
    // Basic
    doc["bike_id"] = config.bike_id;
    doc["version"] = config.version;
    doc["dev_mode"] = config.dev_mode;
    
    // WiFi
    JsonObject wifi = doc.createNestedObject("wifi");
    wifi["scan_interval_sec"] = config.scan_interval_sec;
    wifi["scan_interval_low_batt_sec"] = config.scan_interval_low_batt_sec;
    wifi["scan_timeout_ms"] = config.wifi_scan_timeout_ms;
    wifi["max_networks"] = config.wifi_max_networks;
    wifi["rssi_threshold"] = config.wifi_rssi_threshold;
    
    // BLE
    JsonObject ble = doc.createNestedObject("ble");
    ble["base_name"] = config.base_ble_name;
    ble["scan_time_sec"] = config.ble_scan_time_sec;
    ble["connection_timeout_ms"] = config.ble_connection_timeout_ms;
    
    // Power
    JsonObject power = doc.createNestedObject("power");
    power["radio_coordination_delay_ms"] = config.radio_coordination_delay_ms;
    power["light_sleep_duration_ms"] = config.light_sleep_duration_ms;
    power["deep_sleep_duration_sec"] = config.deep_sleep_sec;
    power["max_time_without_base_sec"] = config.max_time_without_base_sec;
    
    // Battery
    JsonObject battery = doc.createNestedObject("battery");
    battery["critical_voltage"] = config.battery_critical_voltage;
    battery["low_voltage"] = config.min_battery_voltage;
    battery["full_voltage"] = config.battery_full_voltage;
    
    // Timing
    JsonObject timing = doc.createNestedObject("timing");
    timing["status_report_interval_ms"] = config.status_report_interval_ms;
    timing["emergency_button_hold_ms"] = config.emergency_button_hold_ms;
    
    // Buffers
    JsonObject buffers = doc.createNestedObject("buffers");
    buffers["max_wifi_records"] = config.max_wifi_records;
    
    doc["timestamp"] = millis() / 1000;
    
    File file = LittleFS.open("/config.json", "w");
    if (file) {
        serializeJson(doc, file);
        file.close();
        Serial.println("✅ Config saved successfully");
    } else {
        Serial.println("❌ Failed to save config");
    }
}

bool requestConfigFromBase() {
    if (!pClient || !bleConnected || !pConfigChar) {
        Serial.println("❌ No BLE connection to request config");
        return false;
    }
    
    Serial.println("📡 Requesting configuration from base...");
    
    // Send config request
    DynamicJsonDocument request(256);
    request["type"] = "config_request";
    request["bike_id"] = config.bike_id;
    
    String requestStr;
    serializeJson(request, requestStr);
    
    Serial.printf("📤 Config request: %s\n", requestStr.c_str());
    pConfigChar->writeValue(requestStr.c_str());
    
    // Wait for response via notification (handled in callback)
    delay(3000);
    
    Serial.println("⏳ Config request sent, waiting for notification...");
    
    // If we're in CONFIG_REQUEST state, we got some response
    if (currentState == CONFIG_REQUEST) {
        return true;
    }
    
    return false;
}

void handleConfigRequest() {
    static unsigned long lastScan = 0;
    
    Serial.println("🔍 CONFIG REQUEST - Searching for BLE base to get configuration...");
    
    // Procura base a cada 5 segundos
    if (millis() - lastScan > 5000) {
        if (scanForBase()) {
            Serial.println("✅ Base found! Requesting configuration...");
            if (requestConfigFromBase()) {
                Serial.println("✅ Configuration received and saved!");
                currentState = BOOT;
                return;
            } else {
                Serial.println("❌ Failed to get configuration from base");
            }
        }
        lastScan = millis();
    }
    
    // Fallback: botão para usar configuração padrão
    if (digitalRead(BUTTON_PIN) == LOW) {
        delay(100);
        if (digitalRead(BUTTON_PIN) == LOW) {
            Serial.println("🔧 Using default configuration (button pressed)");
            saveConfig();
            currentState = BOOT;
        }
    }
    
    delay(1000);
}

void generateUniqueID() {
    if (strlen(config.bike_id) == 0) {
        String chipId = getChipID();
        snprintf(config.bike_id, sizeof(config.bike_id), "bpr-%s", chipId.c_str());
        Serial.printf("🆔 Generated unique ID: %s\n", config.bike_id);
        saveConfig();
    } else {
        Serial.printf("🆔 Using existing ID: %s\n", config.bike_id);
    }
}

String getChipID() {
    uint64_t chipid = ESP.getEfuseMac();
    char chipStr[9];
    snprintf(chipStr, sizeof(chipStr), "%08x", (uint32_t)(chipid & 0xFFFFFFFF));
    return String(chipStr).substring(0, 6); // Primeiros 6 caracteres
}

void processConfigUpdate(const String& configJson) {
    Serial.printf("⚙️ Processing config update: %s\n", configJson.c_str());
    
    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, configJson) != DeserializationError::Ok) {
        Serial.println("❌ Invalid config JSON");
        return;
    }
    
    // Check if this config is for us
    if (doc["target_bike"] && doc["target_bike"] != config.bike_id) {
        Serial.printf("🚫 Config not for us (target: %s, us: %s)\n", 
                      doc["target_bike"].as<String>().c_str(), config.bike_id);
        return;
    }
    
    JsonObject configData = doc["config"];
    if (!configData) {
        Serial.println("❌ No config data found");
        return;
    }
    
    bool configChanged = false;
    
    // Update basic config
    if (configData["bike_name"]) {
        strcpy(config.bike_name, configData["bike_name"]);
        configChanged = true;
    }
    if (configData["version"]) {
        config.version = configData["version"];
        configChanged = true;
    }
    if (configData["dev_mode"]) {
        config.dev_mode = configData["dev_mode"];
        configChanged = true;
    }
    
    // Update WiFi config
    if (configData["wifi"]["scan_interval_sec"]) {
        config.scan_interval_sec = configData["wifi"]["scan_interval_sec"];
        configChanged = true;
    }
    if (configData["wifi"]["scan_timeout_ms"]) {
        config.wifi_scan_timeout_ms = configData["wifi"]["scan_timeout_ms"];
        configChanged = true;
    }
    if (configData["wifi"]["max_networks"]) {
        config.wifi_max_networks = configData["wifi"]["max_networks"];
        configChanged = true;
    }
    if (configData["wifi"]["rssi_threshold"]) {
        config.wifi_rssi_threshold = configData["wifi"]["rssi_threshold"];
        configChanged = true;
    }
    
    // Update BLE config
    if (configData["ble"]["base_name"]) {
        strcpy(config.base_ble_name, configData["ble"]["base_name"]);
        configChanged = true;
    }
    if (configData["ble"]["scan_time_sec"]) {
        config.ble_scan_time_sec = configData["ble"]["scan_time_sec"];
        configChanged = true;
    }
    
    // Update power config
    if (configData["power"]["deep_sleep_duration_sec"]) {
        config.deep_sleep_sec = configData["power"]["deep_sleep_duration_sec"];
        configChanged = true;
    }
    if (configData["power"]["radio_coordination_delay_ms"]) {
        config.radio_coordination_delay_ms = configData["power"]["radio_coordination_delay_ms"];
        configChanged = true;
    }
    
    // Update battery config
    if (configData["battery"]["critical_voltage"]) {
        config.battery_critical_voltage = configData["battery"]["critical_voltage"];
        configChanged = true;
    }
    if (configData["battery"]["low_voltage"]) {
        config.min_battery_voltage = configData["battery"]["low_voltage"];
        configChanged = true;
    }
    
    if (configChanged) {
        Serial.printf("✅ Config updated: %s v%d\n", config.bike_name, config.version);
        saveConfig();
        
        // Send confirmation
        if (pConfigChar) {
            DynamicJsonDocument confirm(128);
            confirm["type"] = "config_received";
            confirm["bike_id"] = config.bike_id;
            confirm["status"] = "ok";
            
            String confirmStr;
            serializeJson(confirm, confirmStr);
            pConfigChar->writeValue(confirmStr.c_str());
            Serial.println("✅ Config confirmation sent");
        }
    } else {
        Serial.println("📝 No config changes detected");
    }
}

void onBLEDisconnected() {
    Serial.println("🔴 BLE disconnected");
    bleConnected = false;
    pDataChar = nullptr;
    pConfigChar = nullptr;
    if (pClient) {
        pClient = nullptr;
    }
}