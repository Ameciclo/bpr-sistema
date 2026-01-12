#pragma once

#include "../common/bpr_protocol.h"
#include "../common/bpr_types.h"

// Hardware pins
#define LED_PIN 8

// Files
#define CONFIG_FILE "/config.bin"
#define CREDENTIALS_FILE "/config_credentials.bin"
#define BUFFER_DIR "/buffer"
#define BIKE_REGISTRY_FILE "/bike_registry.bin"
#define BIKE_STATUS_FILE "/bike_status.bin"
#define BIKE_CONFIGS_FILE "/bike_configs.csv"
// BIKE_CONFIG_CACHE_FILE removido - usar last_update nos configs

// Small buffers for minimal operations
#define BLE_COMMAND_BUFFER 256         // Simple BLE commands
#define HEARTBEAT_BUFFER 512           // Heartbeat responses
#define BIKE_DATA_BUFFER 1024          // Bike data parsing
#define CONFIG_VERSION_BUFFER 256      // Version checking
#define BIKE_REGISTRY_BUFFER 2048      // Bike registry data
#define CENTRAL_HEARTBEAT_BUFFER 512   // Central heartbeat
#define BUFFER_PERSISTENCE_BUFFER 4096 // Buffer persistence
#define STATUS_RESPONSE_BUFFER 512     // Status response buffer

// Timing constants (ms)
#define WIFI_TIMEOUT_DEFAULT 30000
#define SYNC_INTERVAL_DEFAULT 300000
#define HEARTBEAT_INTERVAL 60000

// LED timing
#define LED_BOOT_INTERVAL 100
#define LED_BLE_INTERVAL 2000
#define LED_SYNC_INTERVAL 500
#define LED_ERROR_INTERVAL 50
#define LED_COUNT_INTERVAL 300
#define LED_COUNT_PAUSE 2000

// Buffer limits
#define MAX_BUFFER_SIZE 8000
#define MAX_BIKES 10
#define MINIMAL_FREE_FS_SPACE 20480

// Buffer sync thresholds
#define BUFFER_SYNC_THRESHOLD_PERCENT 80
#define BUFFER_CRITICAL_THRESHOLD_PERCENT 95

// Config AP
#define AP_SSID "BPR Central"
#define AP_PASSWORD "botaprarodar"
#define AP_IP IPAddress(192, 168, 4, 1)  // IP padrão ESP32: 192.168.4.1
#define AP_GATEWAY IPAddress(192, 168, 4, 1)
#define AP_SUBNET IPAddress(255, 255, 255, 0)
#define CONFIG_TIMEOUT_MS 900000 // 15 minutos

// NTP Configuration (constants)
#define NTP_SERVER "pool.ntp.org"
#define TIMEZONE_OFFSET -10800 // UTC-3

// Fallback to AP thresholds
#define MAX_SYNC_FAILURES 5
#define SYNC_FAILURE_TIMEOUT_MS 1800000 // 30 minutos

// System States
enum SystemState
{
    STATE_BOOT,
    STATE_INITIAL_CONFIG_AP, // Config AP obrigatório (sem config válida)
    STATE_TEMP_CONFIG_AP,    // Config AP temporário (após falhas de sync)
    STATE_INITIAL_SYNC,
    STATE_BIKE_PAIRING,
    STATE_CLOUD_SYNC
};

inline const char *getStateName(SystemState state)
{
    switch (state)
    {
    case STATE_BOOT:
        return "BOOT";
    case STATE_INITIAL_CONFIG_AP:
        return "INITIAL_CONFIG_AP";
    case STATE_TEMP_CONFIG_AP:
        return "TEMP_CONFIG_AP";
    case STATE_INITIAL_SYNC:
        return "INITIAL_SYNC";
    case STATE_BIKE_PAIRING:
        return "BIKE_PAIRING";
    case STATE_CLOUD_SYNC:
        return "CLOUD_SYNC";
    default:
        return "UNKNOWN";
    }
}

// Events
enum SystemEvent
{
    EVENT_CONFIG_COMPLETE,
    EVENT_SYNC_TRIGGER,
    EVENT_SYNC_COMPLETE,
    EVENT_INACTIVITY_TIMEOUT,
    EVENT_WAKE_UP,
    EVENT_ERROR
};

// Bike Pairing States
enum PairingStatus {
    PAIRING_IDLE,           // Nenhuma atividade crítica
    PAIRING_RECEIVING_DATA, // Recebendo dados de bike
    PAIRING_SENDING_CONFIG, // Enviando config para bike
    PAIRING_BUSY           // Atividade geral (múltiplas bikes)
};

// Bike Events
enum BikeEvent {
    BIKE_ARRIVED,
    BIKE_LEFT,
    BIKE_COUNT_CHANGED
};

// Bike Status
enum BikeStatus {
    STATUS_UNKNOWN = 0,
    STATUS_PENDING = 1,
    STATUS_ALLOWED = 2,
    STATUS_BLOCKED = 3
};

// Pairing timeouts
#define BIKE_DATA_TIMEOUT_MS 30000     // 30s timeout por bike