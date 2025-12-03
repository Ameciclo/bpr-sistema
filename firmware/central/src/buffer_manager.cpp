#include "buffer_manager.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "wifi_manager.h"
#include "firebase_sync.h"

static QueueHandle_t bufferQueue;

void initBufferManager() {
    if (!SPIFFS.begin()) {
        Serial.println("SPIFFS mount failed");
        return;
    }
    
    bufferQueue = xQueueCreate(MAX_BUFFER_ENTRIES, sizeof(BufferEntry));
    Serial.println("Buffer manager initialized");
}

void bufferManagerTask(void *parameter) {
    BufferEntry entry;
    
    while (true) {
        // Process buffered entries when WiFi is available
        if (isWiFiConnected()) {
            processBufferedEntries();
        }
        
        // Handle new entries to buffer
        if (xQueueReceive(bufferQueue, &entry, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (isWiFiConnected()) {
                // Send immediately if online
                sendBufferEntry(&entry);
            } else {
                // Store to file if offline
                saveToBuffer(&entry);
            }
        }
    }
}

void addToBuffer(const char* path, const char* data, BufferEntryType type) {
    BufferEntry entry;
    entry.type = type;
    entry.timestamp = millis();
    strncpy(entry.path, path, sizeof(entry.path) - 1);
    strncpy(entry.data, data, sizeof(entry.data) - 1);
    
    if (xQueueSend(bufferQueue, &entry, 0) != pdTRUE) {
        Serial.println("Buffer queue full, dropping entry");
    }
}

void saveToBuffer(BufferEntry* entry) {
    File file = SPIFFS.open(BUFFER_FILE_PATH, "a");
    if (!file) {
        Serial.println("Failed to open buffer file for writing");
        return;
    }
    
    DynamicJsonDocument doc(512);
    doc["type"] = entry->type;
    doc["timestamp"] = entry->timestamp;
    doc["path"] = entry->path;
    doc["data"] = entry->data;
    
    String jsonString;
    serializeJson(doc, jsonString);
    file.println(jsonString);
    file.close();
    
    Serial.println("Entry saved to buffer");
}

void processBufferedEntries() {
    File file = SPIFFS.open(BUFFER_FILE_PATH, "r");
    if (!file) {
        return; // No buffer file exists
    }
    
    String tempPath = BUFFER_FILE_PATH + String(".tmp");
    File tempFile = SPIFFS.open(tempPath, "w");
    
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        
        if (line.length() == 0) continue;
        
        DynamicJsonDocument doc(512);
        if (deserializeJson(doc, line) == DeserializationError::Ok) {
            BufferEntry entry;
            entry.type = (BufferEntryType)doc["type"].as<int>();
            entry.timestamp = doc["timestamp"];
            strncpy(entry.path, doc["path"], sizeof(entry.path) - 1);
            strncpy(entry.data, doc["data"], sizeof(entry.data) - 1);
            
            if (!sendBufferEntry(&entry)) {
                // Failed to send, keep in buffer
                tempFile.println(line);
            }
        }
    }
    
    file.close();
    tempFile.close();
    
    // Replace original with temp file
    SPIFFS.remove(BUFFER_FILE_PATH);
    SPIFFS.rename(tempPath, BUFFER_FILE_PATH);
    
    Serial.println("Processed buffered entries");
}

bool sendBufferEntry(BufferEntry* entry) {
    // This is a simplified implementation
    // In reality, you'd use the appropriate Firebase method based on entry type
    Serial.printf("Sending buffered entry: %s -> %s\n", entry->path, entry->data);
    
    // Simulate success for now
    return true;
}