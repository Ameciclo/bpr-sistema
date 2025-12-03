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

// Estados da central
enum CentralMode {
    MODE_BLE_ONLY,     // Só BLE ativo (padrão)
    MODE_WIFI_SYNC,    // WiFi + Firebase temporário
    MODE_SHUTDOWN      // Desligando WiFi
};

CentralMode currentMode = MODE_BLE_ONLY;
String pendingData = "";
unsigned long lastSync = 0;
unsigned long modeStart = 0;

// NTP + Correção temporal
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -3 * 3600, 60000);
bool ntpSynced = false;
unsigned long ntpEpoch = 0;
unsigned long ntpMillisBase = 0;

// Função para detectar e corrigir timestamps
unsigned long correctTimestamp(unsigned long bikeTimestamp, unsigned long bikeMillis) {
    // Se bike tem NTP válido (> 1600000000 = após 2020)
    if (bikeTimestamp > 1600000000) {
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
    File config = LittleFS.open("/config.json", "r");
    if (!config) {
        Serial.println("❌ Config não encontrado");
        return false;
    }
    
    DynamicJsonDocument doc(512);
    deserializeJson(doc, config);
    config.close();
    
    String firebaseUrl = doc["firebase"]["database_url"];
    Serial.printf("🔗 URL: %s\n", firebaseUrl.c_str());
    
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(10000);
    
    // Parse URL
    String url = firebaseUrl;
    url.replace("https://", "");
    int slashIndex = url.indexOf('/');
    String host = url.substring(0, slashIndex);
    
    Serial.printf("🌐 Host: %s\n", host.c_str());
    Serial.printf("📄 Path: %s\n", path.c_str());
    Serial.printf("📦 JSON size: %d\n", json.length());
    
    String fullPath = path + ".json";
    
    if (client.connect(host.c_str(), 443)) {
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
        while (client.connected() && millis() - start < 5000) {
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
    }
    
    Serial.println("✅ Central em modo BLE");
    Serial.println("📶 WiFi será ativado quando necessário");
}

void handleBLEMode() {
    // Processar configurações pendentes
    processPendingConfigs();
    
    // Limpeza periódica de conexões antigas
    static unsigned long lastCleanup = 0;
    if (millis() - lastCleanup > 60000) { // 1 minuto
        cleanupOldConnections();
        lastCleanup = millis();
    }
    
    // Verificar se precisa sincronizar
    bool needsSync = false;
    
    // Dados pendentes
    if (pendingData.length() > 0) needsSync = true;
    
    // Timeout de sync (5min)
    if (millis() - lastSync > 300000) needsSync = true;
    
    // Config inválida (forçar download)
    if (!isConfigValid()) needsSync = true;
    
    if (needsSync) {
        Serial.println("📶 Ativando WiFi para sync...");
        currentMode = MODE_WIFI_SYNC;
        modeStart = millis();
    }
}

void handleWiFiMode() {
    static bool wifiConnected = false;
    
    // Conectar WiFi se não conectado
    if (!wifiConnected) {
        File config = LittleFS.open("/config.json", "r");
        if (config) {
            DynamicJsonDocument doc(512);
            deserializeJson(doc, config);
            config.close();
            
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
            timeClient.begin();
            if (timeClient.update()) {
                ntpSynced = true;
                ntpEpoch = timeClient.getEpochTime();
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
        
        // Enviar dados pendentes para Firebase (HTTPS direto)
        if (pendingData.length() > 0) {
            Serial.println("🔥 Enviando dados...");
            Serial.printf("📦 Tamanho total: %d bytes\n", pendingData.length());
            
            // Verificar se precisa dividir em batches
            if (pendingData.length() > 8000) { // Limite 8KB por batch
                Serial.println("📦 Dados grandes - enviando em batches...");
                
                // Dividir por vírgulas (cada item JSON)
                int start = 0;
                int batchNum = 0;
                String currentBatch = "";
                
                while (start < pendingData.length()) {
                    int commaPos = pendingData.indexOf(',', start);
                    if (commaPos == -1) commaPos = pendingData.length();
                    
                    String item = pendingData.substring(start, commaPos);
                    
                    // Se adicionar este item ultrapassar 8KB, enviar batch atual
                    if (currentBatch.length() + item.length() > 8000 && currentBatch.length() > 0) {
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
                
                if (uploadToFirebase("/central_data/" + String(timestamp), validJson)) {
                    Serial.println("✅ Upload OK");
                    pendingData = ""; // Limpar após envio
                } else {
                    Serial.println("❌ Upload falhou");
                }
            }
        }
        
        lastSync = millis();
        currentMode = MODE_SHUTDOWN;
        
    } else if (millis() - modeStart > 30000) { // Timeout 30s
        Serial.println("⚠️ WiFi timeout");
        currentMode = MODE_SHUTDOWN;
    }
}

void handleShutdownMode() {
    Serial.println("🔴 Desligando WiFi...");
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    
    currentMode = MODE_BLE_ONLY;
    Serial.println("✅ Voltando ao modo BLE");
}

void loop() {
    static unsigned long lastLog = 0;
    
    // Executar rotina baseada no modo
    switch (currentMode) {
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
    if (millis() - lastLog > 15000) {
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