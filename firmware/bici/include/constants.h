#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <Arduino.h>
#include <cstdint>

// Hardware Configuration
#define LED_PIN 8
#define BUTTON_PIN 9
#define BATTERY_PIN A0

// BLE Configuration (from common/bpr_protocol.h)
#define CENTRAL_BLE_NAME "BPR Central"
#define BLE_SERVICE_UUID "12345678-1234-1234-1234-123456789abc"
#define BLE_CHAR_DATA_UUID "87654321-4321-4321-4321-cba987654321"
#define BLE_CHAR_CONFIG_UUID "11111111-2222-3333-4444-555555555555"

// Buffer Limits
#define MAX_SCANS 50
#define MAX_NETWORKS_PER_SCAN 20
#define MAX_BATTERY 10

// Session persistence (F08/R22: header com magic/versão/CRC do payload)
#define SESSION_FILE_MAGIC 0x42505253   // "BPRS"
// v4 (PRD lotes/recibos): campos do lote congelado + memória de limpeza.
// v3 nunca foi gravado em placa em produção (criado nesta branch) — bump
// seguro, sem perda de dados reais (R21).
#define SESSION_FILE_VERSION 4

// BLE transfer protocol (F04/F05: fragmentação com ACK por fragmento)
// MTU negociado 512 -> payload de escrita ~509; orçamento total por mensagem 460
#define MAX_BLE_FRAGMENT_BYTES 460
#define FRAGMENT_ACK_TIMEOUT_MS 5000
#define MAX_FRAGMENT_RETRIES 3
#define HEARTBEAT_INTERVAL_MS 5000

// X06: arena da árvore JSON do fragmento — NÃO é o mesmo orçamento do payload
// serializado. Pior caso no ESP32-C3 (slot de 16 B): chunk de 7 redes =
// 48 slots (768 B) + strings copiadas (<= payload serializado, 460 B) e
// fragmento final com 10 baterias = 41 slots (656 B). 1280 B cobre ambos.
#define FRAGMENT_JSON_ARENA 1280

// X02: orçamento global da visita à base — idle/busy/rejeição persistente
// têm prazo finito; a proteção de bateria nunca fica suspensa por mais que isso
#define AT_BASE_MAX_DURATION_MS 300000

// X08: pedido de config com retry — sem ACK de config dentro do prazo, repete
#define CONFIG_REQUEST_TIMEOUT_MS 10000
#define MAX_CONFIG_REQUEST_RETRIES 3

// Base discovery scheduling (F24: busca por agenda, não a cada loop)
#define BASE_SEARCH_INTERVAL_MS 30000

// State Machine (FSD compliant) — O06: STATE_LOST removido (era inalcançável:
// nenhum fluxo entrava nele; anúncio sem conexão é tratado pela busca agendada)
enum BikeState {
    STATE_BOOT = 0,
    STATE_SCANNING = 1,
    STATE_AT_BASE = 2,
    STATE_SLEEP = 3
};

// FSD Binary Structures
struct NetworkData {
    char ssid[33];
    char bssid[18];
    int16_t rssi;
    uint8_t channel;
} __attribute__((packed));

struct ScanData {
    uint32_t timestamp_millis;
    uint8_t network_count;
    NetworkData networks[MAX_NETWORKS_PER_SCAN];
} __attribute__((packed));

struct BatteryData {
    uint32_t timestamp_millis;
    uint8_t percent;
} __attribute__((packed));

struct SessionData {
    char bike_id[16];
    uint32_t session_start_millis;
    uint16_t session_salt;      // desambigua sessões entre reboots (F04)
    uint16_t scan_count;
    uint16_t battery_count;
    ScanData scans[MAX_SCANS];
    BatteryData battery[MAX_BATTERY];
    uint16_t boot_id;           // R16: origem monotônica por boot

    // ---- Lote de entrega congelado (PRD lotes/recibos, formato v4) ----
    // O lote fecha no início da entrega: identidade, total, bytes e hash
    // persistidos ANTES de transmitir; novas coletas ficam FORA do lote e
    // são preservadas (a limpeza autorizada cobre só este conjunto).
    uint8_t batch_frozen;            // 1 = lote fechado para entrega
    uint16_t batch_scan_count;       // observações cobertas pelo lote
    uint16_t batch_battery_count;    // leituras de bateria cobertas
    uint16_t batch_total_parts;      // partes anunciadas (0..total-1)
    uint32_t batch_total_bytes;      // soma dos comprimentos das partes
    uint8_t batch_sha256[32];        // hash do lote (contrato BTP1)
    uint8_t batch_authorized;        // autorização de limpeza persistida
    uint8_t batch_resumed;           // valor de 'resumed' CONGELADO no fechamento

    // Memória do último lote limpo: responder "recebido e apaguei" de novo
    // sem recriar a entrega se a confirmação final se perder (T12)
    uint8_t last_cleared_valid;
    uint32_t last_cleared_sid;
    uint16_t last_cleared_salt;
    uint16_t last_cleared_total;
    uint8_t last_cleared_sha[32];
} __attribute__((packed));

// Header do arquivo de sessão (F08/F17/R22: versão, tamanho e CRC validados no load)
struct SessionFileHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t payload_size;
    uint32_t payload_crc;       // R22: integridade do conteúdo, não só do header
} __attribute__((packed));

// Utility Functions
String bssidToString(const uint8_t* bssid);
String generateBikeId();

#endif