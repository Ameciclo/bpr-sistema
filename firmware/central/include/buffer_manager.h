#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include <vector>
#include "binary_structs.h"
#include "constants.h"

#define DATA_ITEM_MAX_SIZE 512

// R02: itens são gravados/lidos um a um no arquivo por bike (registro de
// tamanho fixo BufferItemBin); nada carrega a sessão inteira em RAM.
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

class BufferManager {
public:
    BufferManager();

    // Inicialização
    bool beginWithAvailableHeap();
    uint32_t calculateCapacity();
    void cleanup();
    void begin(); // inclui reconciliação de lotes inconclusos (R05)

    // Ingestão BLE
    bool addData(const String& bikeId, const uint8_t* data, size_t length,
                 uint32_t sessionId = 0, uint16_t sessionSalt = 0, uint16_t seq = 0);
    bool addBikeData(const String& bikeId, const String& jsonData);

    // Upload (F18/F19/R05/R06): prepara UM lote, grava manifesto durável da
    // identidade dos itens ANTES de marcá-los, e devolve a chave estável.
    // Retorna nº de itens incluídos; 0 = sem pendentes ou falha de preparo.
    int prepareUploadBatch(DynamicJsonDocument& doc, int maxItems, String& batchId);

    // R05: confirmação/remove EXATAMENTE os itens do lote manifesto
    bool markAsConfirmed(const String& batchId);
    // R05: lote não entregue volta a pendente (item por item, in-place)
    bool rollbackUpload(const String& batchId);
    // R05: boot com lotes sem confirmação → reverte para pendente
    void reconcilePendingBatches();

    // Dedupe/completude de sessão (F04/F05/R12)
    bool hasCompleteSession(const String& bikeId, uint32_t sessionId, uint16_t salt, uint16_t totalFrags);

    // O06: pendências reais por bike (sessões e bytes não enviados)
    bool getBikePendingInfo(const String& bikeId, uint16_t& sessions, uint32_t& bytes);

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
    std::vector<String> listManifestPaths();
    void recoverStagedBuffers(); // N03: instalações interrompidas de spool

    // Streaming de registros de tamanho fixo.
    // visitor retorna false para parar a iteração antecipadamente.
    // Retorna true se o arquivo é válido (header + todos os itens lidos).
    // N08: registro corrompido/truncado propaga FALSO — o arquivo não é
    // "válido até o erro". Com `corruptRecords` != nullptr (modo reparo),
    // registros corrompidos são PULADOS (o registro é de tamanho fixo: os
    // seguintes continuam endereçáveis), a iteração prossegue até o fim e o
    // total de registros ilegíveis é reportado via *corruptRecords.
    bool forEachItem(const String& path, const std::function<bool(const DataItem&, uint32_t)>& visitor,
                     uint32_t* corruptRecords = nullptr);
    bool setItemUploadedFlag(const String& path, uint32_t index, bool uploaded);

    // R04: gravação recuperável (temporário + rename)
    bool rewriteBikeBufferExcluding(const String& bikeId, const std::vector<DataItem>& manifestItems);

    // R05: manifesto durável do lote
    String getManifestPath(const String& batchId);
    bool writeManifest(const String& batchId, const std::vector<DataItem>& items);
    bool readManifest(const String& batchId, std::vector<DataItem>& items);
    void removeManifest(const String& batchId);

    // O04: backup bruto por cópia de arquivo
    void createBackup();
    void cleanupOldBackups();
    void enforceBackupBudget(std::vector<String> stamps); // O04: limite de bytes entre lotes
    void printFileSize(const String& filePath);
    int getTotalDataCountUnlocked(); // chamadores já seguram o lock

    // Concorrência BLE callback x loop (R10: aquisição com resultado obrigatório)
    bool tryLock();
    void unlock();
};
