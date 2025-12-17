#include "wifi_sync.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "constants.h"
#include "config_manager.h"
#include "buffer_manager.h"
#include "led_controller.h"
#include "state_machine.h"
#include "bike_registry.h"
#include "bike_config_manager.h"

extern ConfigManager configManager;
extern BufferManager bufferManager;
extern LEDController ledController;
extern StateMachine stateMachine;

static uint32_t syncStartTime = 0;

void WiFiSync::enter() {
    Serial.println("📡 Entering WIFI_SYNC mode");
    syncStartTime = millis();
    ledController.syncPattern();
    
    bool syncSuccess = false;
    
    if (!connectWiFi()) {
        Serial.println("❌ WiFi connection failed");
    } else {
        syncTime();
        if (downloadConfig() && uploadData()) {
            syncSuccess = true;
        }
    }
    
    WiFi.disconnect(true);
    
    if (syncSuccess) {
        Serial.println("✅ Sync complete");
        stateMachine.recordSyncSuccess();
        stateMachine.setFirstSync(false);
        stateMachine.handleEvent(EVENT_SYNC_COMPLETE);
    } else {
        Serial.println("❌ Sync failed");
        stateMachine.recordSyncFailure();
        
        if (stateMachine.isFirstSync()) {
            Serial.println("🚨 ERRO CRÍTICO: Primeiro sync falhou!");
            Serial.println("   - Não foi possível baixar configurações do Firebase");
            Serial.println("   - Sistema não pode funcionar sem config válida");
            Serial.println("   - Retornando ao modo CONFIG_AP para reconfigurar");
            stateMachine.setFirstSync(false);
            stateMachine.setState(STATE_CONFIG_AP);
        } else {
            Serial.println("⚠️ Sync falhou - continuando com última config válida");
            stateMachine.handleEvent(EVENT_SYNC_COMPLETE);
        }
    }
}

void WiFiSync::update() {
    // Timeout check
    if (millis() - syncStartTime > configManager.getConfig().timeouts.wifi_sec * 3000) {
        Serial.println("⏰ Sync timeout");
        WiFi.disconnect(true);
        stateMachine.handleEvent(EVENT_SYNC_COMPLETE);
    }
}

void WiFiSync::exit() {
    WiFi.disconnect(true);
    Serial.println("🔚 Exiting WIFI_SYNC mode");
}

bool WiFiSync::connectWiFi() {
    const HubConfig& config = configManager.getConfig();
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(config.wifi.ssid, config.wifi.password);
    
    uint32_t startTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startTime > config.timeouts.wifi_sec * 1000) {
            return false;
        }
        delay(500);
        Serial.print(".");
    }
    
    Serial.printf("\n📶 WiFi connected: %s\n", WiFi.localIP().toString().c_str());
    return true;
}

void WiFiSync::syncTime() {
    Serial.printf("⏰ Sincronizando horário com %s (UTC%+d)...\n", 
                 NTP_SERVER, TIMEZONE_OFFSET / 3600);
    
    configTime(TIMEZONE_OFFSET, 0, NTP_SERVER);
    
    // Aguardar sincronização
    int attempts = 0;
    struct tm timeinfo;
    while (!getLocalTime(&timeinfo) && attempts < 10) {
        delay(1000);
        attempts++;
        Serial.print(".");
    }
    
    if (getLocalTime(&timeinfo)) {
        char dateStr[64];
        strftime(dateStr, sizeof(dateStr), "%Y-%m-%d %H:%M:%S UTC-3", &timeinfo);
        Serial.printf("\n✅ Horário sincronizado: %s\n", dateStr);
        Serial.printf("   Timestamp: %ld\n", time(nullptr));
    } else {
        Serial.println("\n❌ Falha na sincronização do horário");
    }
}

