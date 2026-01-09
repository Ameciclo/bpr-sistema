#pragma once
#include <Arduino.h>

// Bike Registry Data
struct BikeRegistryData {
    uint32_t last_update;
    uint8_t bike_count;
    char bikes[10][16]; // max 10 bikes, 16 chars each (bpr-xxxxxx)
    uint8_t statuses[10]; // BikeStatus enum values
    uint32_t first_seen[10];
    uint32_t last_heartbeat[10];
    uint8_t padding[3];
} __attribute__((packed, aligned(4)));

// Bike Status Data (simplified)
struct BikeStatusData {
    uint32_t last_update;
    uint8_t bike_count;
    char bike_ids[10][16];
    uint8_t statuses[10];
    int16_t battery_levels[10];
    uint32_t last_contacts[10];
    uint32_t first_seen[10];
    uint8_t padding[2];
} __attribute__((packed, aligned(4)));

// Buffer Item (binary format)
struct BufferItemBin {
    char bikeId[16];
    uint32_t timestamp;
    uint16_t size;
    uint32_t crc32;
    uint8_t uploaded;
    uint8_t confirmed;
    uint8_t data[256];
} __attribute__((packed, aligned(4)));

// Buffer File Header
struct BufferFileHeader {
    uint32_t magic; // 0xBPR1
    uint32_t version;
    uint32_t item_count;
    uint32_t last_update;
} __attribute__((packed, aligned(4)));

#define BUFFER_MAGIC 0x42505231 // "BPR1"
#define BUFFER_VERSION 1