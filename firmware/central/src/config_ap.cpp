#include <ArduinoJson.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include "config_ap.h"
#include "config_credentials.h"
#include "config_manager.h"
#include "constants.h"
#include "endpoints.h"

extern ConfigManager configManager;
extern ConfigCredentials configCredentials;

static WebServer server(80);
static DNSServer dnsServer;
static uint32_t apStartTime = 0;
static bool isInitialConfigMode = false;

void ConfigAP::enter(bool isInitialMode)
{
    isInitialConfigMode = isInitialMode;

    if (isInitialMode)
    {
        Serial.println("🔧 Config inválida, entrando no modo AP");
        Serial.println("📱 Conecte-se ao WiFi: BPR Central (senha: botaprarodar)");
        Serial.println("🌐 Acesse: http://192.168.1.1 para configurar");
    }
    else
    {
        Serial.println("⚠️ Modo AP por falha de sync");
    }

    // Simplified WiFi AP initialization
    Serial.println("🔧 Inicializando WiFi AP...");
    
    WiFi.mode(WIFI_AP);
    
    if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
        Serial.println("❌ Falha ao iniciar AP");
        apStartTime = millis();
        return;
    }
    Serial.printf("AP: %s IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

    // Start DNS server for captive portal
    dnsServer.start(53, "*", WiFi.softAPIP());
    Serial.println("📡 DNS captive portal started");

    setupWebServer();
    server.begin();
    apStartTime = millis();
}

void ConfigAP::update()
{
    dnsServer.processNextRequest();
    server.handleClient();

    // Imprimir status a cada 30 segundos para debug
    static uint32_t lastDebugPrint = 0;
    if (millis() - lastDebugPrint > 30000) {
        Serial.printf("📱 ConfigAP ativo há %lus - Heap: %d\n", 
                      (millis() - apStartTime) / 1000, ESP.getFreeHeap());
        lastDebugPrint = millis();
    }

    if (!isInitialConfigMode)
    {
        static uint32_t lastTimeoutWarning = 0;
        uint32_t elapsed = millis() - apStartTime;
        uint32_t timeoutMs = configManager.getConfig().timeouts.config_ap_min * 60000;
        
        if (millis() - lastTimeoutWarning > 60000)
        { // A cada minuto
            uint32_t remaining = (elapsed < timeoutMs) ? (timeoutMs - elapsed) : 0;
            Serial.printf("⏰ Modo CONFIG_AP (fallback) - Tempo restante: %lu min\n", remaining / 60000);
            lastTimeoutWarning = millis();
        }

        if (elapsed > timeoutMs)
        {
            // Fallback - voltar para operação normal
            Serial.println("⏰ Timeout CONFIG_AP (fallback) - Voltando para BIKE_PAIRING");
            // main.cpp vai detectar e mudar estado
            return;
        }
    }
}

void ConfigAP::exit()
{
    dnsServer.stop();
    server.stop();
    WiFi.softAPdisconnect(true);
    Serial.println("🔚 ConfigAP: Saindo do modo AP");
}

void ConfigAP::printStatus()
{
    Serial.println("📱 Modo Configuração Ativo:");
    Serial.println("   WiFi: BPR Central (senha: botaprarodar)");
    Serial.println("   URL: http://192.168.1.1");
}

bool ConfigAP::tryUpdateWiFiInFirebase()
{
    const CredentialsConfig &creds = configCredentials.getCredentials();

    WiFi.mode(WIFI_STA);
    WiFi.begin(creds.wifi_ssid, creds.wifi_password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30)
    {
        delay(500);
        attempts++;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASSWORD);
        return false;
    }

    HTTPClient http;
    String url = Endpoints::getWiFiConfig();

    DynamicJsonDocument doc(BLE_COMMAND_BUFFER);
    doc["ssid"] = creds.wifi_ssid;
    doc["password"] = creds.wifi_password;

    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    String jsonString;
    serializeJson(doc, jsonString);

    int httpCode = http.PUT(jsonString);
    bool success = (httpCode == HTTP_CODE_OK);

    http.end();

    // Voltar para modo AP com IP customizado
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
    WiFi.softAP(AP_SSID, AP_PASSWORD);

    return success;
}

