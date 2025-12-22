#include "buffer_manager.h"
#include <LittleFS.h>

BufferManager::BufferManager(int maxRecords) : bufferCount(0), maxRecords(maxRecords) {
    wifiBuffer = new WiFiRecord[maxRecords];
    memset(wifiBuffer, 0, sizeof(WiFiRecord) * maxRecords);
}

BufferManager::~BufferManager() {
    delete[] wifiBuffer;
}

void BufferManager::addWiFiRecord(const WiFiRecord& record) {
    if (bufferCount < maxRecords) {
        wifiBuffer[bufferCount++] = record;
        Serial.printf("📶 WiFi record added: %s (RSSI: %d) - Buffer: %d/%d\n", 
                      record.ssid, record.rssi, bufferCount, maxRecords);
    } else {
        Serial.println("⚠️ WiFi buffer full - record dropped");
    }
}

void BufferManager::clear() {
    bufferCount = 0;
    memset(wifiBuffer, 0, sizeof(WiFiRecord) * maxRecords);
    Serial.println("🗑️ WiFi buffer cleared");
}

void BufferManager::save() {
    File file = LittleFS.open("/buffer.dat", "w");
    if (file) {
        file.write((uint8_t*)&bufferCount, sizeof(bufferCount));
        file.write((uint8_t*)wifiBuffer, sizeof(WiFiRecord) * bufferCount);
        file.close();
        Serial.printf("💾 Buffer saved: %d records\n", bufferCount);
    } else {
        Serial.println("❌ Failed to save buffer");
    }
}

void BufferManager::load() {
    File file = LittleFS.open("/buffer.dat", "r");
    if (file) {
        file.read((uint8_t*)&bufferCount, sizeof(bufferCount));
        if (bufferCount > maxRecords) bufferCount = maxRecords; // Safety check
        file.read((uint8_t*)wifiBuffer, sizeof(WiFiRecord) * bufferCount);
        file.close();
        LittleFS.remove("/buffer.dat");
        Serial.printf("📂 Buffer loaded: %d records\n", bufferCount);
    }
}

int BufferManager::getCount() const {
    return bufferCount;
}

const WiFiRecord* BufferManager::getRecords() const {
    return wifiBuffer;
}

bool BufferManager::isEmpty() const {
    return bufferCount == 0;
}

bool BufferManager::isFull() const {
    return bufferCount >= maxRecords;
}

void BufferManager::setMaxRecords(int max) {
    if (max != maxRecords) {
        delete[] wifiBuffer;
        maxRecords = max;
        wifiBuffer = new WiFiRecord[maxRecords];
        bufferCount = 0;
        memset(wifiBuffer, 0, sizeof(WiFiRecord) * maxRecords);
        Serial.printf("⚙️ Buffer resized to %d records\n", maxRecords);
    }
}