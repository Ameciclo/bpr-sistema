#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include "ble_simple.h"
#include "config_manager.h"
#include "bike_manager.h"

// Configurações padrão (fallbacks)
struct CentralConfig {
    String base_id = "base01";
    int sync_interval_sec = 300;
    int wifi_timeout_sec = 30;
    int cleanup_interval_sec = 60;
    int led_count_interval_sec = 30;
    int log_interval_sec = 15;
    int firebase_batch_size = 8000;
    int https_timeout_ms = 10000;
    int firebase_response_timeout_ms = 5000;
    int led_pin = 8;
    String ntp_server = "pool.ntp.org";
    int timezone_offset = -10800;
    int ntp_update_interval_ms = 60000;
    int firebase_port = 443;
    unsigned long min_valid_timestamp = 1600000000;
    struct {
        int boot_ms = 100;
        int ble_ready_ms = 2000;
        int bike_arrived_ms = 150;
        int bike_left_ms = 800;
        int wifi_sync_ms = 500;
        int count_ms = 300;
        int count_pause_ms = 1500;
        int error_ms = 50;
    } led;
} config;

void parseConfigFromJson(const String& jsonStr) {
    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, jsonStr) == DeserializationError::Ok) {
        config.base_id = doc["base_id"] | config.base_id;
        config.sync_interval_sec = doc["sync_interval_sec"] | config.sync_interval_sec;
        config.wifi_timeout_sec = doc["wifi_timeout_sec"] | config.wifi_timeout_sec;
        config.cleanup_interval_sec = doc["cleanup_interval_sec"] | config.cleanup_interval_sec;
        config.led_count_interval_sec = doc["led_count_interval_sec"] | config.led_count_interval_sec;
        config.log_interval_sec = doc["log_interval_sec"] | config.log_interval_sec;
        config.firebase_batch_size = doc["firebase_batch_size"] | config.firebase_batch_size;
        config.https_timeout_ms = doc["https_timeout_ms"] | config.https_timeout_ms;
        config.firebase_response_timeout_ms = doc["firebase_response_timeout_ms"] | config.firebase_response_timeout_ms;
        config.led_pin = doc["led_pin"] | config.led_pin;
        config.ntp_server = doc["ntp_server"] | config.ntp_server;
        config.timezone_offset = doc["timezone_offset"] | config.timezone_offset;
        config.ntp_update_interval_ms = doc["ntp_update_interval_ms"] | config.ntp_update_interval_ms;
        config.firebase_port = doc["firebase_port"] | config.firebase_port;
        config.min_valid_timestamp = doc["min_valid_timestamp"] | config.min_valid_timestamp;
        if (doc.containsKey("led")) {
            auto led = doc["led"];
            config.led.boot_ms = led["boot_ms"] | config.led.boot_ms;
            config.led.ble_ready_ms = led["ble_ready_ms"] | config.led.ble_ready_ms;
            config.led.bike_arrived_ms = led["bike_arrived_ms"] | config.led.bike_arrived_ms;
            config.led.bike_left_ms = led["bike_left_ms"] | config.led.bike_left_ms;
            config.led.wifi_sync_ms = led["wifi_sync_ms"] | config.led.wifi_sync_ms;
            config.led.count_ms = led["count_ms"] | config.led.count_ms;
            config.led.count_pause_ms = led["count_pause_ms"] | config.led.count_pause_ms;
            config.led.error_ms = led["error_ms"] | config.led.error_ms;
        }
        Serial.printf("✅ Config aplicada: %s\n", config.base_id.c_str());
    }
}

