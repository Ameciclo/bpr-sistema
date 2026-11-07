#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include "ble_server.h"
#include "config_ap.h"
#include "config_credentials.h"
#include "config_manager.h"
#include "constants.h"
#include "endpoints.h"

extern ConfigManager configManager;
extern ConfigCredentials configCredentials;

// Flag externa para restart seguro
extern bool pendingRestart;
extern uint32_t restartRequestTime;

static WebServer server(80);
static uint32_t apStartTime = 0;
static bool isInitialConfigMode = false;

// F23: cópia com limite — entrada longa é rejeitada, não estoura a struct
static bool safeCopy(char* dst, size_t dstSize, const String& src) {
    if (src.length() >= dstSize) {
        Serial.printf("❌ Campo muito longo (%d >= %d bytes) - rejeitado\n",
                      src.length(), (int)dstSize);
        return false;
    }
    strncpy(dst, src.c_str(), dstSize - 1);
    dst[dstSize - 1] = '\0';
    return true;
}

// F23: escape de HTML para valores pré-preenchidos
static String htmlEscape(const String& value) {
    String out;
    out.reserve(value.length());
    for (unsigned int i = 0; i < value.length(); i++) {
        char c = value.charAt(i);
        switch (c) {
            case '&':  out += "&amp;"; break;
            case '<':  out += "&lt;"; break;
            case '>':  out += "&gt;"; break;
            case '\"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default:   out += c; break;
        }
    }
    return out;
}

// R20: validação de candidata sem tocar o objeto ativo
static bool isCandidateValid(const CredentialsConfig& creds) {
    return strlen(creds.base_id) > 0 &&
           strlen(creds.wifi_ssid) > 0 &&
           strlen(creds.wifi_password) > 0 &&
           strlen(creds.firebase_database_url) > 0 &&
           strlen(creds.firebase_api_key) > 0 &&
           strlen(creds.firebase_project_id) > 0;
}

static void sendErrorPage(const String& title, const String& detail) {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>" + title + "</title>";
    html += "<style>body{font-family:Arial;margin:40px;background:#f5f5f5;text-align:center}";
    html += ".error{background:#f8d7da;color:#721c24;padding:20px;border-radius:8px;border:1px solid #f5c6cb}</style></head><body>";
    html += "<div class='error'><h1>❌ " + title + "</h1><p>" + detail + "</p>";
    html += "<a href='/'>Voltar</a></div></body></html>";
    server.send(400, "text/html", html);
}

static void sendSuccessPage() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Configuração Salva</title>";
    html += "<style>body{font-family:Arial;margin:40px;background:#f5f5f5;text-align:center}";
    html += ".success{background:#d4edda;color:#155724;padding:20px;border-radius:8px;border:1px solid #c3e6cb}</style></head><body>";
    html += "<div class='success'><h1>✅ Credenciais Salvas!</h1><p>🔄 A Central está reiniciando...</p>";
    html += "<p>Aguarde alguns segundos e verifique o monitor serial.</p></div></body></html>";
    server.send(200, "text/html", html);
}

void ConfigAP::enter(bool isInitialMode)
{
    isInitialConfigMode = isInitialMode;

    if (isInitialMode)
    {
        Serial.println("🔧 Config inválida, entrando no modo AP");
    }
    else
    {
        Serial.println("🔧 Modo AP por falha de sync");
    }
    Serial.println("📱 Conecte-se ao WiFi: BPR Central (senha padrão do projeto)");
    Serial.println("🌐 Acesse: http://192.168.4.1 para configurar");

    // §6.5: BLE é PARADO de fato no modo AP (antes só dizia que estava)
    BPRBLEServer::stop();

    Serial.println("🔧 Inicializando WiFi AP...");

    WiFi.mode(WIFI_AP);

    WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);

    if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
        Serial.println("❌ Falha ao iniciar AP");
        apStartTime = millis();
        return;
    }
    Serial.printf("AP: %s IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

    Serial.printf("💾 Free heap: %d bytes\n", ESP.getFreeHeap());

    setupWebServer();
    server.begin();
    apStartTime = millis();
}

