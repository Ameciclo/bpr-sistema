#pragma once
#include <Arduino.h>

// Bike Registry Data
struct BikeRegistryData {
    uint32_t last_update;
    uint8_t bike_count;
    uint32_t bikes[10];        // bpr-XXXXXX (hex) → 24 bits como int
    uint8_t statuses[10];      // BikeStatus enum values
    uint64_t mac_addresses[10]; // MAC como uint64_t (6 bytes úteis)
    uint32_t created_at[10];   // timestamp criação
    uint32_t last_seen[10];    // último heartbeat
    uint8_t padding[3];
} __attribute__((packed, aligned(4)));

// Bike Status Data (simplified)
struct BikeStatusData {
    uint32_t last_update;
    uint8_t bike_count;
    uint32_t bike_ids[10];     // bpr-XXXXXX (hex) → 24 bits
    uint8_t statuses[10];
    int16_t battery_levels[10];
    uint32_t last_contacts[10];
    uint32_t created_at[10];   // renomeado de first_seen
    uint8_t padding[2];
} __attribute__((packed, aligned(4)));

// Buffer Item (binary format) — F04: 512 bytes + identificação de sessão/fragmento
struct BufferItemBin {
    uint32_t bikeId;           // bpr-XXXXXX (hex) → 24 bits
    uint32_t timestamp;
    uint16_t size;
    uint32_t crc32;
    uint32_t session_id;       // dedupe idempotente (F05/F19)
    uint16_t session_salt;
    uint16_t seq;
    uint8_t uploaded;
    uint8_t confirmed;
    uint8_t data[512];
} __attribute__((packed, aligned(4)));

// Buffer File Header
struct BufferFileHeader {
    uint32_t magic; // 0xBPR1
    uint32_t version;
    uint32_t item_count;
    uint32_t last_update;
} __attribute__((packed, aligned(4)));

// Bike Config Data (binary format) — F13: versão por bike
struct BikeConfigData {
    uint32_t last_update;
    uint8_t bike_count;
    uint32_t bike_ids[10];       // bpr-XXXXXX (hex) → 24 bits
    uint16_t scan_intervals[10];     // seconds
    uint16_t scan_timeouts[10];      // milliseconds
    uint8_t max_networks[10];
    int8_t rssi_thresholds[10];
    uint16_t sleep_durations[10];    // seconds
    uint16_t ble_scan_times[10];     // seconds
    uint8_t dev_modes[10];           // 0=false, 1=true
    uint32_t config_versions[10];    // versão remota por bike (F13/F20)
    uint8_t padding[3];
} __attribute__((packed, aligned(4)));

// Conversão bpr-XXXXXX — formato hex padrão (firma o contrato do firmware/common)
inline uint32_t bikeIdToInt(const String& bikeId) {
    if (!bikeId.startsWith("bpr-")) {
        return 0; // ID inválido
    }
    String digits = bikeId.substring(4);
    if (digits.length() == 0 || digits.length() > 6) {
        return 0;
    }
    for (unsigned int i = 0; i < digits.length(); i++) {
        char c = digits.charAt(i);
        bool isHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!isHex) return 0;
    }
    return (uint32_t)strtol(digits.c_str(), NULL, 16);
}

inline String intToBikeId(uint32_t bikeInt) {
    char buffer[12];
    snprintf(buffer, sizeof(buffer), "bpr-%06x", bikeInt & 0xFFFFFF);
    return String(buffer);
}

inline bool isValidBikeIdFormat(const String& bikeId) {
    return bikeIdToInt(bikeId) != 0;
}

// Conversão MAC Address
inline uint64_t macStringToInt(const String& macStr) {
    // "AA:BB:CC:DD:EE:FF" → 0xAABBCCDDEEFF
    uint64_t mac = 0;
    String cleanMac = macStr;
    cleanMac.replace(":", "");
    for (int i = 0; i < 12 && i < cleanMac.length(); i += 2) {
        String byteStr = cleanMac.substring(i, i + 2);
        mac = (mac << 8) | strtol(byteStr.c_str(), NULL, 16);
    }
    return mac;
}

inline String macIntToString(uint64_t macInt) {
    char buffer[18];
    snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
             (uint8_t)(macInt >> 40), (uint8_t)(macInt >> 32),
             (uint8_t)(macInt >> 24), (uint8_t)(macInt >> 16),
             (uint8_t)(macInt >> 8), (uint8_t)macInt);
    return String(buffer);
}

#define BUFFER_MAGIC 0x42505231 // "BPR1"
#define BUFFER_VERSION 2        // F04: layout com session_id/seq e data[512]
