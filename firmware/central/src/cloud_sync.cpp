#include "cloud_sync.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "constants.h"
#include "config_manager.h"
#include "buffer_manager.h"
#include "led_controller.h"
#include "bike_manager.h"

extern ConfigManager configManager;
extern BufferManager bufferManager;
extern LEDController ledController;
extern SystemState currentState;
extern bool firstSync;
extern void recordSyncFailure();
extern void recordSyncSuccess();

static uint32_t syncStartTime = 0;

SyncResult CloudSync::enter()
{
    Serial.println("📡 Entering CLOUD_SYNC mode");
    syncStartTime = millis();
    ledController.syncPattern();

    // Early return se WiFi falhar
    if (!connectWiFi())
    {
        Serial.println("❌ WiFi connection failed");
        recordSyncFailure();
        return SyncResult::FAILURE;
    }

    // WiFi conectado - executar sync
    syncTime();

    bool centralConfigOk = downloadCentralConfig();
    bool bikeDataOk = downloadBikeData();

    bool wifiConfigOk = firstSync ? uploadWiFiConfig() : true;
    bool bikeUploadOk = uploadBikeData();
    bool bufferOk = uploadBufferData();
    bool heartbeatOk = uploadHeartbeat();

    bool syncSuccess = centralConfigOk && bikeDataOk && wifiConfigOk && bikeUploadOk && bufferOk && heartbeatOk;

    // Sempre desconectar WiFi
    WiFi.disconnect(true);

    // Early return se sync falhar
    if (!syncSuccess)
    {
        Serial.println("❌ Sync failed");
        recordSyncFailure();
        return SyncResult::FAILURE;
    }

    // Sucesso
    Serial.println("✅ Sync complete");
    recordSyncSuccess();
    return SyncResult::SUCCESS;
}

void CloudSync::exit()
{
    WiFi.disconnect(true);
    Serial.println("🔚 Exiting CLOUD_SYNC mode");
}

bool CloudSync::connectWiFi()
{
    const CentralConfig &config = configManager.getConfig();

    WiFi.mode(WIFI_STA);
    WiFi.begin(config.wifi.ssid, config.wifi.password);

    uint32_t startTime = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        if (millis() - startTime > config.timeouts.wifi_sec * 1000)
        {
            return false;
        }
        delay(500);
        Serial.print(".");
    }

    Serial.printf("\n📶 WiFi connected: %s\n", WiFi.localIP().toString().c_str());
    return true;
}

void CloudSync::syncTime()
{
    Serial.printf("⏰ Sincronizando horário com %s (UTC%+d)...\n",
                  NTP_SERVER, TIMEZONE_OFFSET / 3600);

    configTime(TIMEZONE_OFFSET, 0, NTP_SERVER);

    // Aguardar sincronização
    int attempts = 0;
    struct tm timeinfo;
    while (!getLocalTime(&timeinfo) && attempts < 10)
    {
        delay(1000);
        attempts++;
        Serial.print(".");
    }

    if (getLocalTime(&timeinfo))
    {
        char dateStr[64];
        strftime(dateStr, sizeof(dateStr), "%Y-%m-%d %H:%M:%S UTC-3", &timeinfo);
        Serial.printf("\n✅ Horário sincronizado: %s\n", dateStr);
        Serial.printf("   Timestamp: %ld\n", time(nullptr));
    }
    else
    {
        Serial.println("\n❌ Falha na sincronização do horário");
    }
}

bool CloudSync::downloadCentralConfig()
{
    HTTPClient http;

    String url = configManager.getCentralConfigUrl();

    Serial.printf("🔄 Downloading central config from Firebase...\n");
    Serial.printf("   Base ID: %s\n", configManager.getConfig().base_id);

    http.begin(url);
    int httpCode = http.GET();

    // Early return se HTTP falhar
    if (httpCode != HTTP_CODE_OK)
    {
        Serial.printf("🚨 Central config download failed: HTTP %d\n", httpCode);
        http.end();
        return false;
    }

    // HTTP OK - processar resposta
    String json = http.getString();
    http.end();

    // Delegar parsing/validation para ConfigManager
    if (!configManager.updateFromJson(json))
    {
        Serial.println("🚨 Failed to update config from JSON");
        return false;
    }

    // Sucesso
    Serial.printf("✅ Central config downloaded successfully\n");
    Serial.printf("   Sync interval: %d seconds\n", configManager.getConfig().intervals.sync_sec);
    return true;
}

