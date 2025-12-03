#include <Arduino.h>
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include "config.h"

// Simulador de bicicleta BPR
class BikeSimulator {
public:
    NimBLEClient* pClient = nullptr;
private:
    bool connected = false;
    String bikeId = "bike_sim_01";
    float batteryLevel = 3.8;
    unsigned long lastUpdate = 0;

    class ClientCallbacks : public NimBLEClientCallbacks {
        BikeSimulator* simulator;
    public:
        ClientCallbacks(BikeSimulator* sim) : simulator(sim) {}
        
        void onConnect(NimBLEClient* pClient) {
            Serial.println("🔵 Conectado à Central BPR");
            simulator->connected = true;
        }
        
        void onDisconnect(NimBLEClient* pClient) {
            Serial.println("🔴 Desconectado da Central BPR");
            simulator->connected = false;
        }
    };

public:
    bool init(String id) {
        bikeId = id;
        Serial.printf("🚲 Inicializando simulador: %s\n", bikeId.c_str());
        
        NimBLEDevice::init(("BPR_Bike_" + bikeId).c_str());
        pClient = NimBLEDevice::createClient();
        pClient->setClientCallbacks(new ClientCallbacks(this));
        
        return true;
    }
    
    bool connectToCentral() {
        if (!pClient) return false;
        
        Serial.println("🔍 Procurando Central BPR...");
        NimBLEScan* pScan = NimBLEDevice::getScan();
        pScan->setActiveScan(true);
        pScan->setInterval(100);
        pScan->setWindow(99);
        
        NimBLEScanResults results = pScan->start(5, false);
        
        for (int i = 0; i < results.getCount(); i++) {
            NimBLEAdvertisedDevice device = results.getDevice(i);
            if (device.getName() == "BPR Base Station") {
                Serial.printf("✅ Central encontrada: %s\n", device.getAddress().toString().c_str());
                
                if (pClient->connect(&device)) {
                    sendBikeData();
                    return true;
                }
            }
        }
        
        Serial.println("❌ Central não encontrada");
        return false;
    }
    
    void sendBikeData() {
        Serial.println("📡 Enviando dados da bicicleta...");
        
        // Criar JSON com dados da bicicleta
        DynamicJsonDocument bikeDoc(1024);
        bikeDoc["uid"] = bikeId;
        bikeDoc["base_id"] = "base01";
        bikeDoc["battery_voltage"] = batteryLevel;
        bikeDoc["status"] = "active";
        bikeDoc["last_ble_contact"] = millis() / 1000;
        bikeDoc["last_wifi_scan"] = millis() / 1000;
        bikeDoc["last_position"]["lat"] = -8.064;
        bikeDoc["last_position"]["lng"] = -34.882;
        bikeDoc["last_position"]["source"] = "wifi";
        
        String bikeJson;
        serializeJson(bikeDoc, bikeJson);
        
        // Criar JSON com scan WiFi simulado
        DynamicJsonDocument wifiDoc(1024);
        JsonArray networks = wifiDoc.createNestedArray("networks");
        
        JsonObject net1 = networks.createNestedObject();
        net1["ssid"] = "NET_5G_CASA";
        net1["bssid"] = "AA:BB:CC:11:22:33";
        net1["rssi"] = -65;
        
        JsonObject net2 = networks.createNestedObject();
        net2["ssid"] = "CLARO_WIFI";
        net2["bssid"] = "CC:DD:EE:44:55:66";
        net2["rssi"] = -78;
        
        JsonObject net3 = networks.createNestedObject();
        net3["ssid"] = "VIVO_FIBRA";
        net3["bssid"] = "FF:AA:BB:77:88:99";
        net3["rssi"] = -82;
        
        wifiDoc["timestamp"] = millis() / 1000;
        wifiDoc["bike_id"] = bikeId;
        
        String wifiJson;
        serializeJson(wifiDoc, wifiJson);
        
        // Enviar dados via BLE
        auto pService = pClient->getService("BAAD");
        if (pService) {
            // Enviar dados da bicicleta
            auto pBikeChar = pService->getCharacteristic("F00D");
            if (pBikeChar) {
                pBikeChar->writeValue(bikeJson.c_str());
                Serial.printf("📝 Dados da bike enviados: %s\n", bikeJson.c_str());
            }
            
            // Enviar dados WiFi
            auto pWifiChar = pService->getCharacteristic("BEEF");
            if (pWifiChar) {
                pWifiChar->writeValue(wifiJson.c_str());
                Serial.printf("📶 Dados WiFi enviados: %s\n", wifiJson.c_str());
            }
        } else {
            Serial.println("❌ Serviço BAAD não encontrado");
        }
    }
    