void ConfigAP::setupWebServer()
{
    server.on("/", HTTP_GET, []()
              {
        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>BPR Central Config</title>";
        html += "<style>body{font-family:Arial;margin:40px;background:#f5f5f5}";
        html += ".container{background:white;padding:30px;border-radius:8px;box-shadow:0 2px 10px rgba(0,0,0,0.1);max-width:500px}";
        html += "h1{color:#2c3e50;margin-bottom:20px}input{width:100%;padding:10px;margin:8px 0;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}";
        html += "button{background:#3498db;color:white;padding:12px 20px;border:none;border-radius:4px;cursor:pointer;width:100%;font-size:16px}";
        html += "button:hover{background:#2980b9}.info{background:#e8f4fd;padding:15px;border-radius:4px;margin-bottom:20px;border-left:4px solid #3498db}";
        html += ".warning{background:#fff3cd;padding:10px;border-radius:4px;margin-top:15px;border-left:4px solid #ffc107}</style></head><body>";
        html += "<div class='container'><h1>🏢 BPR Central - Configuração</h1>";
        html += "<div class='info'><strong>📶 Conecte-se ao WiFi:</strong><br>SSID: BPR Central<br>Senha: botaprarodar<br>Acesse: 192.168.1.1</div>";
        
        // Tabs para alternar entre formulário e JSON
        html += "<div style='margin-bottom:20px'><button onclick='showForm()' id='formBtn' style='margin-right:10px;background:#3498db;color:white;border:none;padding:8px 16px;border-radius:4px;cursor:pointer'>Formulário</button>";
        html += "<button onclick='showJson()' id='jsonBtn' style='background:#95a5a6;color:white;border:none;padding:8px 16px;border-radius:4px;cursor:pointer'>JSON</button></div>";
        
        // Obter configurações atuais para pré-preenchimento
        const CredentialsConfig& currentCreds = configCredentials.getCredentials();
        
        // Formulário tradicional com valores pré-preenchidos
        html += "<div id='formDiv'><form action='/save' method='post'>";
        html += "<label>ID da Base:</label><input name='base_id' value='" + String(currentCreds.base_id) + "' placeholder='Ex: base01, ameciclo, cepas' required><br>";
        html += "<label>WiFi SSID:</label><input name='ssid' value='" + String(currentCreds.wifi_ssid) + "' placeholder='Nome da rede WiFi' required><br>";
        html += "<label>WiFi Senha:</label><input name='pass' type='password' value='" + String(currentCreds.wifi_password) + "' placeholder='Senha do WiFi' required><br>";
        html += "<label>Firebase Database URL:</label><input name='url' value='" + String(currentCreds.firebase_database_url) + "' placeholder='https://projeto.firebaseio.com' required><br>";
        html += "<label>Firebase API Key:</label><input name='key' value='" + String(currentCreds.firebase_api_key) + "' placeholder='AIza...' required><br>";
        html += "<button type='submit'>💾 Salvar Configuração</button></form></div>";
        
        // Gerar JSON atual para pré-preenchimento
        String currentJson = "{\n";
        currentJson += "  \"base_id\": \"" + String(currentCreds.base_id) + "\",\n";
        currentJson += "  \"wifi_ssid\": \"" + String(currentCreds.wifi_ssid) + "\",\n";
        currentJson += "  \"wifi_password\": \"" + String(currentCreds.wifi_password) + "\",\n";
        currentJson += "  \"firebase_database_url\": \"" + String(currentCreds.firebase_database_url) + "\",\n";
        currentJson += "  \"firebase_api_key\": \"" + String(currentCreds.firebase_api_key) + "\"\n";
        currentJson += "}";
        
        // Configuração via JSON com valores pré-preenchidos
        html += "<div id='jsonDiv' style='display:none'><form action='/save-json' method='post'>";
        html += "<label>Cole o JSON de configuração:</label><br>";
        html += "<p><strong>Formato esperado:</strong></p><pre style='background:#f8f9fa;padding:10px;border-radius:4px;font-size:11px'>" + currentJson + "</pre>";
        html += "<textarea name='config_json' rows='10' style='width:100%;font-family:monospace;font-size:12px' required>" + currentJson + "</textarea><br>";
        html += "<button type='submit'>💾 Salvar JSON</button></form></div>";
        
        if (isInitialConfigMode) {
            html += "<div class='warning'>⚠️ A Central reiniciará após salvar. Sem limite de tempo.</div>";
        } else {
            html += "<div class='warning'>⚠️ A Central reiniciará após salvar. Tempo limite: " + String(configManager.getConfig().timeouts.config_ap_min) + " minutos.</div>";
        }
        
        // JavaScript para alternar tabs
        html += "<script>function showForm(){document.getElementById('formDiv').style.display='block';document.getElementById('jsonDiv').style.display='none';document.getElementById('formBtn').style.background='#3498db';document.getElementById('jsonBtn').style.background='#95a5a6';}";
        html += "function showJson(){document.getElementById('formDiv').style.display='none';document.getElementById('jsonDiv').style.display='block';document.getElementById('formBtn').style.background='#95a5a6';document.getElementById('jsonBtn').style.background='#3498db';}</script>";
        html += "</div></body></html>";
        server.send(200, "text/html", html); });

    server.on("/save", HTTP_POST, []()
              {
        CredentialsConfig& creds = configCredentials.getCredentials();
        
        Serial.println("📝 Dados recebidos do formulário:");
        
        if (server.hasArg("base_id")) {
            strcpy(creds.base_id, server.arg("base_id").c_str());
            Serial.printf("   Base ID: %s\n", creds.base_id);
        }
        if (server.hasArg("ssid")) {
            strcpy(creds.wifi_ssid, server.arg("ssid").c_str());
            Serial.printf("   WiFi SSID: %s\n", creds.wifi_ssid);
        }
        if (server.hasArg("pass")) {
            strcpy(creds.wifi_password, server.arg("pass").c_str());
            Serial.printf("   WiFi Password: %s\n", creds.wifi_password);
        }
        if (server.hasArg("url")) {
            strcpy(creds.firebase_database_url, server.arg("url").c_str());
            Serial.printf("   Firebase URL: %s\n", creds.firebase_database_url);
        }
        if (server.hasArg("key")) {
            strcpy(creds.firebase_api_key, server.arg("key").c_str());
            Serial.printf("   Firebase Key: %s\n", creds.firebase_api_key);
        }
        
        // Extrair project_id da URL automaticamente
        String url = creds.firebase_database_url;
        if (url.indexOf("://") > 0) {
            int start = url.indexOf("://") + 3;
            int end = url.indexOf(".", start);
            if (end > start) {
                String projectId = url.substring(start, end);
                strcpy(creds.firebase_project_id, projectId.c_str());
                Serial.printf("   Firebase Project (auto): %s\n", creds.firebase_project_id);
            }
        }
        
        // Set timestamp and first_sync flag
        creds.created_timestamp = millis() / 1000;
        creds.first_sync = true;
        
        Serial.println("💾 Salvando credenciais...");
        
        if (configCredentials.saveCredentials()) {
            Serial.println("✅ Credenciais salvas com sucesso!");
            
            // Tentar atualizar WiFi no Firebase imediatamente
            if (strlen(creds.wifi_ssid) > 0 && strlen(creds.firebase_database_url) > 0) {
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
            html += "<div class='success'><h1>✅ Credenciais Salvas!</h1><p>🔄 A Central está reiniciando...</p>";
            html += "<p>Aguarde alguns segundos e verifique o monitor serial.</p></div></body></html>";
            server.send(200, "text/html", html);
            delay(2000);
            ESP.restart();
        } else {
            Serial.println("❌ Erro ao salvar credenciais!");
            String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Erro</title>";
            html += "<style>body{font-family:Arial;margin:40px;background:#f5f5f5;text-align:center}";
            html += ".error{background:#f8d7da;color:#721c24;padding:20px;border-radius:8px;border:1px solid #f5c6cb}</style></head><body>";
            html += "<div class='error'><h1>❌ Erro ao Salvar</h1><p>Tente novamente ou verifique os dados.</p>";
            html += "<a href='/'>Voltar</a></div></body></html>";
            server.send(500, "text/html", html);
        } });

    server.on("/status", HTTP_GET, []()
              {
        uint32_t elapsed = millis() - apStartTime;
        
        // Usar o mesmo timeout do update() para consistência
        uint32_t timeoutMs = configManager.getConfig().timeouts.config_ap_min * 60000;
        uint32_t remaining = (elapsed < timeoutMs) ? (timeoutMs - elapsed) : 0;
        
        DynamicJsonDocument doc(STATUS_RESPONSE_BUFFER);
        doc["status"] = "config_mode";
        doc["uptime_ms"] = millis();
        doc["config_time_remaining_ms"] = remaining;
        doc["heap_free"] = ESP.getFreeHeap();
        doc["base_id"] = configCredentials.getCredentials().base_id;
        
        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response); });

    server.on("/save-json", HTTP_POST, []()
              {
        if (!server.hasArg("config_json")) {
            server.send(400, "text/html", "<html><body><h1>❌ JSON não fornecido</h1></body></html>");
            return;
        }
        
        String jsonStr = server.arg("config_json");
        Serial.println("📝 JSON recebido via formulário:");
        Serial.println(jsonStr);
        Serial.println("---");
        
        DynamicJsonDocument doc(BLE_COMMAND_BUFFER);
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
        
        CredentialsConfig& creds = configCredentials.getCredentials();
        
        // Processar campos do JSON
        if (doc["base_id"]) {
            strcpy(creds.base_id, doc["base_id"]);
            Serial.printf("   Base ID: %s\n", creds.base_id);
        }
        if (doc["wifi_ssid"]) {
            strcpy(creds.wifi_ssid, doc["wifi_ssid"]);
            Serial.printf("   WiFi SSID: %s\n", creds.wifi_ssid);
        }
        if (doc["wifi_password"]) {
            strcpy(creds.wifi_password, doc["wifi_password"]);
            Serial.printf("   WiFi Password: %s\n", creds.wifi_password);
        }
        if (doc["firebase_database_url"]) {
            strcpy(creds.firebase_database_url, doc["firebase_database_url"]);
            Serial.printf("   Firebase URL: %s\n", creds.firebase_database_url);
        }
        if (doc["firebase_api_key"]) {
            strcpy(creds.firebase_api_key, doc["firebase_api_key"]);
            Serial.printf("   Firebase Key: %s\n", creds.firebase_api_key);
        }
        
        // Extrair project_id da URL automaticamente
        String url = creds.firebase_database_url;
        if (url.indexOf("://") > 0) {
            int start = url.indexOf("://") + 3;
            int end = url.indexOf(".", start);
            if (end > start) {
                String projectId = url.substring(start, end);
                strcpy(creds.firebase_project_id, projectId.c_str());
                Serial.printf("   Firebase Project (auto): %s\n", creds.firebase_project_id);
            }
        }
        
        // Set timestamp and first_sync flag
        creds.created_timestamp = millis() / 1000;
        creds.first_sync = true;
        
        Serial.println("💾 Salvando credenciais via JSON...");
        
        if (configCredentials.saveCredentials()) {
            Serial.println("✅ Credenciais JSON salvas com sucesso!");
            
            // Tentar atualizar WiFi no Firebase imediatamente
            if (strlen(creds.wifi_ssid) > 0 && strlen(creds.firebase_database_url) > 0) {
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
            html += "<div class='success'><h1>✅ JSON Processado!</h1><p>🔄 A Central está reiniciando...</p>";
            html += "<p>Aguarde alguns segundos e verifique o monitor serial.</p></div></body></html>";
            server.send(200, "text/html", html);
            delay(2000);
            ESP.restart();
        } else {
            Serial.println("❌ Erro ao salvar credenciais JSON!");
            String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Erro</title>";
            html += "<style>body{font-family:Arial;margin:40px;background:#f5f5f5;text-align:center}";
            html += ".error{background:#f8d7da;color:#721c24;padding:20px;border-radius:8px;border:1px solid #f5c6cb}</style></head><body>";
            html += "<div class='error'><h1>❌ Erro ao Salvar</h1><p>Tente novamente ou verifique os dados.</p>";
            html += "<a href='/'>Voltar</a></div></body></html>";
            server.send(500, "text/html", html);
        } });
}