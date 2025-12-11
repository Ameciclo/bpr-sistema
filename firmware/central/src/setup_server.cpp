#include "setup_server.h"
#include "config_manager.h"
#include "led_controller.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// Setup Server Configuration
#define SETUP_SERVER_PORT 80
#define SETUP_AP_PASSWORD "botaprarodar"
#define SETUP_AP_PREFIX "BPR_Setup_"
#define SETUP_SERVER_IP "192.168.4.1"
#define SETUP_CONFIG_FILE "/config.json"
#define SETUP_CONFIG_SIZE 512
#define SETUP_RESTART_DELAY 2000
#define FIREBASE_DEFAULT_URL "https://botaprarodar-routes-default-rtdb.firebaseio.com"
#define CENTRAL_NAME_PREFIX "BPR_"

WebServer setupServer(SETUP_SERVER_PORT);
bool setupComplete = false;

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
            <input type="url" name="firebase_url" value=")" FIREBASE_DEFAULT_URL R"(" required>
            
            <label>Firebase API Key:</label>
            <input type="text" name="firebase_api_key" placeholder="AIza..." required>
            
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
        
        DynamicJsonDocument basicConfig(SETUP_CONFIG_SIZE);
        basicConfig["base_id"] = baseId;
        basicConfig["central_name"] = CENTRAL_NAME_PREFIX + baseId;
        basicConfig["wifi"]["ssid"] = wifiSSID;
        basicConfig["wifi"]["password"] = wifiPassword;
        basicConfig["firebase"]["database_url"] = firebaseUrl;
        basicConfig["firebase"]["api_key"] = firebaseApiKey;
        
        String configJson;
        serializeJson(basicConfig, configJson);
        
        File configFile = LittleFS.open(SETUP_CONFIG_FILE, "w");
        if (configFile) {
            configFile.print(configJson);
            configFile.close();
            
            // Log dos dados salvos
            Serial.println("💾 CONFIGURAÇÃO SALVA:");
            Serial.printf("   🆔 Base ID: %s\n", baseId.c_str());
            Serial.printf("   🏢 Nome: %s\n", baseName.c_str());
            Serial.printf("   📶 WiFi: %s\n", wifiSSID.c_str());
            Serial.printf("   🔥 Firebase: %s\n", firebaseUrl.c_str());
            Serial.printf("   📁 Arquivo: %s (%d bytes)\n", SETUP_CONFIG_FILE, configJson.length());
            
            // TODO: Save baseId to config
            // TODO: Save central_name to config
            
            String response = R"(
<!DOCTYPE html>
<html>
<head><title>Setup Completo</title><meta charset="utf-8"></head>
<body style="font-family: Arial; margin: 40px; text-align: center;">
    <h1>✅ Configuração Salva!</h1>
    <p>A central irá reiniciar e conectar ao WiFi.</p>
    <p>Base: <strong>)" + baseId + R"(</strong></p>
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
    Serial.println("🌐 Servidor web iniciado em http://" SETUP_SERVER_IP);
}

void startSetupAP() {
    Serial.println("🔧 Iniciando modo setup...");
    
    String apName = SETUP_AP_PREFIX + WiFi.macAddress().substring(9);
    apName.replace(":", "");
    
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apName.c_str(), SETUP_AP_PASSWORD);
    
    Serial.printf("📶 AP criado: %s\n", apName.c_str());
    Serial.println("🔑 Senha: " SETUP_AP_PASSWORD);
    Serial.println("🌐 Acesse: http://" SETUP_SERVER_IP);
    
    setLEDPattern(LED_SETUP_MODE);
    setupWebServer();
}

void handleSetupMode() {
    setupServer.handleClient();
    
    // Log de conexões no AP
    static int lastStations = -1;
    int currentStations = WiFi.softAPgetStationNum();
    if (currentStations != lastStations) {
        if (currentStations > 0) {
            Serial.printf("📱 Cliente conectado no AP! Total: %d\n", currentStations);
        } else {
            Serial.println("📱 Cliente desconectado do AP");
        }
        lastStations = currentStations;
    }
    
    if (setupComplete) {
        Serial.println("✅ Setup completo - reiniciando...");
        delay(SETUP_RESTART_DELAY);
        ESP.restart();
    }
}