bool downloadCentralConfig() {
    // Obter base_id do config.json
    File localConfig = LittleFS.open("/config.json", "r");
    String baseId = config.base_id; // fallback
    if (localConfig) {
        DynamicJsonDocument doc(512);
        if (deserializeJson(doc, localConfig) == DeserializationError::Ok) {
            if (doc.containsKey("central") && doc["central"].containsKey("id")) {
                baseId = doc["central"]["id"].as<String>();
            }
        }
        localConfig.close();
    }
    
    String configPath = "/central_configs/" + baseId + ".json";
    Serial.printf("📥 Baixando config: %s\n", configPath.c_str());
    
    File configFile = LittleFS.open("/config.json", "r");
    if (!configFile) return false;
    
    DynamicJsonDocument doc(512);
    deserializeJson(doc, configFile);
    configFile.close();
    
    String firebaseUrl = doc["firebase"]["database_url"];
    String url = firebaseUrl;
    url.replace("https://", "");
    int slashIndex = url.indexOf('/');
    String host = url.substring(0, slashIndex);
    
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(config.https_timeout_ms);
    
    if (client.connect(host.c_str(), config.firebase_port)) {
        String request = "GET " + configPath + " HTTP/1.1\r\n";
        request += "Host: " + host + "\r\n";
        request += "Connection: close\r\n\r\n";
        
        client.print(request);
        
        String response = "";
        unsigned long start = millis();
        while (client.connected() && millis() - start < config.firebase_response_timeout_ms) {
            if (client.available()) {
                response = client.readString();
                break;
            }
            delay(10);
        }
        client.stop();
        
        if (response.indexOf("200 OK") >= 0) {
            int jsonStart = response.indexOf("\r\n\r\n");
            if (jsonStart >= 0) {
                String configJson = response.substring(jsonStart + 4);
                configJson.trim();
                
                if (configJson.length() > 10 && configJson != "null") {
                    // Aplicar configuração baixada
                    parseConfigFromJson(configJson);
                    
                    // Salvar config completa substituindo a básica
                    File saveFile = LittleFS.open("/config.json", "w");
                    if (saveFile) {
                        saveFile.print(configJson);
                        saveFile.close();
                        Serial.println("✅ Config completa salva em /config.json");
                    }
                    
                    Serial.println("✅ Config baixada do Firebase");
                    return true;
                }
            }
        }
    }
    
    Serial.println("❌ Falha ao baixar config do Firebase");
    return false;
}

bool isFirstBoot() {
    return !LittleFS.exists("/config.json");
}

void loadCentralConfig() {
    // Tentar carregar config.json
    File configFile = LittleFS.open("/config.json", "r");
    if (configFile) {
        String configJson = configFile.readString();
        configFile.close();
        
        // Se tem configurações avançadas, aplicar
        DynamicJsonDocument testDoc(256);
        if (deserializeJson(testDoc, configJson) == DeserializationError::Ok) {
            if (testDoc.containsKey("sync_interval_sec")) {
                // É config completa baixada do Firebase
                parseConfigFromJson(configJson);
                Serial.println("✅ Config completa carregada");
            } else {
                // É config básica - extrair base_id
                if (testDoc.containsKey("base_id")) {
                    config.base_id = testDoc["base_id"].as<String>();
                    Serial.printf("✅ Config básica carregada - Base: %s\n", config.base_id.c_str());
                }
            }
        }
    } else {
        Serial.println("⚠️ Config não encontrada - usando padrões");
    }
}

enum LEDPattern {
    LED_OFF,              // Desligado
    LED_BOOT,             // Inicializando (piscar rápido)
    LED_SETUP_MODE,       // Modo setup (piscar alternado)
    LED_BLE_READY,        // BLE ativo (piscar lento)
    LED_BIKE_ARRIVED,     // Bike chegou (3 piscadas rápidas)
    LED_BIKE_LEFT,        // Bike saiu (1 piscada longa)
    LED_WIFI_SYNC,        // Sincronizando (piscar médio)
    LED_COUNT_BIKES,      // Mostrar qtd bikes (N piscadas)
    LED_ERROR             // Erro (piscar muito rápido)
};

LEDPattern currentLEDPattern = LED_OFF;
unsigned long ledLastChange = 0;
bool ledState = false;
int ledCounter = 0;
int bikesToShow = 0;

// Estados da central
enum CentralMode {
    MODE_SETUP_AP,     // Modo setup inicial (AP)
    MODE_BLE_ONLY,     // Só BLE ativo (padrão)
    MODE_WIFI_SYNC,    // WiFi + Firebase temporário
    MODE_SHUTDOWN      // Desligando WiFi
};

// Variáveis do modo setup
#include <WebServer.h>
WebServer setupServer(80);
bool setupComplete = false;

CentralMode currentMode = MODE_BLE_ONLY;
String pendingData = "";
unsigned long lastSync = 0;
unsigned long modeStart = 0;

// NTP + Correção temporal
WiFiUDP ntpUDP;
NTPClient *timeClient = nullptr;
bool ntpSynced = false;
unsigned long ntpEpoch = 0;
unsigned long ntpMillisBase = 0;

