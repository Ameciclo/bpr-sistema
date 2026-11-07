#ifndef BUFFER_MANAGER_H
#define BUFFER_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include "constants.h"

class BufferManager {
private:
    SessionData currentSession;
    bool sessionActive;
    uint16_t bootId;

    // Fragmentação determinística da sessão (F04): uma "chunk" = subconjunto de
    // redes de UM scan que cabe numa mensagem BLE; scans densos são divididos
    // entre mensagens e remontados no consumidor por (session_id, scan index)
    // R16: chunk referencia o scan pelo ÍNDICE, não pelo timestamp
    struct FragChunk {
        uint16_t scan_idx;
        uint8_t net_start;
        uint8_t net_count;
    };
    size_t measureNetworkBytes(const NetworkData& net) const;
    void buildChunks(std::vector<FragChunk>& chunks) const;
    void quarantine(); // R22: preserva arquivo suspeito para análise

public:
    BufferManager();

    // Session management
    void startSession(const char* bikeId);
    void endSession();
    bool isSessionActive() const { return sessionActive; }
    void beginBoot(); // R16: novo boot_id por energização

    // Data collection
    bool addScan(uint32_t timestamp, const NetworkData* networks, uint8_t count);
    void addBattery(uint32_t timestamp, uint8_t percent);

    // Persistence (F08/R04/R22: header com magic/versão/CRC; gravação atômica
    // via temporário+rename; falha preserva a última versão íntegra)
    bool save();
    bool load();
    void clear();

    // Data access
    const SessionData& getCurrentSession() const { return currentSession; }
    uint32_t getSessionId() const;
    uint16_t getSessionSalt() const;
    uint16_t getBootId() const;

    // Fragmentação BLE (F04): mensagens <= MAX_BLE_FRAGMENT_BYTES com seq/total
    uint16_t fragmentCount() const;
    bool serializeFragment(uint16_t index, String& out) const;

    // Status
    bool isEmpty() const;
    bool isFull() const;
    size_t getSize() const;
};

#endif
