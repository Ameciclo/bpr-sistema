#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

struct DataItem {
    String bikeId;
    uint32_t timestamp;
    size_t size;
    uint8_t data[128]; // Reduced from 256 to save memory
    uint32_t crc32;
    bool uploaded;
    bool confirmed;
};

struct BikeBuffer {
    DataItem buffer[10]; // Reduced from 50 to save stack
    uint16_t dataCount;
};

class BufferManager {
public:
    BufferManager();
    
    // Inicialização dinâmica baseada no heap
    bool beginWithAvailableHeap();
    uint32_t calculateCapacity();
    void cleanup();
    
    // Métodos legados
    void begin();
    bool addData(const String& bikeId, const uint8_t* data, size_t length);
    bool addBikeData(const String& bikeId, const String& jsonData);
    bool addConfigData(const String& configType, const String& jsonData);
    bool getDataForUpload(DynamicJsonDocument& doc);
    void markAsConfirmed();
    void rollbackUpload();
    
    // Status
    int getDataCount();
    int getTotalDataCount();
    int getPendingCount();
    bool isFull();
    bool hasData();
    void printStorageInfo();
    bool hasEnoughSpace();

private:
    uint32_t maxCapacity;
    uint32_t currentUsage;
    bool initialized;
    uint32_t lastSync;
    
    // Gerenciamento de buffers individuais
    String getBikeBufferPath(const String& bikeId);
    bool loadBikeBuffer(const String& bikeId, BikeBuffer& bikeBuffer);
    bool saveBikeBuffer(const String& bikeId, const BikeBuffer& bikeBuffer);
    
    // Persistência (métodos de compatibilidade)
    void loadAllBuffers();
    void saveBuffer();
    void createBackup();
    void cleanupOldBackups();
    void printFileSize(const String& filePath);
};