bool CloudSync::downloadBikeData()
{
    Serial.println("🔄 Downloading bike data (registry + configs)...");

    if (BikeManager::downloadFromFirebase())
    {
        Serial.println("✅ Bike data downloaded successfully");
        return true;
    }
    else
    {
        Serial.println("🚨 Bike data download failed!");
        return false;
    }
}

bool CloudSync::uploadBufferData()
{
    DynamicJsonDocument doc(4096);

    // Early return se não há dados
    if (!bufferManager.getDataForUpload(doc))
    {
        Serial.println("📝 No buffer data to upload");
        return true; // Não ter dados não é erro
    }

    HTTPClient http;
    String url = configManager.getBufferDataUrl();

    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    String jsonString;
    serializeJson(doc, jsonString);

    int httpCode = http.PATCH(jsonString);

    // Early return se falhar
    if (httpCode != HTTP_CODE_OK)
    {
        Serial.printf("❌ Buffer upload failed: HTTP %d\n", httpCode);
        Serial.printf("   URL: %s\n", url.c_str());
        http.end();
        return false;
    }

    // Sucesso
    bufferManager.markAsSent();
    Serial.printf("📤 Buffer data uploaded: %d bytes\n", jsonString.length());
    Serial.printf("   URL: /bases/%s/data\n", configManager.getConfig().base_id);
    http.end();
    return true;
}

bool CloudSync::uploadHeartbeat()
{
    HTTPClient http;

    String url = configManager.getHeartbeatUrl();

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

    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    String jsonString;
    serializeJson(doc, jsonString);

    int httpCode = http.PUT(jsonString);

    bool success = (httpCode == HTTP_CODE_OK);

    if (success)
    {
        Serial.printf("💓 Heartbeat: %s | Bikes: %d | Heap: %d\n",
                      dateStr, bufferManager.getConnectedBikes(), ESP.getFreeHeap());
    }
    else
    {
        Serial.printf("❌ Heartbeat falhou: HTTP %d\n", httpCode);
        Serial.printf("   URL: %s\n", url.c_str());
        Serial.printf("   Payload: %s\n", jsonString.c_str());
    }

    http.end();
    return success;
}

bool CloudSync::uploadWiFiConfig()
{
    HTTPClient http;

    String url = configManager.getWiFiConfigUrl();
    const CentralConfig &config = configManager.getConfig();

    DynamicJsonDocument doc(256);
    doc["ssid"] = config.wifi.ssid;
    doc["password"] = config.wifi.password;

    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    String jsonString;
    serializeJson(doc, jsonString);

    int httpCode = http.PUT(jsonString);
    http.end();

    // Early return se falhar
    if (httpCode != HTTP_CODE_OK)
    {
        Serial.printf("❌ Failed to upload WiFi config: HTTP %d\n", httpCode);
        return false;
    }

    // Sucesso
    Serial.printf("📶 WiFi config updated in Firebase: %s\n", config.wifi.ssid);
    return true;
}



bool CloudSync::uploadBikeData()
{
    DynamicJsonDocument doc(4096);

    // Early return se não há atualizações
    if (!BikeManager::uploadToFirebase(doc))
    {
        Serial.println("📝 No bike data updates to send");
        return true;
    }

    HTTPClient http;
    String url = configManager.getBikeRegistryUrl();

    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    String jsonString;
    serializeJson(doc, jsonString);

    int httpCode = http.PATCH(jsonString);
    http.end();

    // Early return se falhar
    if (httpCode != HTTP_CODE_OK)
    {
        Serial.printf("❌ Failed to upload bike data: HTTP %d\n", httpCode);
        return false;
    }

    // Sucesso
    Serial.printf("📤 Bike data uploaded: %d bikes\n", doc.size());
    return true;
}