bool WiFiSync::downloadConfig() {
    HTTPClient http;
    const HubConfig& config = configManager.getConfig();
    
    String url = String(config.firebase.database_url) + 
                "/bases/" + config.base_id + "/configs" + ".json?auth=" + 
                config.firebase.api_key;
    
    Serial.printf("🔄 Baixando config obrigatória do Firebase...\n");
    Serial.printf("   Base ID: %s\n", config.base_id);
    Serial.printf("   URL: /bases/%s\n", config.base_id + "/configs");
    
    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        
        Serial.printf("⚙️ JSON baixado do Firebase (%d bytes):\n", payload.length());
        Serial.println(payload);
        Serial.println("---");
        
        // Verificar se não é muito pequeno (provavelmente erro)
        if (payload.length() < 100) {
            Serial.println("🚨 ERRO CRÍTICO: JSON muito pequeno - config inválida!");
            Serial.printf("   Tamanho: %d bytes (mínimo: 100)\n", payload.length());
            Serial.println("   Verifique se a config existe no Firebase");
            http.end();
            return false;
        }
        
        // Baixar também o registro de bikes e configs
        downloadBikeRegistry();
        BikeConfigManager::downloadConfigsFromFirebase();
        
        DynamicJsonDocument doc(2048);
        
        if (deserializeJson(doc, payload) == DeserializationError::Ok) {
            // Validar se JSON tem campos obrigatórios
            if (validateFirebaseConfig(doc)) {
                configManager.updateFromFirebase(doc);
                Serial.printf("✅ Config obrigatória baixada e aplicada com sucesso!\n");
                Serial.printf("   Sync interval: %d segundos\n", configManager.getConfig().intervals.sync_sec);
                Serial.printf("   Max bikes: %d\n", configManager.getConfig().limits.max_bikes);
                http.end();
                return true;
            } else {
                Serial.println("🚨 ERRO CRÍTICO: Config incompleta no Firebase!");
                Serial.println("   Campos obrigatórios ausentes - sistema não pode funcionar");
            }
        } else {
            Serial.println("🚨 ERRO CRÍTICO: JSON inválido no Firebase!");
            Serial.println("   Não foi possível parsear a configuração");
        }
    } else if (httpCode == 404) {
        Serial.printf("🚨 ERRO CRÍTICO: Config não encontrada! HTTP 404\n");
        Serial.printf("   Verifique se existe: /bases/%s.json\n", config.base_id + "/configs");
        Serial.printf("   Base ID configurado: '%s'\n", config.base_id);
    } else {
        Serial.printf("🚨 ERRO CRÍTICO: Falha na conexão Firebase! HTTP %d\n", httpCode);
        Serial.printf("   URL: %s\n", url.c_str());
        Serial.println("   Verifique: internet, Firebase URL, API key");
    }
    
    http.end();
    return false;
}

bool WiFiSync::validateFirebaseConfig(const DynamicJsonDocument& doc) {
    Serial.println("🔍 Validando campos obrigatórios da config Firebase...");
    
    // Lista de campos obrigatórios
    struct RequiredField {
        const char* path;
        const char* description;
    };
    
    RequiredField required[] = {
        {"intervals.sync_sec", "Intervalo de sincronização"},
        {"timeouts.wifi_sec", "Timeout de WiFi"},
        {"led.ble_ready_ms", "Padrão LED BLE"},
        {"limits.max_bikes", "Máximo de bikes"},
        {"fallback.max_failures", "Máximo de falhas"}
    };
    
    bool valid = true;
    
    for (auto& field : required) {
        bool exists = false;
        
        if (strcmp(field.path, "intervals.sync_sec") == 0) exists = doc["intervals"]["sync_sec"];
        else if (strcmp(field.path, "timeouts.wifi_sec") == 0) exists = doc["timeouts"]["wifi_sec"];
        else if (strcmp(field.path, "led.ble_ready_ms") == 0) exists = doc["led"]["ble_ready_ms"];
        else if (strcmp(field.path, "limits.max_bikes") == 0) exists = doc["limits"]["max_bikes"];
        else if (strcmp(field.path, "fallback.max_failures") == 0) exists = doc["fallback"]["max_failures"];
        
        if (exists) {
            Serial.printf("   ✅ %s: OK\n", field.description);
        } else {
            Serial.printf("   ❌ %s: AUSENTE (%s)\n", field.description, field.path);
            valid = false;
        }
    }
    
    if (valid) {
        Serial.println("✅ Validação completa - config Firebase válida!");
    } else {
        Serial.println("🚨 VALIDAÇÃO FALHOU - config Firebase incompleta!");
        Serial.println("   Sistema NÃO PODE funcionar sem esses campos");
        Serial.println("   Corrija a config no Firebase antes de continuar");
    }
    
    return valid;
}

bool WiFiSync::uploadData() {
    HTTPClient http;
    const HubConfig& config = configManager.getConfig();
    
    // Se é primeira sync, atualizar WiFi no Firebase
    if (stateMachine.isFirstSync()) {
        uploadWiFiConfig();
    }
    
    // Upload bike registry updates
    uploadBikeRegistry();
    
    // Upload buffered data
    DynamicJsonDocument doc(4096);
    if (bufferManager.getDataForUpload(doc)) {
        String url = String(config.firebase.database_url) + 
                    "/bases/" + config.base_id + "/data.json?auth=" + 
                    config.firebase.api_key;
        
        http.begin(url);
        http.addHeader("Content-Type", "application/json");
        
        String jsonString;
        serializeJson(doc, jsonString);
        
        int httpCode = http.PATCH(jsonString);
        
        if (httpCode == HTTP_CODE_OK) {
            bufferManager.markAsSent();
            Serial.printf("📤 Dados enviados: %d bytes\n", jsonString.length());
            Serial.printf("   URL: /bases/%s/data\n", config.base_id);
            Serial.printf("   Payload: %s\n", jsonString.c_str());
        } else {
            Serial.printf("❌ Upload falhou: HTTP %d\n", httpCode);
            Serial.printf("   URL: %s\n", url.c_str());
            Serial.printf("   Payload: %s\n", jsonString.c_str());
        }
        
        http.end();
    }
    
    // Upload bike config logs
    uploadBikeConfigLogs();
    
    // Upload heartbeat
    return uploadHeartbeat();
}

