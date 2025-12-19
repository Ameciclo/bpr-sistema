#ifndef BPR_PROTOCOL_H
#define BPR_PROTOCOL_H

// ========================================
// 🔵 BLE Protocol Definitions
// ========================================

// Service and Characteristics UUIDs (MUST match central/bike)
#define BLE_SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define BLE_CHAR_DATA_UUID      "87654321-4321-4321-4321-cba987654321"
#define BLE_CHAR_CONFIG_UUID    "11111111-2222-3333-4444-555555555555"

// Device naming
#define BLE_DEVICE_NAME         "BPR Central"  // Central advertises as this
#define CENTRAL_BLE_NAME        "BPR Central"  // Bike searches for this
#define BIKE_ID_PREFIX          "bpr-"
#define BIKE_ID_LENGTH          10  // "bpr-" + 6 hex chars

// ========================================
// 📨 Message Types
// ========================================

#define MSG_TYPE_STATUS         "status"
#define MSG_TYPE_WIFI_DATA      "wifi_data"
#define MSG_TYPE_CONFIG_REQUEST "config_request"
#define MSG_TYPE_CONFIG_PUSH    "config_push"
#define MSG_TYPE_CONFIG_ACK     "config_received"

// ========================================
// 🔗 Data Limits (BLE constraints)
// ========================================

#define MAX_BLE_PACKET_SIZE     512   // Safe BLE packet size
#define MAX_WIFI_NETWORKS_BLE   10    // Networks per BLE packet
#define MAX_SSID_LENGTH         32
#define MAX_JSON_SIZE           2048
#define BLE_CHARACTERISTIC_SIZE 512   // From bike constants

// ========================================
// 🔧 Connection Parameters
// ========================================

#define BLE_SCAN_TIME_SEC       5
#define BLE_CONNECTION_TIMEOUT  10000  // ms
#define STATUS_REPORT_INTERVAL  5000   // ms (bike sends status every 5s)

#endif // BPR_PROTOCOL_H