void ConfigAP::update()
{
    server.handleClient();

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
        {
            uint32_t remaining = (elapsed < timeoutMs) ? (timeoutMs - elapsed) : 0;
            Serial.printf("⏰ Modo CONFIG_AP (fallback) - Tempo restante: %lu min\n", remaining / 60000);
            lastTimeoutWarning = millis();
        }

        if (elapsed > timeoutMs)
        {
            Serial.println("⏰ Timeout CONFIG_AP (fallback) - Voltando para BIKE_PAIRING");
            return;
        }
    }
}

void ConfigAP::exit()
{
    server.stop();
    WiFi.softAPdisconnect(true);
    Serial.println("🔚 ConfigAP: Saindo do modo AP (BLE será reiniciado no pairing)");
}

void ConfigAP::printStatus()
{
    Serial.println("📱 Modo Configuração Ativo:");
    Serial.println("   WiFi: BPR Central");
    Serial.println("   URL: http://192.168.4.1");
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
        html += "<div class='info'><strong>📶 Conecte-se ao WiFi:</strong><br>SSID: BPR Central<br>Acesse: 192.168.4.1</div>";

        // Tabs para alternar entre formulário e JSON
        html += "<div style='margin-bottom:20px'><button onclick='showForm()' id='formBtn' style='margin-right:10px;background:#3498db;color:white;border:none;padding:8px 16px;border-radius:4px;cursor:pointer'>Formulário</button>";
        html += "<button onclick='showJson()' id='jsonBtn' style='background:#95a5a6;color:white;border:none;padding:8px 16px;border-radius:4px;cursor:pointer'>JSON</button></div>";

        const CredentialsConfig& currentCreds = configCredentials.getCredentials();

        // Formulário tradicional — F23: valores escapados; segredo fica vazio para não vazar no HTML
        html += "<div id='formDiv'><form action='/save' method='post'>";
        html += "<label>ID da Base:</label><input name='base_id' value='" + htmlEscape(String(currentCreds.base_id)) + "' placeholder='Ex: base01, ameciclo, cepas' required><br>";
        html += "<label>WiFi SSID:</label><input name='ssid' value='" + htmlEscape(String(currentCreds.wifi_ssid)) + "' placeholder='Nome da rede WiFi' required><br>";
        html += "<label>WiFi Senha:</label><input name='pass' type='password' placeholder='deixe vazio para manter a salva'><br>";
        html += "<label>Firebase Database URL:</label><input name='url' value='" + htmlEscape(String(currentCreds.firebase_database_url)) + "' placeholder='https://projeto.firebaseio.com' required><br>";
        // F21: rótulo honesto — ?auth= espera token/secret, não Web API Key
        html += "<label>Firebase Auth Token (Database Secret):</label><input name='key' type='password' placeholder='deixe vazio para manter o salvo'><br>";
        html += "<button type='submit'>💾 Salvar Configuração</button></form></div>";

        // F22: JSON atual pré-preenchido na textarea (era construído e descartado)
        String currentJson = "{\n";
        currentJson += "  \"base_id\": \"" + String(currentCreds.base_id) + "\",\n";
        currentJson += "  \"wifi_ssid\": \"" + String(currentCreds.wifi_ssid) + "\",\n";
        currentJson += "  \"wifi_password\": \"\",\n";
        currentJson += "  \"firebase_database_url\": \"" + String(currentCreds.firebase_database_url) + "\",\n";
        currentJson += "  \"firebase_api_key\": \"\"\n";
        currentJson += "}";

        html += "<div id='jsonDiv' style='display:none'><form action='/save-json' method='post'>";
        html += "<label>Cole o JSON de configuração:</label><br>";
        html += "<button type='button' onclick='clearJson()' style='background:#e74c3c;color:white;border:none;padding:5px 10px;border-radius:3px;cursor:pointer;margin-bottom:10px'>🗑️ Limpar</button><br>";
        html += "<textarea name='config_json' id='jsonTextarea' rows='10' style='width:100%;font-family:monospace;font-size:12px' required>";
        html += currentJson;
        html += "</textarea><br>";
        html += "<button type='submit'>💾 Salvar JSON</button></form></div>";

        if (isInitialConfigMode) {
            html += "<div class='warning'>⚠️ A Central reiniciará após salvar. Sem limite de tempo.</div>";
        } else {
            html += "<div class='warning'>⚠️ A Central reiniciará após salvar. Tempo limite: " + String(configManager.getConfig().timeouts.config_ap_min) + " minutos.</div>";
        }

        html += "<script>function showForm(){document.getElementById('formDiv').style.display='block';document.getElementById('jsonDiv').style.display='none';document.getElementById('formBtn').style.background='#3498db';document.getElementById('jsonBtn').style.background='#95a5a6';}";
        html += "function showJson(){document.getElementById('formDiv').style.display='none';document.getElementById('jsonDiv').style.display='block';document.getElementById('formBtn').style.background='#95a5a6';document.getElementById('jsonBtn').style.background='#3498db';}";
        html += "function clearJson(){document.getElementById('jsonTextarea').value='';}</script>";
        html += "</div></body></html>";
        server.send(200, "text/html", html); });

    server.on("/save", HTTP_POST, []()
              {
        // R20: aplica em CÓPIA candidata; credenciais ativas só são tocadas
        // depois de validar E salvar — campo tardio inválido não deixa a RAM
        // parcialmente modificada após um HTTP 400
        CredentialsConfig candidate = configCredentials.getCredentials();
        bool ok = true;

        if (server.hasArg("base_id")) {
            ok = ok && safeCopy(candidate.base_id, sizeof(candidate.base_id), server.arg("base_id"));
        }
        if (server.hasArg("ssid")) {
            ok = ok && safeCopy(candidate.wifi_ssid, sizeof(candidate.wifi_ssid), server.arg("ssid"));
        }
        // F23: vazio mantém o segredo salvo (não vaza no HTML)
        if (server.hasArg("pass") && server.arg("pass").length() > 0) {
            ok = ok && safeCopy(candidate.wifi_password, sizeof(candidate.wifi_password), server.arg("pass"));
        }
        if (server.hasArg("url")) {
            ok = ok && safeCopy(candidate.firebase_database_url, sizeof(candidate.firebase_database_url), server.arg("url"));
        }
        if (server.hasArg("key") && server.arg("key").length() > 0) {
            ok = ok && safeCopy(candidate.firebase_api_key, sizeof(candidate.firebase_api_key), server.arg("key"));
        }

        if (!ok) {
            sendErrorPage("Dados Inválidos", "Um ou mais campos excedem o tamanho permitido.");
            return;
        }

        // Extrair project_id da URL automaticamente
        String url = candidate.firebase_database_url;
        if (url.indexOf("://") > 0) {
            int start = url.indexOf("://") + 3;
            int end = url.indexOf(".", start);
            if (end > start) {
                String projectId = url.substring(start, end);
                safeCopy(candidate.firebase_project_id, sizeof(candidate.firebase_project_id), projectId);
            }
        }

        candidate.created_timestamp = millis() / 1000;
        candidate.first_sync = true;

        // F22: confirmação só depois de validar E salvar
        if (!isCandidateValid(candidate)) {
            sendErrorPage("Credenciais Incompletas", "Verifique base_id, ssid, senha WiFi, URL e token Firebase.");
            return;
        }

        configCredentials.getCredentials() = candidate;
        if (configCredentials.saveCredentials()) {
            Serial.println("✅ Credenciais salvas (segredos não são impressos no serial)");
            sendSuccessPage();

            pendingRestart = true;
            restartRequestTime = millis();
            Serial.println("🔄 Restart agendado - aguardando cleanup...");
        } else {
            sendErrorPage("Erro ao Salvar", "Falha de gravação no LittleFS. Tente novamente.");
        } });

    server.on("/status", HTTP_GET, []()
              {
        uint32_t elapsed = millis() - apStartTime;

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
            sendErrorPage("JSON não fornecido", "Envie o campo config_json.");
            return;
        }

        String jsonStr = server.arg("config_json");
        Serial.println("📝 JSON recebido via formulário (conteúdo não é impresso)");

        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, jsonStr);

        if (error) {
            sendErrorPage("JSON Inválido", String("Erro: ") + error.c_str());
            return;
        }

        // R20: aplica em CÓPIA candidata — objeto ativo intacto até validar E salvar
        CredentialsConfig candidate = configCredentials.getCredentials();
        bool ok = true;

        // F22: schema único — plano E aninhado, incluindo firebase_api_key no nível plano
        if (!doc["base_id"].isNull()) {
            ok = ok && safeCopy(candidate.base_id, sizeof(candidate.base_id), doc["base_id"].as<String>());
        }
        if (!doc["wifi_ssid"].isNull()) {
            ok = ok && safeCopy(candidate.wifi_ssid, sizeof(candidate.wifi_ssid), doc["wifi_ssid"].as<String>());
        }
        if (!doc["wifi_password"].isNull() && doc["wifi_password"].as<String>().length() > 0) {
            ok = ok && safeCopy(candidate.wifi_password, sizeof(candidate.wifi_password), doc["wifi_password"].as<String>());
        }
        if (!doc["firebase_database_url"].isNull()) {
            ok = ok && safeCopy(candidate.firebase_database_url, sizeof(candidate.firebase_database_url), doc["firebase_database_url"].as<String>());
        }
        if (!doc["firebase_api_key"].isNull() && doc["firebase_api_key"].as<String>().length() > 0) {
            ok = ok && safeCopy(candidate.firebase_api_key, sizeof(candidate.firebase_api_key), doc["firebase_api_key"].as<String>());
        }
        if (!doc["firebase_project_id"].isNull()) {
            ok = ok && safeCopy(candidate.firebase_project_id, sizeof(candidate.firebase_project_id), doc["firebase_project_id"].as<String>());
        }

        // Formato aninhado
        if (!doc["wifi"]["ssid"].isNull()) {
            ok = ok && safeCopy(candidate.wifi_ssid, sizeof(candidate.wifi_ssid), doc["wifi"]["ssid"].as<String>());
        }
        if (!doc["wifi"]["password"].isNull() && doc["wifi"]["password"].as<String>().length() > 0) {
            ok = ok && safeCopy(candidate.wifi_password, sizeof(candidate.wifi_password), doc["wifi"]["password"].as<String>());
        }
        if (!doc["firebase"]["database_url"].isNull()) {
            ok = ok && safeCopy(candidate.firebase_database_url, sizeof(candidate.firebase_database_url), doc["firebase"]["database_url"].as<String>());
        }
        if (!doc["firebase"]["api_key"].isNull() && doc["firebase"]["api_key"].as<String>().length() > 0) {
            ok = ok && safeCopy(candidate.firebase_api_key, sizeof(candidate.firebase_api_key), doc["firebase"]["api_key"].as<String>());
        }
        if (!doc["firebase"]["project_id"].isNull()) {
            ok = ok && safeCopy(candidate.firebase_project_id, sizeof(candidate.firebase_project_id), doc["firebase"]["project_id"].as<String>());
        }

        if (!ok) {
            sendErrorPage("Dados Inválidos", "Um ou mais campos excedem o tamanho permitido.");
            return;
        }

        // Extrair project_id da URL automaticamente se não veio no JSON
        String url = candidate.firebase_database_url;
        if (strlen(candidate.firebase_project_id) == 0 && url.indexOf("://") > 0) {
            int start = url.indexOf("://") + 3;
            int end = url.indexOf(".", start);
            if (end > start) {
                safeCopy(candidate.firebase_project_id, sizeof(candidate.firebase_project_id), url.substring(start, end));
            }
        }

        candidate.created_timestamp = millis() / 1000;
        candidate.first_sync = true;

        // F22: validação completa antes de confirmar — não mais HTTP 200 com credencial vazia
        if (!isCandidateValid(candidate)) {
            sendErrorPage("Credenciais Incompletas", "Verifique base_id, ssid, senha WiFi, URL e token Firebase.");
            return;
        }

        configCredentials.getCredentials() = candidate;
        if (configCredentials.saveCredentials()) {
            Serial.println("✅ Credenciais JSON salvas (segredos não são impressos no serial)");
            sendSuccessPage();

            pendingRestart = true;
            restartRequestTime = millis();
            Serial.println("🔄 Restart agendado - aguardando cleanup...");
        } else {
            sendErrorPage("Erro ao Salvar", "Falha de gravação no LittleFS. Tente novamente.");
        } });
}
