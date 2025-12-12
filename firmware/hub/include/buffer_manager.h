#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

struct BufferEntry {
    uint32_t timestamp;
    size_t size;
    uint8_t data[256];
};

class BufferManager {
public:
    BufferManager();
    void begin();
    bool addData(const uint8_t* data, size_t length);
    void addHeartbeat(uint8_t bikes);
    bool needsSync();
    void clear();
    size_t getDataSize();
    uint8_t* getData();
    bool getDataForUpload(DynamicJsonDocument& doc);
    void markAsSent();
    uint8_t getConnectedBikes();
    void saveState();
    void loadState();

private:
    BufferEntry buffer[50];  // Buffer estático para evitar alocação dinâmica
    uint16_t dataCount;
    uint32_t lastSync;
    uint8_t connectedBikes;
};