#include "buffer_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "constants.h"

BufferManager::BufferManager() : dataCount(0), lastSync(0), connectedBikes(0) {}

void BufferManager::begin() {
    loadState();
}

bool BufferManager::addData(const uint8_t* data, size_t length) {
    if (dataCount >= 50 || length > 256) {
        return false;
    }
    
    // Store data with timestamp
    buffer[dataCount].timestamp = millis();
    buffer[dataCount].size = length;
    memcpy(buffer[dataCount].data, data, length);
    dataCount++;
    
    Serial.printf("📦 Buffer: %d/%d items\n", dataCount, MAX_BUFFER_SIZE);
    
    // Auto-save every 10 items
    if (dataCount % 10 == 0) {
        saveState();
    }
    
    return true;
}

bool BufferManager::needsSync() {
    return dataCount > 40 || // 80% de 50
           (dataCount > 0 && (millis() - lastSync) > SYNC_INTERVAL_DEFAULT);
}

bool BufferManager::getDataForUpload(DynamicJsonDocument& doc) {
    if (dataCount == 0) {
        return false;
    }
    
    doc["timestamp"] = time(nullptr);
    doc["hub_id"] = "hub_default";
    doc["data_count"] = dataCount;
    
    JsonArray dataArray = doc.createNestedArray("data");
    
    for (int i = 0; i < dataCount; i++) {
        JsonObject item = dataArray.createNestedObject();
        item["ts"] = buffer[i].timestamp;
        item["size"] = buffer[i].size;
        
        String hexData = "";
        for (size_t j = 0; j < buffer[i].size; j++) {
            char hex[3];
            sprintf(hex, "%02X", buffer[i].data[j]);
            hexData += hex;
        }
        item["data"] = hexData;
    }
    
    return true;
}

void BufferManager::markAsSent() {
    dataCount = 0;
    lastSync = millis();
    saveState();
    Serial.println("✅ Buffer cleared after successful upload");
}

void BufferManager::addHeartbeat(uint8_t bikes) {
    connectedBikes = bikes;
}

void BufferManager::addConfigLog(const String& bikeId, bool authorized) {
    DynamicJsonDocument logDoc(256);
    logDoc["type"] = "config_log";
    logDoc["timestamp"] = time(nullptr);
    logDoc["bike_id"] = bikeId;
    logDoc["authorized"] = authorized;
    
    String logStr;
    serializeJson(logDoc, logStr);
    
    addData((uint8_t*)logStr.c_str(), logStr.length());
    Serial.printf("📝 Config log added: %s - %s\n", 
                 bikeId.c_str(), authorized ? "AUTHORIZED" : "DENIED");
}

uint8_t BufferManager::getConnectedBikes() {
    return connectedBikes;
}

void BufferManager::saveState() {
    DynamicJsonDocument doc(4096);
    
    doc["data_count"] = dataCount;
    doc["last_sync"] = lastSync;
    doc["connected_bikes"] = connectedBikes;
    
    JsonArray dataArray = doc.createNestedArray("buffer");
    for (int i = 0; i < dataCount; i++) {
        JsonObject item = dataArray.createNestedObject();
        item["ts"] = buffer[i].timestamp;
        item["size"] = buffer[i].size;
        
        String hexData = "";
        for (size_t j = 0; j < buffer[i].size; j++) {
            char hex[3];
            sprintf(hex, "%02X", buffer[i].data[j]);
            hexData += hex;
        }
        item["data"] = hexData;
    }
    
    File file = LittleFS.open(BUFFER_FILE, "w");
    if (file) {
        serializeJson(doc, file);
        file.close();
        Serial.println("💾 Buffer state saved");
    }
}

void BufferManager::loadState() {
    if (!LittleFS.exists(BUFFER_FILE)) {
        Serial.println("No buffer state file found");
        return;
    }
    
    File file = LittleFS.open(BUFFER_FILE, "r");
    if (!file) {
        Serial.println("Failed to open buffer state file");
        return;
    }
    
    DynamicJsonDocument doc(4096);
    if (deserializeJson(doc, file) != DeserializationError::Ok) {
        file.close();
        Serial.println("Failed to parse buffer state");
        return;
    }
    file.close();
    
    dataCount = doc["data_count"] | 0;
    lastSync = doc["last_sync"] | 0;
    connectedBikes = doc["connected_bikes"] | 0;
    
    JsonArray dataArray = doc["buffer"];
    int loadedCount = 0;
    
    for (JsonObject item : dataArray) {
        if (loadedCount >= 50) break;
        
        buffer[loadedCount].timestamp = item["ts"];
        buffer[loadedCount].size = item["size"];
        
        String hexData = item["data"];
        size_t dataSize = hexData.length() / 2;
        
        for (size_t i = 0; i < dataSize && i < 256; i++) {
            String byteString = hexData.substring(i * 2, i * 2 + 2);
            buffer[loadedCount].data[i] = strtol(byteString.c_str(), NULL, 16);
        }
        
        loadedCount++;
    }
    
    dataCount = loadedCount;
    Serial.printf("📥 Buffer state loaded: %d items\n", dataCount);
}