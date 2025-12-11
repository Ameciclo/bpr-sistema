#include "firebase_manager.h"
#include "config_manager.h"
#include "ntp_manager.h"
#include "bike_manager.h"
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

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
    
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(30000);
    
    String url = firebaseUrl;
    url.replace("https://", "");
    int slashIndex = url.indexOf('/');
    String host = url.substring(0, slashIndex);
    
    String fullPath = path + ".json";
    
    if (client.connect(host.c_str(), 443)) {
        String request = "PUT " + fullPath + " HTTP/1.1\r\n";
        request += "Host: " + host + "\r\n";
        request += "Content-Type: application/json\r\n";
        request += "Content-Length: " + String(json.length()) + "\r\n";
        request += "Connection: close\r\n\r\n";
        
        client.print(request);
        client.print(json);
        
        String response = "";
        unsigned long start = millis();
        while (client.connected() && millis() - start < 10000) {
            if (client.available()) {
                response = client.readString();
                break;
            }
            delay(10);
        }
        client.stop();
        
        return response.indexOf("200 OK") >= 0;
    }
    
    Serial.println("❌ Falha conexão Firebase");
    return false;
}

bool downloadFromFirebase(String path, String& result) {
    File configFile = LittleFS.open("/config.json", "r");
    if (!configFile) return false;
    
    DynamicJsonDocument doc(512);
    deserializeJson(doc, configFile);
    configFile.close();
    
    String firebaseUrl = doc["firebase"]["database_url"];
    
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(30000);
    
    String url = firebaseUrl;
    url.replace("https://", "");
    int slashIndex = url.indexOf('/');
    String host = url.substring(0, slashIndex);
    
    String fullPath = path + ".json";
    
    if (client.connect(host.c_str(), 443)) {
        String request = "GET " + fullPath + " HTTP/1.1\r\n";
        request += "Host: " + host + "\r\n";
        request += "Connection: close\r\n\r\n";
        
        client.print(request);
        
        String response = "";
        unsigned long start = millis();
        while (client.connected() && millis() - start < 10000) {
            if (client.available()) {
                response = client.readString();
                break;
            }
            delay(10);
        }
        client.stop();
        
        if (response.indexOf("200 OK") >= 0) {
            int bodyStart = response.indexOf("\r\n\r\n");
            if (bodyStart >= 0) {
                result = response.substring(bodyStart + 4);
                return true;
            }
        }
    }
    
    return false;
}

bool sendHeartbeat() {
    unsigned long currentTimestamp = getCurrentTimestamp();
    
    DynamicJsonDocument heartbeat(256);
    heartbeat["timestamp"] = currentTimestamp;
    heartbeat["bikes_connected"] = getConnectedBikeCount();
    heartbeat["heap"] = ESP.getFreeHeap();
    heartbeat["uptime"] = millis() / 1000;
    
    String heartbeatJson;
    serializeJson(heartbeat, heartbeatJson);
    
    CentralConfigCache config = getCentralConfig();
    String heartbeatPath = "/bases/" + String(config.base_id) + "/last_heartbeat";
    
    if (uploadToFirebase(heartbeatPath, heartbeatJson)) {
        Serial.printf("💓 Heartbeat enviado para %s\n", config.base_id);
        return true;
    }
    
    return false;
}

bool uploadPendingData(String& pendingData) {
    if (pendingData.length() == 0) return true;
    
    Serial.println("🔥 Enviando dados...");
    Serial.printf("📦 Tamanho total: %d bytes\n", pendingData.length());
    
    CentralConfigCache config = getCentralConfig();
    if (pendingData.length() > config.firebase_batch_size) {
        Serial.println("📦 Dados grandes - enviando em batches...");
        
        int start = 0;
        int batchNum = 0;
        String currentBatch = "";
        
        while (start < pendingData.length()) {
            int commaPos = pendingData.indexOf(',', start);
            if (commaPos == -1) commaPos = pendingData.length();
            
            String item = pendingData.substring(start, commaPos);
            
            if (currentBatch.length() + item.length() > config.firebase_batch_size && currentBatch.length() > 0) {
                String batchJson = "{\"timestamp\":" + String(getCurrentTimestamp()) + ",\"batch\":" + String(batchNum) + ",\"data\":[" + currentBatch + "]}";
                
                if (uploadToFirebase("/central_data/batch_" + String(batchNum) + "_" + String(millis()), batchJson)) {
                    Serial.printf("✅ Batch %d enviado\n", batchNum);
                } else {
                    Serial.printf("❌ Batch %d falhou\n", batchNum);
                    return false;
                }
                
                currentBatch = "";
                batchNum++;
            }
            
            if (currentBatch.length() > 0) currentBatch += ",";
            currentBatch += item;
            start = commaPos + 1;
        }
        
        if (currentBatch.length() > 0) {
            String batchJson = "{\"timestamp\":" + String(getCurrentTimestamp()) + ",\"batch\":" + String(batchNum) + ",\"data\":[" + currentBatch + "]}";
            
            if (uploadToFirebase("/central_data/batch_" + String(batchNum) + "_" + String(millis()), batchJson)) {
                Serial.printf("✅ Batch final %d enviado\n", batchNum);
            } else {
                return false;
            }
        }
        
    } else {
        String validJson = "{\"timestamp\":" + String(getCurrentTimestamp()) + ",\"data\":[" + pendingData + "]}";
        
        if (!uploadToFirebase("/central_data/" + String(config.base_id) + "/" + String(getCurrentTimestamp()), validJson)) {
            Serial.println("❌ Upload falhou");
            return false;
        }
        
        Serial.println("✅ Upload OK");
    }
    
    pendingData = "";
    return true;
}