bool WiFiSync::uploadHeartbeat() {
    HTTPClient http;
    const HubConfig& config = configManager.getConfig();
    
    String url = String(config.firebase.database_url) + 
                "/bases/" + config.base_id + "/last_heartbeat.json?auth=" + 
                config.firebase.api_key;
    
    // Obter timestamp e formato legível
    time_t now = time(nullptr);
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    
    char dateStr[64];
    strftime(dateStr, sizeof(dateStr), "%Y-%m-%d %H:%M:%S UTC-3", &timeinfo);
    
    DynamicJsonDocument doc(512);
    doc["timestamp"] = now;
    doc["timestamp_human"] = dateStr;
    doc["bikes_connected"] = bufferManager.getConnectedBikes();
    doc["heap"] = ESP.getFreeHeap();
    doc["uptime"] = millis() / 1000;
    // Removidas redundâncias: base_id (já no path), wifi_ssid e sync_interval_sec (não mudam)
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    int httpCode = http.PUT(jsonString);
    
    bool success = (httpCode == HTTP_CODE_OK);
    
    if (success) {
        Serial.printf("💓 Heartbeat: %s | Bikes: %d | Heap: %d\n", 
                     dateStr, bufferManager.getConnectedBikes(), ESP.getFreeHeap());
    } else {
        Serial.printf("❌ Heartbeat falhou: HTTP %d\n", httpCode);
        Serial.printf("   URL: %s\n", url.c_str());
        Serial.printf("   Payload: %s\n", jsonString.c_str());
    }
    
    http.end();
    return success;
}

bool WiFiSync::uploadWiFiConfig() {
    HTTPClient http;
    const HubConfig& config = configManager.getConfig();
    
    String url = String(config.firebase.database_url) + 
                "/bases/" + config.base_id + "/configs/wifi.json?auth=" + 
                config.firebase.api_key;
    
    DynamicJsonDocument doc(256);
    doc["ssid"] = config.wifi.ssid;
    doc["password"] = config.wifi.password;
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    int httpCode = http.PUT(jsonString);
    
    if (httpCode == HTTP_CODE_OK) {
        Serial.printf("📶 WiFi atualizado no Firebase: %s\n", config.wifi.ssid);
        return true;
    } else {
        Serial.printf("❌ Falha ao atualizar WiFi: HTTP %d\n", httpCode);
        return false;
    }
    
    http.end();
}

bool WiFiSync::downloadBikeRegistry() {
    HTTPClient http;
    const HubConfig& config = configManager.getConfig();
    
    String url = String(config.firebase.database_url) + 
                "/bases/" + config.base_id + "/bikes.json?auth=" + 
                config.firebase.api_key;
    
    Serial.printf("📝 Baixando registro de bikes...\n");
    
    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        
        if (payload == "null" || payload.length() < 10) {
            Serial.println("📝 Nenhuma bike registrada ainda");
            http.end();
            return true;
        }
        
        DynamicJsonDocument doc(2048);
        if (deserializeJson(doc, payload) == DeserializationError::Ok) {
            BikeRegistry::updateFromFirebase(doc);
            Serial.printf("✅ Registro de bikes atualizado\n");
            http.end();
            return true;
        } else {
            Serial.println("❌ Erro ao parsear registro de bikes");
        }
    } else {
        Serial.printf("⚠️ Falha ao baixar bikes: HTTP %d\n", httpCode);
    }
    
    http.end();
    return false;
}

bool WiFiSync::uploadBikeRegistry() {
    HTTPClient http;
    const HubConfig& config = configManager.getConfig();
    
    DynamicJsonDocument doc(2048);
    if (!BikeRegistry::getRegistryForUpload(doc)) {
        Serial.println("📝 Nenhuma atualização de bike para enviar");
        return true;
    }
    
    String url = String(config.firebase.database_url) + 
                "/bases/" + config.base_id + "/bikes.json?auth=" + 
                config.firebase.api_key;
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    int httpCode = http.PATCH(jsonString);
    
    if (httpCode == HTTP_CODE_OK) {
        Serial.printf("📤 Registro de bikes enviado: %d bikes\n", doc.size());
        return true;
    } else {
        Serial.printf("❌ Falha ao enviar registro: HTTP %d\n", httpCode);
        return false;
    }
    
    http.end();
}

bool WiFiSync::uploadBikeConfigLogs() {
    // This function would extract config logs from buffer and upload them
    // to /bike_config_logs/{hub_id}/ in Firebase
    // For now, config logs are included in the general data upload
    return true;
}