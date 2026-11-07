#pragma once
/*
 * BPR — Spool de lotes e recibos (PRD transferência de lotes/recibos).
 * Arquivo por lote {bike}_{sid}_{salt}.bin com header de estado + bitmap de
 * partes + registros de tamanho fixo indexados (acesso direto por índice).
 * Recibo {bike}_{sid}_{salt}.receipt persiste APÓS a confirmação da nuvem e
 * SOBREVIVE à remoção do payload (T11: autorização repetível sem payload).
 *
 * Concorrência: chamadores rodam no loop (eventos BLE enfileirados); o
 * mutex próprio protege contra usos futuros concorrentes.
 */
#include <Arduino.h>
#include <vector>
#include "batch_proto.h"
#include "binary_structs.h"
#include "constants.h"

#define BATCH_SPOOL_MAGIC 0x42505331   // "BPS1"
#define BATCH_SPOOL_VERSION 1
#define BATCH_RECEIPT_MAGIC 0x42524331 // "BRC1"

// Orçamento de admissão (PRD §6): reservar capacidade para terminar as
// entregas admitidas; lotes além disso recebem busy (T15)
#define MAX_ADMITTED_BATCHES 2
#define BATCH_ADMISSION_MARGIN_BYTES (64 * 1024)
// Upload progressivo à nuvem: partes por ciclo de sync (T09/T07 — resumível)
#define MAX_PART_UPLOADS_PER_SYNC 48

struct BatchPartRecord {
    uint16_t len;
    uint16_t reserved;
    uint32_t crc32;
    uint8_t data[MAX_ITEM_DATA_SIZE];
} __attribute__((packed, aligned(4)));

struct BatchSpoolHeader {
    uint32_t magic;
    uint32_t version;
    char bike_id[16];
    uint32_t session_id;
    uint16_t session_salt;
    uint16_t total_parts;
    uint32_t total_bytes;
    uint8_t sha256[32];
    uint8_t state;              // batch_state_t
    uint8_t part_bitmap[64];    // bit 1 = parte persistida e válida
    uint8_t uploaded_bitmap[64];// bit 1 = parte já enviada à nuvem
    uint32_t created_at;
    uint32_t updated_at;
    uint8_t padding[4];
} __attribute__((packed, aligned(4)));

struct BatchReceiptData {
    uint32_t magic;
    uint32_t version;
    char bike_id[16];
    uint32_t session_id;
    uint16_t session_salt;
    uint16_t total_parts;
    uint32_t total_bytes;
    uint8_t sha256[32];
    uint32_t confirmed_at;
    uint8_t padding[4];
} __attribute__((packed, aligned(4)));

class BatchSpool {
public:
    // ---- Consulta de estado ------------------------------------------------
    static bool find(const String& bikeId, uint32_t sid, uint16_t salt,
                     BatchSpoolHeader& out);
    static bool hasReceipt(const String& bikeId, uint32_t sid, uint16_t salt,
                           BatchReceiptData& out);
    static String spoolPath(const String& bikeId, uint32_t sid, uint16_t salt);
    static String receiptPath(const String& bikeId, uint32_t sid, uint16_t salt);

    // ---- Anúncio / admissão (PRD 4.1/§6) ------------------------------------
    // Admite o lote anunciado (reserva de capacidade). `reason` explica a
    // recusa (sem espaço / lotes demais / conflito de identidade).
    static bool admit(const String& bikeId, uint32_t sid, uint16_t salt,
                      uint16_t total, uint32_t totalBytes, const uint8_t sha[32],
                      String& reason);
    static int countAdmitted(); // lotes em recepção (não confirmados)

    // ---- Partes (PRD 4.2) ----------------------------------------------------
    // Valida índice/comprimento/CRC, persiste o registro e SÓ DEPOIS marca o
    // bitmap (crash no meio = registro órfão sobrescrito pelo reenvio — T06).
    // Duplicata idêntica: idempotente; conteúdo diferente: conflito (T03).
    // Retorna: 1=ok, 0=falha persistível (reenviar), -1=conflito.
    static int addPart(const String& bikeId, uint32_t sid, uint16_t salt,
                       uint16_t index, const uint8_t* bytes, uint16_t len);

    // Conferência do conjunto (PRD 4.3): bitmap cheio -> hash global ->
    // state complete_local ou hash_mismatch (nunca confirma divergente — T05)
    static bool checkComplete(const String& bikeId, uint32_t sid, uint16_t salt);

    // Página do bitmap de pendências (bit 1 = presente) para o batch_status
    static bool missingPage(const String& bikeId, uint32_t sid, uint16_t salt,
                            uint8_t page, uint8_t pages, uint8_t out[8]);

    // ---- Nuvem (PRD 5.1/5.2) -------------------------------------------------
    // Lotes prontos para upload (complete_local com partes ainda não enviadas)
    static std::vector<BatchSpoolHeader> listReadyForUpload();
    // Lê a parte i (registro) para envio; atualiza o bitmap de upload
    static bool readPartForUpload(const BatchSpoolHeader& h, uint16_t index,
                                  BatchPartRecord& out);
    static bool markUploaded(const String& bikeId, uint32_t sid, uint16_t salt,
                             uint16_t index);
    static bool markAwaitingCloud(const String& bikeId, uint32_t sid, uint16_t salt);

    // Recibo: persiste ANTES de remover o payload (PRD 5.2 — ordem das gravações)
    static bool writeReceipt(const BatchSpoolHeader& h, uint32_t confirmedAt);
    // Remove o payload do lote (mantém o recibo — T11)
    static bool removeSpool(const String& bikeId, uint32_t sid, uint16_t salt);
    // Lotes aguardando conferência da confirmação remota
    static std::vector<BatchSpoolHeader> listAwaitingCloud();

private:
    static bool writeHeader(const String& path, const BatchSpoolHeader& h);
    static bool readHeader(const String& path, BatchSpoolHeader& h);
    static size_t partOffset(uint16_t index);
};