    void updateBattery(float voltage) {
        if (!connected || !pClient) return;
        
        batteryLevel = voltage;
        
        // Criar JSON com alerta de bateria
        DynamicJsonDocument alertDoc(512);
        alertDoc["type"] = "battery_alert";
        alertDoc["bike_id"] = bikeId;
        alertDoc["battery_voltage"] = batteryLevel;
        alertDoc["timestamp"] = millis() / 1000;
        alertDoc["critical"] = (batteryLevel < 3.45);
        
        String alertJson;
        serializeJson(alertDoc, alertJson);
        
        auto pService = pClient->getService("BAAD");
        if (pService) {
            auto pChar = pService->getCharacteristic("BEEF");
            if (pChar) {
                pChar->writeValue(alertJson.c_str());
                Serial.printf("🔋 Alerta bateria enviado: %s\n", alertJson.c_str());
            }
        }
    }
    
    void simulateActivity() {
        if (!connected) return;
        
        unsigned long now = millis();
        if (now - lastUpdate < 10000) return; // Atualiza a cada 10s
        
        // Simula descarga da bateria
        batteryLevel -= 0.02;
        if (batteryLevel < 3.0) batteryLevel = 4.2; // Reset
        
        // Enviar dados atualizados
        sendBikeData();
        lastUpdate = now;
    }
    
    void disconnect() {
        if (pClient && connected) {
            pClient->disconnect();
        }
    }
    
    bool isConnected() { return connected; }
};

BikeSimulator bike;
enum TestMode { IDLE, BLE_TEST, BATTERY_TEST, MULTI_TEST, SIGNAL_TEST };
TestMode currentTest = IDLE;
unsigned long testStart = 0;

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("\n🧪 BPR Bike Simulator");
    Serial.println("====================");
    Serial.println("Comandos:");
    Serial.println("1 - Teste de Conexão BLE");
    Serial.println("2 - Teste de Bateria Baixa");
    Serial.println("3 - Teste Multi-bicicleta");
    Serial.println("4 - Teste de Intensidade do Sinal");
    Serial.println("0 - Desconectar");
    
    bike.init("sim_bike_01");
}

void handleCommand(int cmd);
void runActiveTest();
void runBLETest(unsigned long elapsed);
void runBatteryTest(unsigned long elapsed);
void runMultiTest(unsigned long elapsed);
void runSignalTest(unsigned long elapsed);
int8_t getConnectionRSSI();
void displaySignalStrength(int8_t rssi);

void loop() {
    // Processa comandos seriais
    if (Serial.available()) {
        int cmd = Serial.parseInt();
        handleCommand(cmd);
    }
    
    // Executa teste ativo
    runActiveTest();
    
    delay(100);
}

void handleCommand(int cmd) {
    switch (cmd) {
        case 1:
            Serial.println("\n🔵 Iniciando teste de conexão BLE...");
            currentTest = BLE_TEST;
            testStart = millis();
            break;
            
        case 2:
            Serial.println("\n🔋 Iniciando teste de bateria baixa...");
            currentTest = BATTERY_TEST;
            testStart = millis();
            break;
            
        case 3:
            Serial.println("\n🚲 Teste multi-bicicleta (use múltiplos ESP32s)");
            currentTest = MULTI_TEST;
            testStart = millis();
            break;
            
        case 4:
            Serial.println("\n📶 Iniciando teste de intensidade do sinal...");
            currentTest = SIGNAL_TEST;
            testStart = millis();
            break;
            
        case 0:
            Serial.println("\n🔴 Desconectando...");
            bike.disconnect();
            currentTest = IDLE;
            break;
    }
}

