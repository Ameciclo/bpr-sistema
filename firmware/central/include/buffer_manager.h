#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include "constants.h"

#define DATA_ITEM_MAX_SIZE 512

struct DataItem {
    String bikeId;
    uint32_t timestamp;
    size_t size;
    uint8_t data[DATA_ITEM_MAX_SIZE];
    uint32_t crc32;
    uint32_t session_id;
    uint16_t session_salt;
    uint16_t seq;
    bool uploaded;
    bool confirmed;
};

struct BikeBuffer {
    DataItem buffer[MAX_ITEMS_PER_BIKE];
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
    bool addData(const String& bikeId, const uint8_t* data, size_t length,
                 uint32_t sessionId = 0, uint16_t sessionSalt = 0, uint16_t seq = 0);
    bool addBikeData(const String& bikeId, const String& jsonData);
    bool addConfigData(const String& configType, const String& jsonData);

    // Upload (F18/F19): um lote medido por chamada; retorna nº de itens incluídos
    int getDataForUpload(DynamicJsonDocument& doc, int maxItems);
    void markAsConfirmed();
    void rollbackUpload();

    // Dedupe/completude de sessão (F04/F05)
    bool hasCompleteSession(const String& bikeId, uint32_t sessionId, uint16_t salt, uint16_t totalFrags);

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

    // F15: caminho canônico por bike (/buffer/{bikeId}.bin)
    String getBikeBufferPath(const String& bikeId);
    std::vector<String> listBikeIds();
    bool loadBikeBuffer(const String& bikeId, BikeBuffer& bikeBuffer);
    bool saveBikeBuffer(const String& bikeId, const BikeBuffer& bikeBuffer);

    // Persistência (métodos de compatibilidade)
    void loadAllBuffers();
    void saveBuffer();
    void createBackup();
    void cleanupOldBackups();
    void printFileSize(const String& filePath);
    int getTotalDataCountUnlocked(); // chamadores já seguram o lock

    // Concorrência BLE callback x loop (análise §6.4)
    void lock();
    void unlock();
};