// Função para detectar e corrigir timestamps
unsigned long correctTimestamp(unsigned long bikeTimestamp, unsigned long bikeMillis) {
    // Se bike tem NTP válido
    if (bikeTimestamp > config.min_valid_timestamp) {
        Serial.printf("🕰️ Bike com NTP válido: %lu\n", bikeTimestamp);
        return bikeTimestamp;
    }
    
    // Se central tem NTP, corrigir timestamp da bike
    if (ntpSynced && ntpEpoch > 0) {
        unsigned long correctedTime = ntpEpoch + ((millis() - ntpMillisBase) / 1000);
        Serial.printf("🔧 Corrigindo timestamp: %lu -> %lu\n", bikeTimestamp, correctedTime);
        return correctedTime;
    }
    
    // Fallback: usar timestamp original
    Serial.printf("⚠️ Sem correção disponível: %lu\n", bikeTimestamp);
    return bikeTimestamp;
}

// Função para enviar NTP para bike via BLE
void sendNTPToBike() {
    if (ntpSynced && ntpEpoch > 0) {
        unsigned long currentEpoch = ntpEpoch + ((millis() - ntpMillisBase) / 1000);
        
        // TODO: Implementar envio via BLE
        Serial.printf("📡 Enviando NTP para bike: %lu\n", currentEpoch);
        
        // Criar JSON com correção temporal
        String ntpSync = "{\"type\":\"ntp_sync\",\"epoch\":" + String(currentEpoch) + ",\"millis\":" + String(millis()) + "}";
        
        // Adicionar aos dados pendentes para próxima conexão
        if (pendingData.length() > 0) pendingData += ",";
        pendingData += ntpSync;
        
        Serial.println("✅ NTP adicionado aos dados pendentes");
    }
}

bool uploadToFirebase(String path, String json) {
    File configFile = LittleFS.open("/config.json", "r");
    if (!configFile) {
        Serial.println("❌ Config não encontrado");
        return false;
    }
    
    DynamicJsonDocument doc(512);
    deserializeJson(doc, configFile);
    configFile.close();
    
    String firebaseUrl = doc["firebase"]["database_url"];
    Serial.printf("🔗 URL: %s\n", firebaseUrl.c_str());
    
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(config.https_timeout_ms);
    
    // Parse URL
    String url = firebaseUrl;
    url.replace("https://", "");
    int slashIndex = url.indexOf('/');
    String host = url.substring(0, slashIndex);
    
    Serial.printf("🌐 Host: %s\n", host.c_str());
    Serial.printf("📄 Path: %s\n", path.c_str());
    Serial.printf("📦 JSON size: %d\n", json.length());
    
    String fullPath = path + ".json";
    
    if (client.connect(host.c_str(), config.firebase_port)) {
        Serial.println("✅ Conectado ao Firebase");
        
        String request = "PUT " + fullPath + " HTTP/1.1\r\n";
        request += "Host: " + host + "\r\n";
        request += "Content-Type: application/json\r\n";
        request += "Content-Length: " + String(json.length()) + "\r\n";
        request += "Connection: close\r\n\r\n";
        
        client.print(request);
        client.print(json);
        
        String response = "";
        unsigned long start = millis();
        while (client.connected() && millis() - start < config.firebase_response_timeout_ms) {
            if (client.available()) {
                response = client.readString();
                break;
            }
            delay(10);
        }
        client.stop();
        
        Serial.printf("📡 Response: %s\n", response.substring(0, 200).c_str());
        return response.indexOf("200 OK") >= 0;
    } else {
        Serial.println("❌ Falha conexão Firebase");
        return false;
    }
}

