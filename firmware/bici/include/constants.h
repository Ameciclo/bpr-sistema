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

// State Machine (FSD compliant)
enum BikeState {
    STATE_BOOT = 0,
    STATE_SCANNING = 1,
    STATE_AT_BASE = 2,
    STATE_LOST = 3,
    STATE_SLEEP = 4
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
    uint16_t scan_count;
    uint16_t battery_count;
    ScanData scans[MAX_SCANS];
    BatteryData battery[MAX_BATTERY];
} __attribute__((packed));

// Utility Functions
String bssidToString(const uint8_t* bssid);
String generateBikeId();

#endif