void runActiveTest() {
    unsigned long elapsed = millis() - testStart;
    
    switch (currentTest) {
        case BLE_TEST:
            runBLETest(elapsed);
            break;
            
        case BATTERY_TEST:
            runBatteryTest(elapsed);
            break;
            
        case MULTI_TEST:
            runMultiTest(elapsed);
            break;
            
        case SIGNAL_TEST:
            runSignalTest(elapsed);
            break;
            
        case IDLE:
            break;
    }
}

void runBLETest(unsigned long elapsed) {
    static bool connected = false;
    
    if (elapsed < 5000 && !connected) {
        if (bike.connectToCentral()) {
            connected = true;
        }
    } else if (connected) {
        bike.simulateActivity();
        
        if (elapsed > 30000) {
            Serial.println("🏁 Teste BLE concluído");
            bike.disconnect();
            currentTest = IDLE;
            connected = false;
        }
    } else if (elapsed > 10000) {
        Serial.println("❌ Timeout - Central não encontrada");
        currentTest = IDLE;
    }
}

void runBatteryTest(unsigned long elapsed) {
    static bool connected = false;
    static bool alertSent = false;
    
    if (elapsed < 5000 && !connected) {
        if (bike.connectToCentral()) {
            connected = true;
        }
    } else if (connected && !alertSent) {
        bike.updateBattery(3.2); // Bateria baixa
        alertSent = true;
        Serial.println("🔋 Alerta de bateria baixa enviado!");
    } else if (elapsed > 20000) {
        Serial.println("🏁 Teste de bateria concluído");
        bike.disconnect();
        currentTest = IDLE;
        connected = false;
        alertSent = false;
    }
}

void runMultiTest(unsigned long elapsed) {
    Serial.println("📝 Use múltiplos ESP32s com este firmware");
    Serial.println("   Cada um simulará uma bicicleta diferente");
    currentTest = IDLE;
}

void runSignalTest(unsigned long elapsed) {
    static bool connected = false;
    static unsigned long lastCheck = 0;
    
    if (elapsed < 5000 && !connected) {
        if (bike.connectToCentral()) {
            connected = true;
            Serial.println("\n📶 Monitoramento de sinal iniciado");
            Serial.println("   Afaste-se da central para testar alcance");
            Serial.println("   RSSI | Qualidade | Status");
            Serial.println("   -----|-----------|--------");
        }
    } else if (connected && (elapsed - lastCheck > 2000)) {
        int8_t rssi = getConnectionRSSI();
        displaySignalStrength(rssi);
        lastCheck = elapsed;
        
        if (!bike.isConnected()) {
            Serial.println("\n❌ Conexão perdida! Distância máxima atingida");
            Serial.println("🔄 Tentando reconectar...");
            if (bike.connectToCentral()) {
                Serial.println("✅ Reconectado com sucesso!");
            }
        }
        
        if (elapsed > 60000) {
            Serial.println("\n🏁 Teste de sinal concluído");
            bike.disconnect();
            currentTest = IDLE;
            connected = false;
        }
    } else if (elapsed > 10000 && !connected) {
        Serial.println("❌ Timeout - Central não encontrada");
        currentTest = IDLE;
    }
}

int8_t getConnectionRSSI() {
    if (bike.pClient && bike.isConnected()) {
        return bike.pClient->getRssi();
    }
    return -100; // Sinal perdido
}

void displaySignalStrength(int8_t rssi) {
    String quality, status, bars;
    
    if (rssi >= -50) {
        quality = "Excelente";
        status = "Muito Perto";
        bars = "████████";
    } else if (rssi >= -60) {
        quality = "Muito Bom";
        status = "Perto";
        bars = "███████░";
    } else if (rssi >= -70) {
        quality = "Bom";
        status = "Distância OK";
        bars = "██████░░";
    } else if (rssi >= -80) {
        quality = "Regular";
        status = "Longe";
        bars = "████░░░░";
    } else if (rssi >= -90) {
        quality = "Fraco";
        status = "Muito Longe";
        bars = "██░░░░░░";
    } else {
        quality = "Crítico";
        status = "Limite";
        bars = "█░░░░░░░";
    }
    
    Serial.printf("   %3ddBm | %-9s | %s %s\n", rssi, quality.c_str(), status.c_str(), bars.c_str());
    
    if (rssi < -85) {
        Serial.println("   ⚠️  Sinal fraco - risco de desconexão!");
    }
}