bool createNewBase(String baseId, String baseName) {
    Serial.printf("🆕 Criando nova base: %s\n", baseId.c_str());
    
    // Criar config padrão para nova base
    DynamicJsonDocument newConfig(2048);
    newConfig["base_id"] = baseId;
    newConfig["central"]["id"] = baseId;
    newConfig["central"]["name"] = baseName;
    newConfig["central"]["max_bikes"] = 10;
    newConfig["central"]["location"]["lat"] = -8.062;
    newConfig["central"]["location"]["lng"] = -34.881;
    
    // Copiar configurações padrão
    newConfig["sync_interval_sec"] = config.sync_interval_sec;
    newConfig["wifi_timeout_sec"] = config.wifi_timeout_sec;
    newConfig["cleanup_interval_sec"] = config.cleanup_interval_sec;
    newConfig["led_count_interval_sec"] = config.led_count_interval_sec;
    newConfig["log_interval_sec"] = config.log_interval_sec;
    newConfig["firebase_batch_size"] = config.firebase_batch_size;
    newConfig["https_timeout_ms"] = config.https_timeout_ms;
    newConfig["firebase_response_timeout_ms"] = config.firebase_response_timeout_ms;
    newConfig["led_pin"] = config.led_pin;
    newConfig["ntp_server"] = config.ntp_server;
    newConfig["timezone_offset"] = config.timezone_offset;
    newConfig["ntp_update_interval_ms"] = config.ntp_update_interval_ms;
    newConfig["firebase_port"] = config.firebase_port;
    newConfig["min_valid_timestamp"] = config.min_valid_timestamp;
    
    // LED configs
    newConfig["led"]["boot_ms"] = config.led.boot_ms;
    newConfig["led"]["ble_ready_ms"] = config.led.ble_ready_ms;
    newConfig["led"]["bike_arrived_ms"] = config.led.bike_arrived_ms;
    newConfig["led"]["bike_left_ms"] = config.led.bike_left_ms;
    newConfig["led"]["wifi_sync_ms"] = config.led.wifi_sync_ms;
    newConfig["led"]["count_ms"] = config.led.count_ms;
    newConfig["led"]["count_pause_ms"] = config.led.count_pause_ms;
    newConfig["led"]["error_ms"] = config.led.error_ms;
    
    String configJson;
    serializeJson(newConfig, configJson);
    
    // Tentar enviar para Firebase
    String configPath = "/central_configs/" + baseId;
    if (uploadToFirebase(configPath, configJson)) {
        Serial.println("✅ Nova base criada no Firebase");
        
        // Aplicar config localmente
        parseConfigFromJson(configJson);
        
        // Salvar localmente
        File saveFile = LittleFS.open("/config.json", "w");
        if (saveFile) {
            saveFile.print(configJson);
            saveFile.close();
        }
        
        return true;
    }
    
    Serial.println("❌ Falha ao criar base no Firebase");
    return false;
}

void updateLED() {
    unsigned long now = millis();
    
    switch (currentLEDPattern) {
        case LED_OFF:
            digitalWrite(config.led_pin, LOW);
            break;
            
        case LED_BOOT:
            if (now - ledLastChange > config.led.boot_ms) {
                ledState = !ledState;
                digitalWrite(config.led_pin, ledState);
                ledLastChange = now;
            }
            break;
            
        case LED_SETUP_MODE:
            if (now - ledLastChange > 1000) { // Piscar alternado 1s
                ledState = !ledState;
                digitalWrite(config.led_pin, ledState);
                ledLastChange = now;
            }
            break;
            
        case LED_BLE_READY:
            if (now - ledLastChange > config.led.ble_ready_ms) {
                ledState = !ledState;
                digitalWrite(config.led_pin, ledState);
                ledLastChange = now;
            }
            break;
            
        case LED_BIKE_ARRIVED:
            if (ledCounter < 6) { // 3 piscadas = 6 mudanças
                if (now - ledLastChange > config.led.bike_arrived_ms) {
                    ledState = !ledState;
                    digitalWrite(config.led_pin, ledState);
                    ledLastChange = now;
                    ledCounter++;
                }
            } else {
                currentLEDPattern = LED_BLE_READY;
                ledCounter = 0;
            }
            break;
            
        case LED_BIKE_LEFT:
            if (ledCounter == 0) {
                digitalWrite(config.led_pin, HIGH);
                ledLastChange = now;
                ledCounter = 1;
            } else if (now - ledLastChange > config.led.bike_left_ms) {
                digitalWrite(config.led_pin, LOW);
                currentLEDPattern = LED_BLE_READY;
                ledCounter = 0;
            }
            break;
            
        case LED_WIFI_SYNC:
            if (now - ledLastChange > config.led.wifi_sync_ms) {
                ledState = !ledState;
                digitalWrite(config.led_pin, ledState);
                ledLastChange = now;
            }
            break;
            
        case LED_COUNT_BIKES:
            if (ledCounter < bikesToShow * 2) { // N bikes = N*2 mudanças
                if (now - ledLastChange > config.led.count_ms) {
                    ledState = !ledState;
                    digitalWrite(config.led_pin, ledState);
                    ledLastChange = now;
                    ledCounter++;
                }
            } else if (now - ledLastChange > config.led.count_pause_ms) {
                currentLEDPattern = LED_BLE_READY;
                ledCounter = 0;
            }
            break;
            
        case LED_ERROR:
            if (now - ledLastChange > config.led.error_ms) {
                ledState = !ledState;
                digitalWrite(config.led_pin, ledState);
                ledLastChange = now;
            }
            break;
    }
}

