#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

struct DataItem {
    String bikeId;
    uint32_t timestamp;
    size_t size;
    uint8_t data[256];
    uint32_t crc32;
    bool uploaded;
    bool confirmed;
    bool compressed;
};

class BufferManager {
public:
    BufferManager();
    void begin();
    
    // Dados coletados
    bool addData(const String& bikeId, const uint8_t* data, size_t length);
    bool addBikeData(const String& bikeId, const String& jsonData);
    bool needsSync();
    bool isCriticallyFull();
    bool getDataForUpload(DynamicJsonDocument& doc);
    void markAsConfirmed();
    void rollbackUpload();
    
    // Status
    int getDataCount();
    int getPendingCount();
    void printStorageInfo();
    bool hasEnoughSpace();

private:
    DataItem buffer[50]; // Tamanho máximo, controlado por config
    uint16_t dataCount;
    uint32_t lastSync;
    
    // Persistência
    void loadBuffer();
    void saveBuffer();
    void createBackup();
    void cleanupOldBackups();
    void printFileSize(const String& filePath);
};