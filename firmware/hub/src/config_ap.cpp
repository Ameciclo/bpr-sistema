#include "config_ap.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "constants.h"
#include "config_manager.h"
#include "led_controller.h"
#include "state_machine.h"
#include <HTTPClient.h>

extern ConfigManager configManager;
extern LEDController ledController;
extern StateMachine stateMachine;

static WebServer server(80);
static uint32_t apStartTime = 0;

void ConfigAP::enter() {
    Serial.println("CONFIG_AP mode");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.printf("AP: %s IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
    
    // Configurar callback para conexões
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
            Serial.printf("📱 Dispositivo conectado ao AP: %02X:%02X:%02X:%02X:%02X:%02X\n",
                         info.wifi_ap_staconnected.mac[0], info.wifi_ap_staconnected.mac[1],
                         info.wifi_ap_staconnected.mac[2], info.wifi_ap_staconnected.mac[3],
                         info.wifi_ap_staconnected.mac[4], info.wifi_ap_staconnected.mac[5]);
        } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
            Serial.printf("📵 Dispositivo desconectado do AP: %02X:%02X:%02X:%02X:%02X:%02X\n",
                         info.wifi_ap_stadisconnected.mac[0], info.wifi_ap_stadisconnected.mac[1],
                         info.wifi_ap_stadisconnected.mac[2], info.wifi_ap_stadisconnected.mac[3],
                         info.wifi_ap_stadisconnected.mac[4], info.wifi_ap_stadisconnected.mac[5]);
        }
    });
    
    setupWebServer();
    server.begin();
    apStartTime = millis();
    ledController.configPattern();
}

void ConfigAP::update() {
    server.handleClient();
    
    // Mostrar tempo restante a cada minuto
    static uint32_t lastTimeoutWarning = 0;
    uint32_t elapsed = millis() - apStartTime;
    uint32_t remaining = CONFIG_TIMEOUT_MS - elapsed;
    
    if (millis() - lastTimeoutWarning > 60000) { // A cada minuto
        Serial.printf("⏰ Modo CONFIG_AP - Tempo restante: %lu min\n", remaining / 60000);
        lastTimeoutWarning = millis();
    }
    
    if (elapsed > CONFIG_TIMEOUT_MS) {
        Serial.println("⏰ Timeout do modo CONFIG_AP - Reiniciando...");
        ESP.restart();
    }
}

void ConfigAP::exit() {
    server.stop();
    WiFi.softAPdisconnect(true);
}

bool ConfigAP::tryUpdateWiFiInFirebase() {
    const HubConfig& config = configManager.getConfig();
    
    // Conectar WiFi temporariamente
    WiFi.mode(WIFI_STA);
    WiFi.begin(config.wifi.ssid, config.wifi.password);
    
    // Aguardar conexão (timeout 15s)
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        attempts++;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASSWORD);
        return false;
    }
    
    // Tentar upload WiFi
    HTTPClient http;
    String url = String(config.firebase.database_url) + 
                "/central_configs/" + config.base_id + "/wifi.json?auth=" + 
                config.firebase.api_key;
    
    DynamicJsonDocument doc(256);
    doc["ssid"] = config.wifi.ssid;
    doc["password"] = config.wifi.password;
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    int httpCode = http.PUT(jsonString);
    bool success = (httpCode == HTTP_CODE_OK);
    
    http.end();
    
    // Voltar para modo AP
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    
    return success;
}