void setLEDPattern(LEDPattern pattern, int count = 0) {
    currentLEDPattern = pattern;
    ledCounter = 0;
    ledLastChange = millis();
    bikesToShow = count;
    
    String patternName[] = {"OFF", "BOOT", "SETUP_MODE", "BLE_READY", "BIKE_ARRIVED", "BIKE_LEFT", "WIFI_SYNC", "COUNT_BIKES", "ERROR"};
    Serial.printf("💡 LED: %s%s\n", patternName[pattern].c_str(), count > 0 ? (" (" + String(count) + ")").c_str() : "");
}

void setupWebServer() {
    setupServer.on("/", []() {
        String html = R"(
<!DOCTYPE html>
<html>
<head>
    <title>BPR Central Setup</title>
    <meta charset="utf-8">
    <style>
        body { font-family: Arial; margin: 40px; background: #f0f0f0; }
        .container { background: white; padding: 30px; border-radius: 10px; max-width: 500px; }
        input, select { width: 100%; padding: 10px; margin: 10px 0; border: 1px solid #ddd; border-radius: 5px; }
        button { background: #007cba; color: white; padding: 15px 30px; border: none; border-radius: 5px; cursor: pointer; }
        button:hover { background: #005a87; }
        .status { margin: 20px 0; padding: 10px; border-radius: 5px; }
        .success { background: #d4edda; color: #155724; }
        .error { background: #f8d7da; color: #721c24; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🚲 BPR Central Setup</h1>
        <p>Configure sua central pela primeira vez:</p>
        
        <form action="/save" method="POST">
            <label>ID da Base:</label>
            <input type="text" name="base_id" placeholder="ex: ameciclo, cepas, ctresiste" required>
            
            <label>Nome da Base:</label>
            <input type="text" name="base_name" placeholder="ex: Ameciclo, CEPAS, CTResiste" required>
            
            <label>WiFi SSID:</label>
            <input type="text" name="wifi_ssid" required>
            
            <label>WiFi Password:</label>
            <input type="password" name="wifi_password" required>
            
            <label>Firebase Database URL:</label>
            <input type="url" name="firebase_url" value="https://botaprarodar-routes-default-rtdb.firebaseio.com" required>
            
            <label>Firebase API Key:</label>
            <input type="text" name="firebase_api_key" placeholder="AIzaSyBOf0iB1PE3byamxPaPnxRdjZHT-Wx5mKs" required>
            
            <button type="submit">Configurar Central</button>
        </form>
    </div>
</body>
</html>
        )";
        setupServer.send(200, "text/html", html);
    });
    
    setupServer.on("/save", HTTP_POST, []() {
        String baseId = setupServer.arg("base_id");
        String baseName = setupServer.arg("base_name");
        String wifiSSID = setupServer.arg("wifi_ssid");
        String wifiPassword = setupServer.arg("wifi_password");
        String firebaseUrl = setupServer.arg("firebase_url");
        String firebaseApiKey = setupServer.arg("firebase_api_key");
        
        // Criar config básica para conectar
        DynamicJsonDocument basicConfig(512);
        basicConfig["base_id"] = baseId;
        basicConfig["wifi"]["ssid"] = wifiSSID;
        basicConfig["wifi"]["password"] = wifiPassword;
        basicConfig["firebase"]["database_url"] = firebaseUrl;
        basicConfig["firebase"]["api_key"] = firebaseApiKey;
        
        String configJson;
        serializeJson(basicConfig, configJson);
        
        // Salvar config básica
        File configFile = LittleFS.open("/config.json", "w");
        if (configFile) {
            configFile.print(configJson);
            configFile.close();
            
            // Salvar também firebase_config.json para compatibilidade
            DynamicJsonDocument fbConfig(256);
            fbConfig["firebase_host"] = firebaseUrl.substring(8); // Remove https://
            fbConfig["firebase_auth"] = firebaseApiKey;
            fbConfig["base_id"] = baseId;
            fbConfig["base_name"] = baseName;
            fbConfig["wifi_ssid"] = wifiSSID;
            fbConfig["wifi_password"] = wifiPassword;
            
            String fbConfigJson;
            serializeJson(fbConfig, fbConfigJson);
            
            File fbConfigFile = LittleFS.open("/firebase_config.json", "w");
            if (fbConfigFile) {
                fbConfigFile.print(fbConfigJson);
                fbConfigFile.close();
            }
            
            config.base_id = baseId;
            
            String response = R"(
<!DOCTYPE html>
<html>
<head><title>Setup Completo</title><meta charset="utf-8"></head>
<body style="font-family: Arial; margin: 40px; text-align: center;">
    <h1>✅ Configuração Salva!</h1>
    <p>A central irá reiniciar e conectar ao WiFi.</p>
    <p>Base: <strong>)" + baseId + R"(</strong></p>
    <p>A central tentará baixar configurações existentes ou criar uma nova base.</p>
</body>
</html>
            )";
            
            setupServer.send(200, "text/html", response);
            setupComplete = true;
        } else {
            setupServer.send(500, "text/plain", "Erro ao salvar configuração");
        }
    });
    
    setupServer.begin();
    Serial.println("🌐 Servidor web iniciado em http://192.168.4.1");
}

void startSetupAP() {
    Serial.println("🔧 Iniciando modo setup...");
    
    // Criar AP
    String apName = "BPR_Setup_" + WiFi.macAddress().substring(9);
    apName.replace(":", "");
    
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apName.c_str(), "bpr12345");
    
    Serial.printf("📶 AP criado: %s\n", apName.c_str());
    Serial.println("🔑 Senha: bpr12345");
    Serial.println("🌐 Acesse: http://192.168.4.1");
    
    // LED modo setup
    setLEDPattern(LED_SETUP_MODE);
    
    // Iniciar servidor web
    setupWebServer();
    
    currentMode = MODE_SETUP_AP;
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("\n🚲 BPR Central - HUB INTELIGENTE");
    Serial.println("===================================");
    
    // LittleFS
    if (!LittleFS.begin()) {
        LittleFS.format();
        LittleFS.begin();
    }
    Serial.println("✅ LittleFS OK");
    
    // Configurar LED
    pinMode(config.led_pin, OUTPUT);
    setLEDPattern(LED_BOOT);
    
    // Verificar se é primeira vez
    if (isFirstBoot()) {
        Serial.println("🆕 Primeira execução detectada!");
        startSetupAP();
        return;
    }
    
    // Carregar configurações
    loadCentralConfig();
    
    // Inicializar NTP com configurações
    timeClient = new NTPClient(ntpUDP, config.ntp_server.c_str(), config.timezone_offset, config.ntp_update_interval_ms);
    
    // Inicializar módulos
    initBikeManager();
    
    // Carregar cache de configurações
    if (!loadConfigCache()) {
        Serial.println("⚠️ Cache de config não encontrado - será baixado na próxima sync");
        invalidateConfig();
    }
    
    // BLE sempre ativo
    if (initBLESimple()) {
        Serial.println("✅ BLE OK");
        startBLEServer();
        setLEDPattern(LED_BLE_READY);
    } else {
        Serial.println("❌ BLE falhou");
        setLEDPattern(LED_ERROR);
    }
    
    Serial.println("✅ Central em modo BLE");
    Serial.println("📶 WiFi será ativado quando necessário");
}

void handleBLEMode() {
    // Processar configurações pendentes
    processPendingConfigs();
    
    // Detectar mudanças no número de bikes
    static int lastBikeCount = -1;
    int currentBikeCount = getConnectedBikeCount();
    
    if (currentBikeCount != lastBikeCount && lastBikeCount >= 0) {
        if (currentBikeCount > lastBikeCount) {
            Serial.printf("🚲 Bike chegou! Total: %d\n", currentBikeCount);
            setLEDPattern(LED_BIKE_ARRIVED);
        } else {
            Serial.printf("🚲 Bike saiu! Total: %d\n", currentBikeCount);
            setLEDPattern(LED_BIKE_LEFT);
        }
    }
    lastBikeCount = currentBikeCount;
    
    // Mostrar contagem de bikes periodicamente
    static unsigned long lastCountShow = 0;
    if (millis() - lastCountShow > config.led_count_interval_sec * 1000 && currentBikeCount > 0) {
        setLEDPattern(LED_COUNT_BIKES, currentBikeCount);
        lastCountShow = millis();
    }
    
    // Limpeza periódica de conexões antigas
    static unsigned long lastCleanup = 0;
    if (millis() - lastCleanup > config.cleanup_interval_sec * 1000) {
        cleanupOldConnections();
        lastCleanup = millis();
    }
    
    // Verificar se precisa sincronizar
    bool needsSync = false;
    
    // Dados pendentes
    if (pendingData.length() > 0) needsSync = true;
    
    // Timeout de sync
    if (millis() - lastSync > config.sync_interval_sec * 1000) needsSync = true;
    
    // Config inválida (forçar download)
    if (!isConfigValid()) needsSync = true;
    
    if (needsSync) {
        Serial.println("📶 Ativando WiFi para sync...");
        setLEDPattern(LED_WIFI_SYNC);
        currentMode = MODE_WIFI_SYNC;
        modeStart = millis();
    }
}

void handleWiFiMode() {
    static bool wifiConnected = false;
    
    // Conectar WiFi se não conectado
    if (!wifiConnected) {
        File configFile = LittleFS.open("/config.json", "r");
        if (configFile) {
            DynamicJsonDocument doc(512);
            deserializeJson(doc, configFile);
            configFile.close();
            
            String ssid = doc["wifi"]["ssid"];
            String pass = doc["wifi"]["password"];
            
            WiFi.begin(ssid.c_str(), pass.c_str());
            Serial.println("📶 Conectando WiFi...");
        }
        wifiConnected = true;
        return;
    }
    
    // Verificar conexão
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("✅ WiFi conectado - sincronizando...");
        
        // Sincronizar NTP se necessário
        if (!ntpSynced) {
            Serial.println("🕰️ Sincronizando NTP...");
            timeClient->begin();
            if (timeClient->update()) {
                ntpSynced = true;
                ntpEpoch = timeClient->getEpochTime();
                ntpMillisBase = millis();
                Serial.printf("✅ NTP OK: %lu (base: %lu)\n", ntpEpoch, ntpMillisBase);
                
                // Preparar correção para próximas bikes
                sendNTPToBike();
            } else {
                Serial.println("⚠️ NTP falhou - usando millis()");
            }
        }
        
        // Baixar configurações se necessário
        if (!isConfigValid()) {
            Serial.println("📥 Baixando configurações...");
            if (downloadConfigs()) {
                // Marcar todas as bikes para receber nova config
                Serial.println("📝 Marcando bikes para reconfigurar...");
                // TODO: Implementar marcação de todas as bikes
            }
        }
        
        // Baixar configuração da central
        Serial.println("📥 Verificando config da central...");
        if (!downloadCentralConfig()) {
            // Se não conseguiu baixar, tentar criar nova base
            Serial.println("🆕 Base não existe - criando nova...");
            
            // Obter nome da base do config local
            File localConfig = LittleFS.open("/config.json", "r");
            String baseName = config.base_id;
            if (localConfig) {
                DynamicJsonDocument doc(256);
                if (deserializeJson(doc, localConfig) == DeserializationError::Ok) {
                    baseName = doc["base_name"] | config.base_id;
                }
                localConfig.close();
            }
            
            createNewBase(config.base_id, baseName);
        }
        
        // Enviar heartbeat da central
        unsigned long currentTimestamp = ntpSynced ? (ntpEpoch + ((millis() - ntpMillisBase) / 1000)) : millis()/1000;
        String centralHeartbeat = "{\"type\":\"central_heartbeat\",\"timestamp\":" + String(currentTimestamp) + ",\"bikes_connected\":" + String(getConnectedBikeCount()) + ",\"heap\":" + String(ESP.getFreeHeap()) + ",\"uptime\":" + String(millis()/1000) + "}";
        
        String heartbeatPath = "/bases/" + config.base_id + "/last_heartbeat";
        if (uploadToFirebase(heartbeatPath, centralHeartbeat)) {
            Serial.printf("💓 Heartbeat enviado para %s\n", config.base_id.c_str());
        }
        
        // Enviar dados pendentes para Firebase (HTTPS direto)
        if (pendingData.length() > 0) {
            Serial.println("🔥 Enviando dados...");
            Serial.printf("📦 Tamanho total: %d bytes\n", pendingData.length());
            
            // Verificar se precisa dividir em batches
            if (pendingData.length() > config.firebase_batch_size) {
                Serial.println("📦 Dados grandes - enviando em batches...");
                
                // Dividir por vírgulas (cada item JSON)
                int start = 0;
                int batchNum = 0;
                String currentBatch = "";
                
                while (start < pendingData.length()) {
                    int commaPos = pendingData.indexOf(',', start);
                    if (commaPos == -1) commaPos = pendingData.length();
                    
                    String item = pendingData.substring(start, commaPos);
                    
                    // Se adicionar este item ultrapassar limite, enviar batch atual
                    if (currentBatch.length() + item.length() > config.firebase_batch_size && currentBatch.length() > 0) {
                        String batchJson = "{\"timestamp\":" + String(millis()/1000) + ",\"batch\":" + String(batchNum) + ",\"data\":[" + currentBatch + "]}";
                        
                        if (uploadToFirebase("/central_data/batch_" + String(batchNum) + "_" + String(millis()), batchJson)) {
                            Serial.printf("✅ Batch %d enviado\n", batchNum);
                        } else {
                            Serial.printf("❌ Batch %d falhou\n", batchNum);
                        }
                        
                        currentBatch = "";
                        batchNum++;
                    }
                    
                    if (currentBatch.length() > 0) currentBatch += ",";
                    currentBatch += item;
                    start = commaPos + 1;
                }
                
                // Enviar último batch
                if (currentBatch.length() > 0) {
                    String batchJson = "{\"timestamp\":" + String(millis()/1000) + ",\"batch\":" + String(batchNum) + ",\"data\":[" + currentBatch + "]}";
                    
                    if (uploadToFirebase("/central_data/batch_" + String(batchNum) + "_" + String(millis()), batchJson)) {
                        Serial.printf("✅ Batch final %d enviado\n", batchNum);
                    }
                }
                
                pendingData = ""; // Limpar após todos os batches
                
            } else {
                // JSON pequeno - enviar direto
                unsigned long timestamp = ntpSynced ? (ntpEpoch + ((millis() - ntpMillisBase) / 1000)) : millis()/1000;
                String validJson = "{\"timestamp\":" + String(timestamp) + ",\"data\":[" + pendingData + "]}";
                
                if (uploadToFirebase("/central_data/" + config.base_id + "/" + String(timestamp), validJson)) {
                    Serial.println("✅ Upload OK");
                    pendingData = ""; // Limpar após envio
                } else {
                    Serial.println("❌ Upload falhou");
                }
            }
        }
        
        lastSync = millis();
        currentMode = MODE_SHUTDOWN;
        
    } else if (millis() - modeStart > config.wifi_timeout_sec * 1000) {
        Serial.println("⚠️ WiFi timeout");
        currentMode = MODE_SHUTDOWN;
    }
}

void handleShutdownMode() {
    Serial.println("🔴 Desligando WiFi...");
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    
    currentMode = MODE_BLE_ONLY;
    setLEDPattern(LED_BLE_READY);
    Serial.println("✅ Voltando ao modo BLE");
}

void handleSetupMode() {
    setupServer.handleClient();
    
    // Se setup foi completado, reiniciar
    if (setupComplete) {
        Serial.println("✅ Setup completo - reiniciando...");
        delay(2000);
        ESP.restart();
    }
}

void loop() {
    static unsigned long lastLog = 0;
    
    // Atualizar LED sempre
    updateLED();
    
    // Executar rotina baseada no modo
    switch (currentMode) {
        case MODE_SETUP_AP:
            handleSetupMode();
            break;
            
        case MODE_BLE_ONLY:
            handleBLEMode();
            break;
            
        case MODE_WIFI_SYNC:
            handleWiFiMode();
            break;
            
        case MODE_SHUTDOWN:
            handleShutdownMode();
            break;
    }
    
    // Log periódico
    if (millis() - lastLog > config.log_interval_sec * 1000) {
        String modeStr = (currentMode == MODE_BLE_ONLY) ? "BLE" : 
                        (currentMode == MODE_WIFI_SYNC) ? "WiFi" : "Shutdown";
        
        Serial.printf("[%lu] 📊 Heap: %d | Modo: %s | BLE: %s | Bikes: %d | Config: %s\n", 
                     millis()/1000, 
                     ESP.getFreeHeap(),
                     modeStr.c_str(),
                     isBLEReady() ? "OK" : "FAIL",
                     getConnectedBikeCount(),
                     isConfigValid() ? "OK" : "INVALID");
        lastLog = millis();
    }
    
    delay(100);
}