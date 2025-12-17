#include "wifi_sync.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "constants.h"
#include "config_manager.h"
#include "buffer_manager.h"
#include "led_controller.h"
#include "bike_registry.h"
#include "bike_config_manager.h"

extern ConfigManager configManager;
extern BufferManager bufferManager;
extern LEDController ledController;
extern SystemState currentState;
extern bool firstSync;
extern void recordSyncFailure();
extern void recordSyncSuccess();

static uint32_t syncStartTime = 0;

SyncResult WiFiSync::enter() {
    Serial.println("📡 Entering WIFI_SYNC mode");
    syncStartTime = millis();
    ledController.syncPattern();
    
    // Early return se WiFi falhar
    if (!connectWiFi()) {
        Serial.println("❌ WiFi connection failed");
        recordSyncFailure();
        return SyncResult::FAILURE;
    }
    
    // WiFi conectado - executar sync
    syncTime();
    
    bool hubConfigOk = downloadHubConfig();
    bool bikeRegistryOk = downloadBikeRegistry();
    bool bikeConfigsOk = downloadBikeConfigs();
    
    bool wifiConfigOk = firstSync ? uploadWiFiConfig() : true;
    bool registryOk = uploadBikeRegistry();
    bool bufferOk = uploadBufferData();
    bool heartbeatOk = uploadHeartbeat();
    
    bool syncSuccess = hubConfigOk && bikeRegistryOk && bikeConfigsOk && wifiConfigOk && registryOk && bufferOk && heartbeatOk;
    
    // Sempre desconectar WiFi
    WiFi.disconnect(true);
    
    // Early return se sync falhar
    if (!syncSuccess) {
        Serial.println("❌ Sync failed");
        recordSyncFailure();
        return SyncResult::FAILURE;
    }
    
    // Sucesso
    Serial.println("✅ Sync complete");
    recordSyncSuccess();
    return SyncResult::SUCCESS;
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

bool WiFiSync::downloadHubConfig() {
    HTTPClient http;
    const HubConfig& config = configManager.getConfig();
    
    String url = String(config.firebase.database_url) + 
                "/bases/" + config.base_id + "/configs" + ".json?auth=" + 
                config.firebase.api_key;
    
    Serial.printf("🔄 Downloading hub config from Firebase...\n");
    Serial.printf("   Base ID: %s\n", config.base_id);
    
    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        
        if (payload.length() < 100) {
            Serial.println("🚨 Hub config too small - invalid!");
            http.end();
            return false;
        }
        
        DynamicJsonDocument doc(2048);
        
        if (deserializeJson(doc, payload) == DeserializationError::Ok) {
            if (validateFirebaseConfig(doc)) {
                configManager.updateFromFirebase(doc);
                Serial.printf("✅ Hub config downloaded successfully\n");
                Serial.printf("   Sync interval: %d seconds\n", configManager.getConfig().intervals.sync_sec);
                http.end();
                return true;
            } else {
                Serial.println("🚨 Hub config validation failed!");
            }
        } else {
            Serial.println("🚨 Hub config JSON parse failed!");
        }
    } else {
        Serial.printf("🚨 Hub config download failed: HTTP %d\n", httpCode);
    }
    
    http.end();
    return false;
}

bool WiFiSync::downloadBikeConfigs() {
    Serial.println("🔄 Downloading bike configs...");
    
    if (BikeConfigManager::downloadConfigsFromFirebase()) {
        Serial.println("✅ Bike configs downloaded successfully");
        return true;
    } else {
        Serial.println("🚨 Bike configs download failed!");
        return false;
    }
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

bool WiFiSync::uploadBufferData() {
    DynamicJsonDocument doc(4096);
    if (!bufferManager.getDataForUpload(doc)) {
        Serial.println("📝 No buffer data to upload");
        return true; // Não ter dados não é erro
    }
    
    HTTPClient http;
    const HubConfig& config = configManager.getConfig();
    
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
        Serial.printf("📤 Buffer data uploaded: %d bytes\n", jsonString.length());
        Serial.printf("   URL: /bases/%s/data\n", config.base_id);
        http.end();
        return true;
    } else {
        Serial.printf("❌ Buffer upload failed: HTTP %d\n", httpCode);
        Serial.printf("   URL: %s\n", url.c_str());
        http.end();
        return false;
    }
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