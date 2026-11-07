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
    // PRD lotes/recibos: chunks do SUBCONJUNTO congelado (lote fechado)
    void buildChunksForRange(std::vector<FragChunk>& chunks, uint16_t scanLimit) const;
    // Serialização com limites explícitos — freeze e reenvios produzem
    // bytes IDÊNTICOS (o hash do lote depende disso); resumedFixed congela
    // o valor de 'resumed' no fechamento do lote
    bool serializePartWithLimits(uint16_t index, uint16_t total,
                                 uint16_t scanLimit, uint16_t batteryLimit,
                                 bool resumedFixed, String& out) const;
    void quarantineFile(const char* path); // R22/X05: preserva arquivo suspeito
    bool tryLoadFrom(const char* path);    // N01: valida sem candidata na pilha
    void resetBatchFields();               // zera campos de lote (não a memória de limpeza)

    // N02: saturação de bateria é explícita e diagnosticável
    bool batterySaturationLogged;

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

    // ---- Lote de entrega (PRD lotes/recibos — contrato BTP1) -----------
    // Fecha o lote no início da entrega: congela identidade/total/bytes/hash
    // a partir do conteúdo ATUAL e persiste. Idempotente; novas coletas
    // posteriores ficam fora do lote e são preservadas (T14).
    bool freezeBatch();
    bool isBatchFrozen() const { return currentSession.batch_frozen != 0; }
    bool hasBatchToDeliver() const;         // lote congelado ainda não autorizado
    uint16_t getBatchTotalParts() const { return currentSession.batch_total_parts; }
    uint32_t getBatchTotalBytes() const { return currentSession.batch_total_bytes; }
    void getBatchSha256Hex(char out[65]) const;
    uint16_t getBatchScanCount() const { return currentSession.batch_scan_count; }

    // Autorização de limpeza (PRD 5.2): valida identidade+hash, persiste a
    // autorização ANTES de remover qualquer registro; depois executa a
    // limpeza cirúrgica (só o conjunto coberto) e persiste o estado final.
    bool applyClearAuthorization(const char shaHex[65], uint16_t totalParts, uint32_t totalBytes);
    bool performClearIfAuthorized();
    // T12: lote já limpo — responder "recebido e apaguei" de novo, sem
    // recriar a entrega, se a confirmação final se perder
    bool isBatchCleared(uint32_t sid, uint16_t salt, const char shaHex[65]) const;
    bool wasAuthorized() const { return currentSession.batch_authorized != 0; }

    // Status
    bool isEmpty() const;
    bool isFull() const;
    size_t getSize() const;              // capacidade da estrutura (constante)
    size_t getPendingDataBytes() const;  // N02: carga útil real pendente
};

#endif
