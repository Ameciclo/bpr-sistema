#ifndef BPR_TYPES_H
#define BPR_TYPES_H

#include <Arduino.h>
#include <stdint.h>
#include "bpr_protocol.h"

// ========================================
// 📡 WiFi Data Structures (BLE)
// ========================================

struct WiFiRecord {
    uint32_t timestamp;
    char ssid[33];          // 32 chars + null terminator
    uint8_t bssid[6];       // MAC address
    int8_t rssi;            // Signal strength
    uint8_t channel;        // WiFi channel
};

// ========================================
// 📨 BLE Message Structures
// ========================================

struct StatusMessage {
    char bike_id[16];
    float battery;
    int records;
    uint32_t timestamp;
    uint32_t heap;
};

struct ConfigRequest {
    char type[16];          // "config_request"
    char bike_id[16];
    uint32_t timestamp;
};

struct ConfigAck {
    char type[16];          // "config_received"
    char bike_id[16];
    char status[16];        // "ok" or "error"
    uint32_t timestamp;
};

// ========================================
// 🆔 ID Utilities
// ========================================

inline String generateBikeId() {
    uint64_t chipid = ESP.getEfuseMac();
    char chipStr[7];
    snprintf(chipStr, sizeof(chipStr), "%06x", (uint32_t)(chipid & 0xFFFFFF));
    return String(BIKE_ID_PREFIX) + String(chipStr);
}

inline bool isValidBikeId(const String& bikeId) {
    return bikeId.length() == BIKE_ID_LENGTH && 
           bikeId.startsWith(BIKE_ID_PREFIX);
}

// ========================================
// 🔗 BLE Utilities
// ========================================

inline String bssidToString(const uint8_t* bssid) {
    char bssidStr[18];
    snprintf(bssidStr, sizeof(bssidStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    return String(bssidStr);
}

inline bool isValidMessageSize(size_t size) {
    return size > 0 && size <= MAX_JSON_SIZE;
}

#endif // BPR_TYPES_H