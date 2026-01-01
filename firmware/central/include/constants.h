#pragma once

#include "../common/bpr_protocol.h"
#include "../common/bpr_types.h"

// Hardware pins
#define LED_PIN 8

// Files
#define CONFIG_FILE "/config.json"
#define CREDENTIALS_FILE "/config_credentials.json"
#define BUFFER_FILE "/buffer.json"
#define BIKE_REGISTRY_FILE "/bike_registry.json"
#define BIKE_DATA_FILE "/bike_data.json"
#define BIKE_CONFIG_CACHE_FILE "/bike_config_versions.json"
#define BIKE_CONFIGS_FILE "/bike_configs.json"

// JSON Buffer sizes - Specific document types
#define BIKE_REGISTRY_BUFFER 4096      // Multiple bikes with full data
#define BIKE_CONFIG_BUFFER 1024        // Single bike configuration
#define BIKE_DATA_BUFFER 1024          // Single bike data/scans
#define BIKE_HEARTBEAT_BUFFER 512      // Heartbeat responses
#define BLE_COMMAND_BUFFER 256         // Simple BLE commands
#define CENTRAL_HEARTBEAT_BUFFER 512   // Central heartbeat data
#define BUFFER_PERSISTENCE_BUFFER 8192 // Buffer save/load operations
#define UPLOAD_BATCH_BUFFER 4096       // Firebase upload batches
#define CONFIG_VERSION_BUFFER 256      // Version checking
#define STATUS_RESPONSE_BUFFER 512     // Status/queue responses
#define CONFIG_JSON_BUFFER_SIZE 1536
#define CONFIG_CREDENTIALS_SIZE 512 // Credentials JSON buffer

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