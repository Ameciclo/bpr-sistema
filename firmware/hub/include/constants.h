#pragma once

// Hardware pins
#define LED_PIN 8

// Files
#define CONFIG_FILE "/config.json"
#define BUFFER_FILE "/buffer.json"

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

// BLE Configuration
#define BLE_DEVICE_NAME "BPR Hub Station"
#define BLE_SERVICE_UUID "12345678-1234-1234-1234-123456789abc"
#define BLE_CHAR_DATA_UUID "87654321-4321-4321-4321-cba987654321"

// Config AP
#define AP_SSID "BPR_Hub_Config"
#define AP_PASSWORD "botaprarodar"
#define CONFIG_TIMEOUT_MS 900000  // 15 minutos

// Timeouts
#define SHUTDOWN_TIMEOUT 3600000

// NTP Configuration (constants)
#define NTP_SERVER "pool.ntp.org"
#define TIMEZONE_OFFSET -10800  // UTC-3

// Fallback to AP thresholds
#define MAX_SYNC_FAILURES 5
#define SYNC_FAILURE_TIMEOUT_MS 1800000  // 30 minutos

// Events
enum SystemEvent {
    EVENT_CONFIG_COMPLETE,
    EVENT_SYNC_TRIGGER,
    EVENT_SYNC_COMPLETE,
    EVENT_INACTIVITY_TIMEOUT,
    EVENT_WAKE_UP,
    EVENT_ERROR
};