void ConfigAP::setupWebServer() {
    server.on("/", HTTP_GET, []() {
        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>BPR Hub Config</title>";
        html += "<style>body{font-family:Arial;margin:40px;background:#f5f5f5}";
        html += ".container{background:white;padding:30px;border-radius:8px;box-shadow:0 2px 10px rgba(0,0,0,0.1);max-width:500px}";
        html += "h1{color:#2c3e50;margin-bottom:20px}input{width:100%;padding:10px;margin:8px 0;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}";
        html += "button{background:#3498db;color:white;padding:12px 20px;border:none;border-radius:4px;cursor:pointer;width:100%;font-size:16px}";
        html += "button:hover{background:#2980b9}.info{background:#e8f4fd;padding:15px;border-radius:4px;margin-bottom:20px;border-left:4px solid #3498db}";
        html += ".warning{background:#fff3cd;padding:10px;border-radius:4px;margin-top:15px;border-left:4px solid #ffc107}</style></head><body>";
        html += "<div class='container'><h1>🏢 BPR Hub - Configuração</h1>";
        html += "<div class='info'><strong>📶 Conecte-se ao WiFi:</strong><br>SSID: BPR_Hub_Config<br>Senha: botaprarodar<br>Acesse: 192.168.4.1</div>";
        
        // Tabs para alternar entre formulário e JSON
        html += "<div style='margin-bottom:20px'><button onclick='showForm()' id='formBtn' style='margin-right:10px;background:#3498db;color:white;border:none;padding:8px 16px;border-radius:4px;cursor:pointer'>Formulário</button>";
        html += "<button onclick='showJson()' id='jsonBtn' style='background:#95a5a6;color:white;border:none;padding:8px 16px;border-radius:4px;cursor:pointer'>JSON</button></div>";
        
        // Obter configurações atuais para pré-preenchimento
        const HubConfig& currentConfig = configManager.getConfig();
        
        // Formulário tradicional com valores pré-preenchidos
        html += "<div id='formDiv'><form action='/save' method='post'>";
        html += "<label>ID da Base:</label><input name='base_id' value='" + String(currentConfig.base_id) + "' placeholder='Ex: base01, ameciclo, cepas' required><br>";
        html += "<label>WiFi SSID:</label><input name='ssid' value='" + String(currentConfig.wifi.ssid) + "' placeholder='Nome da rede WiFi' required><br>";
        html += "<label>WiFi Senha:</label><input name='pass' type='password' value='" + String(currentConfig.wifi.password) + "' placeholder='Senha do WiFi' required><br>";
        html += "<label>Firebase Database URL:</label><input name='url' value='" + String(currentConfig.firebase.database_url) + "' placeholder='https://projeto.firebaseio.com' required><br>";
        html += "<label>Firebase API Key:</label><input name='key' value='" + String(currentConfig.firebase.api_key) + "' placeholder='AIza...' required><br>";
        html += "<button type='submit'>💾 Salvar Configuração</button></form></div>";
        
        // Gerar JSON atual para pré-preenchimento
        String currentJson = "{\n";
        currentJson += "  \"base_id\": \"" + String(currentConfig.base_id) + "\",\n";
        currentJson += "  \"wifi\": {\n";
        currentJson += "    \"ssid\": \"" + String(currentConfig.wifi.ssid) + "\",\n";
        currentJson += "    \"password\": \"" + String(currentConfig.wifi.password) + "\"\n";
        currentJson += "  },\n";
        currentJson += "  \"firebase\": {\n";
        currentJson += "    \"database_url\": \"" + String(currentConfig.firebase.database_url) + "\",\n";
        currentJson += "    \"api_key\": \"" + String(currentConfig.firebase.api_key) + "\"\n";
        currentJson += "  }\n";
        currentJson += "}";
        
        // Configuração via JSON com valores pré-preenchidos
        html += "<div id='jsonDiv' style='display:none'><form action='/save-json' method='post'>";
        html += "<label>Cole o JSON de configuração:</label><br>";
        html += "<textarea name='config_json' rows='15' style='width:100%;font-family:monospace;font-size:12px' required>" + currentJson + "</textarea><br>";
        html += "<button type='submit'>💾 Salvar JSON</button></form></div>";
        
        html += "<div class='warning'>⚠️ O hub reiniciará após salvar. Tempo limite: 15 minutos.</div>";
        
        // JavaScript para alternar tabs
        html += "<script>function showForm(){document.getElementById('formDiv').style.display='block';document.getElementById('jsonDiv').style.display='none';document.getElementById('formBtn').style.background='#3498db';document.getElementById('jsonBtn').style.background='#95a5a6';}";
        html += "function showJson(){document.getElementById('formDiv').style.display='none';document.getElementById('jsonDiv').style.display='block';document.getElementById('formBtn').style.background='#95a5a6';document.getElementById('jsonBtn').style.background='#3498db';}</script>";
        html += "</div></body></html>";
        server.send(200, "text/html", html);
    });
    
    server.on("/save", HTTP_POST, []() {
        HubConfig& config = configManager.getConfig();
        
        Serial.println("📝 Dados recebidos do formulário:");
        
        if (server.hasArg("base_id")) {
            strcpy(config.base_id, server.arg("base_id").c_str());
            Serial.printf("   Base ID: %s\n", config.base_id);
        }
        if (server.hasArg("ssid")) {
            strcpy(config.wifi.ssid, server.arg("ssid").c_str());
            Serial.printf("   WiFi SSID: %s\n", config.wifi.ssid);
        }
        if (server.hasArg("pass")) {
            strcpy(config.wifi.password, server.arg("pass").c_str());
            Serial.printf("   WiFi Password: %s\n", config.wifi.password);
        }
        if (server.hasArg("url")) {
            strcpy(config.firebase.database_url, server.arg("url").c_str());
            Serial.printf("   Firebase URL: %s\n", config.firebase.database_url);
        }
        if (server.hasArg("key")) {
            strcpy(config.firebase.api_key, server.arg("key").c_str());
            Serial.printf("   Firebase Key: %s\n", config.firebase.api_key);
        }
        
        // Extrair project_id da URL automaticamente
        String url = config.firebase.database_url;
        if (url.indexOf("://") > 0) {
            int start = url.indexOf("://") + 3;
            int end = url.indexOf(".", start);
            if (end > start) {
                String projectId = url.substring(start, end);
                strcpy(config.firebase.project_id, projectId.c_str());
                Serial.printf("   Firebase Project (auto): %s\n", config.firebase.project_id);
            }
        }
        
        Serial.println("💾 Salvando configuração...");
        
        if (configManager.saveConfig()) {
            Serial.println("✅ Configuração salva com sucesso!");
            
            // Tentar atualizar WiFi no Firebase imediatamente
            if (strlen(config.wifi.ssid) > 0 && strlen(config.firebase.database_url) > 0) {
                Serial.println("🔄 Tentando atualizar WiFi no Firebase...");
                if (tryUpdateWiFiInFirebase()) {
                    Serial.println("✅ WiFi atualizado no Firebase com sucesso!");
                } else {
                    Serial.println("⚠️ Falha ao atualizar WiFi no Firebase (será tentado no próximo sync)");
                }
            }
            
            String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Configuração Salva</title>";
            html += "<style>body{font-family:Arial;margin:40px;background:#f5f5f5;text-align:center}";
            html += ".success{background:#d4edda;color:#155724;padding:20px;border-radius:8px;border:1px solid #c3e6cb}</style></head><body>";
            html += "<div class='success'><h1>✅ Configuração Salva!</h1><p>🔄 O hub está reiniciando...</p>";
            html += "<p>Aguarde alguns segundos e verifique o monitor serial.</p></div></body></html>";
            server.send(200, "text/html", html);
            delay(2000);
            ESP.restart();
        } else {
            Serial.println("❌ Erro ao salvar configuração!");
            String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Erro</title>";
            html += "<style>body{font-family:Arial;margin:40px;background:#f5f5f5;text-align:center}";
            html += ".error{background:#f8d7da;color:#721c24;padding:20px;border-radius:8px;border:1px solid #f5c6cb}</style></head><body>";
            html += "<div class='error'><h1>❌ Erro ao Salvar</h1><p>Tente novamente ou verifique os dados.</p>";
            html += "<a href='/'>Voltar</a></div></body></html>";
            server.send(500, "text/html", html);
        }
    });
    
    server.on("/status", HTTP_GET, []() {
        uint32_t elapsed = millis() - apStartTime;
        uint32_t remaining = CONFIG_TIMEOUT_MS - elapsed;
        
        DynamicJsonDocument doc(512);
        doc["status"] = "config_mode";
        doc["uptime_ms"] = millis();
        doc["config_time_remaining_ms"] = remaining;
        doc["heap_free"] = ESP.getFreeHeap();
        doc["base_id"] = configManager.getConfig().base_id;
        
        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });
    
    server.on("/save-json", HTTP_POST, []() {
        if (!server.hasArg("config_json")) {
            server.send(400, "text/html", "<html><body><h1>❌ JSON não fornecido</h1></body></html>");
            return;
        }
        
        String jsonStr = server.arg("config_json");
        Serial.println("📝 JSON recebido via formulário:");
        Serial.println(jsonStr);
        Serial.println("---");
        
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, jsonStr);
        
        if (error) {
            Serial.printf("❌ Erro ao parsear JSON: %s\n", error.c_str());
            String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Erro JSON</title>";
            html += "<style>body{font-family:Arial;margin:40px;background:#f5f5f5;text-align:center}";
            html += ".error{background:#f8d7da;color:#721c24;padding:20px;border-radius:8px;border:1px solid #f5c6cb}</style></head><body>";
            html += "<div class='error'><h1>❌ JSON Inválido</h1><p>Erro: " + String(error.c_str()) + "</p>";
            html += "<a href='/'>Voltar</a></div></body></html>";
            server.send(400, "text/html", html);
            return;
        }
        
        HubConfig& config = configManager.getConfig();
        
        // Processar campos do JSON
        if (doc["base_id"]) {
            strcpy(config.base_id, doc["base_id"]);
            Serial.printf("   Base ID: %s\n", config.base_id);
        }
        if (doc["wifi"]["ssid"]) {
            strcpy(config.wifi.ssid, doc["wifi"]["ssid"]);
            Serial.printf("   WiFi SSID: %s\n", config.wifi.ssid);
        }
        if (doc["wifi"]["password"]) {
            strcpy(config.wifi.password, doc["wifi"]["password"]);
            Serial.printf("   WiFi Password: %s\n", config.wifi.password);
        }
        if (doc["firebase"]["database_url"]) {
            strcpy(config.firebase.database_url, doc["firebase"]["database_url"]);
            Serial.printf("   Firebase URL: %s\n", config.firebase.database_url);
        }
        if (doc["firebase"]["api_key"]) {
            strcpy(config.firebase.api_key, doc["firebase"]["api_key"]);
            Serial.printf("   Firebase Key: %s\n", config.firebase.api_key);
        }
        
        // Extrair project_id da URL automaticamente
        String url = config.firebase.database_url;
        if (url.indexOf("://") > 0) {
            int start = url.indexOf("://") + 3;
            int end = url.indexOf(".", start);
            if (end > start) {
                String projectId = url.substring(start, end);
                strcpy(config.firebase.project_id, projectId.c_str());
                Serial.printf("   Firebase Project (auto): %s\n", config.firebase.project_id);
            }
        }
        
        Serial.println("💾 Salvando configuração via JSON...");
        
        if (configManager.saveConfig()) {
            Serial.println("✅ Configuração JSON salva com sucesso!");
            
            // Tentar atualizar WiFi no Firebase imediatamente
            HubConfig& savedConfig = configManager.getConfig();
            if (strlen(savedConfig.wifi.ssid) > 0 && strlen(savedConfig.firebase.database_url) > 0) {
                Serial.println("🔄 Tentando atualizar WiFi no Firebase...");
                if (tryUpdateWiFiInFirebase()) {
                    Serial.println("✅ WiFi atualizado no Firebase com sucesso!");
                } else {
                    Serial.println("⚠️ Falha ao atualizar WiFi no Firebase (será tentado no próximo sync)");
                }
            }
            
            String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>JSON Salvo</title>";
            html += "<style>body{font-family:Arial;margin:40px;background:#f5f5f5;text-align:center}";
            html += ".success{background:#d4edda;color:#155724;padding:20px;border-radius:8px;border:1px solid #c3e6cb}</style></head><body>";
            html += "<div class='success'><h1>✅ JSON Processado!</h1><p>🔄 O hub está reiniciando...</p>";
            html += "<p>Aguarde alguns segundos e verifique o monitor serial.</p></div></body></html>";
            server.send(200, "text/html", html);
            delay(2000);
            ESP.restart();
        } else {
            Serial.println("❌ Erro ao salvar configuração JSON!");
            String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Erro</title>";
            html += "<style>body{font-family:Arial;margin:40px;background:#f5f5f5;text-align:center}";
            html += ".error{background:#f8d7da;color:#721c24;padding:20px;border-radius:8px;border:1px solid #f5c6cb}</style></head><body>";
            html += "<div class='error'><h1>❌ Erro ao Salvar</h1><p>Tente novamente ou verifique os dados.</p>";
            html += "<a href='/'>Voltar</a></div></body></html>";
            server.send(500, "text/html", html);
        }
    });
}