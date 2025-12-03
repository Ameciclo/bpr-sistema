#ifndef CONFIG_H
#define CONFIG_H

// WiFi Configuration
#define WIFI_TIMEOUT_MS 30000
#define WIFI_RETRY_DELAY_MS 5000

// Configuration file path
#define CONFIG_FILE_PATH "/firebase_config.json"

// BLE Configuration
#define BLE_DEVICE_NAME "BPR Base Station"
#define BLE_MAX_CONNECTIONS 10
#define BLE_PACKET_SIZE 64

// Task Configuration
#define WIFI_TASK_STACK_SIZE 4096
#define FIREBASE_TASK_STACK_SIZE 8192
#define BLE_TASK_STACK_SIZE 12288
#define EVENT_TASK_STACK_SIZE 4096
#define BUFFER_TASK_STACK_SIZE 4096
#define SELFCHECK_TASK_STACK_SIZE 2048

// Timing Configuration
#define NTP_SYNC_INTERVAL_MS 600000  // 10 minutes
#define FIREBASE_SYNC_INTERVAL_MS 30000  // 30 seconds
#define HEARTBEAT_INTERVAL_MS 60000  // 1 minute
#define SELFCHECK_INTERVAL_MS 30000  // 30 seconds

// Buffer Configuration
#define MAX_BUFFER_ENTRIES 100
#define BUFFER_FILE_PATH "/buffer.json"

// Watchdog Configuration
#define WATCHDOG_TIMEOUT_MS 60000

// BLE UUIDs - Bota Pra Rodar themed 🚲
#define BPR_CONFIG_SERVICE_UUID "b07a-c0f1-9000-b1c3-c0f19000b1c3"
#define CONFIG_PACKET_CHAR_UUID "b07a-c0f1-9001-b1c3-packe7c0f19001"
#define BATTERY_THRESHOLD_CHAR_UUID "b07a-c0f1-9002-b1c3-ba77e4yc0f19"
#define SLEEP_INTERVAL_CHAR_UUID "b07a-c0f1-9003-b1c3-51eep1c0f193"
#define WIFI_SCAN_INTERVAL_CHAR_UUID "b07a-c0f1-9004-b1c3-w1f15cc0f194"

#define BPR_STATUS_SERVICE_UUID "b07a-5747-9000-b1c3-57a7u5b1c300"
#define BIKE_ID_CHAR_UUID "b07a-5747-9001-b1c3-b1c31db1c301"
#define BATTERY_LEVEL_CHAR_UUID "b07a-5747-9002-b1c3-ba77e4yb1c32"
#define LAST_WIFI_SCAN_CHAR_UUID "b07a-5747-9003-b1c3-1a57w1f1b1c3"
#define MODE_CHAR_UUID "b07a-5747-9004-b1c3-m0d3b1c3b1c4"

#define BPR_TIME_SERVICE_UUID "b07a-71me-9000-b1c3-71me5e4v1ce0"
#define EPOCH_TS_CHAR_UUID "b07a-71me-9001-b1c3-ep0ch71meb1c"

#endif