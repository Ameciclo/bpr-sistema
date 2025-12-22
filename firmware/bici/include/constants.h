#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <Arduino.h>
#include <cstdint>

// Hardware Configuration (fixed)
#define LED_PIN 8
#define BUTTON_PIN 9
#define BATTERY_PIN A0

// BLE Configuration (shared protocol - fixed)
#define CENTRAL_BLE_NAME "BPR Central"
#define BLE_SERVICE_UUID "12345678-1234-1234-1234-123456789abc"
#define BLE_CHAR_DATA_UUID "87654321-4321-4321-4321-cba987654321"
#define BLE_CHAR_CONFIG_UUID "11111111-2222-3333-4444-555555555555"

// Message Types (shared protocol - fixed)
#define MSG_TYPE_STATUS "status"
#define MSG_TYPE_WIFI_DATA "wifi_data"
#define MSG_TYPE_CONFIG_REQUEST "config_request"
#define MSG_TYPE_CONFIG_PUSH "config_push"
#define MSG_TYPE_CONFIG_ACK "config_ack"

// Deep Sleep Duration (hardware limit)
#define DEEP_SLEEP_DURATION 3600000000ULL // 1 hour in microseconds

// Config JSON Size
#define CONFIG_JSON_SIZE 1024

// State Machine
enum BikeState {
    STATE_BOOT = 0,
    STATE_CONFIG_REQUEST = 1,
    STATE_AT_BASE = 2,
    STATE_SCANNING = 3,
    STATE_LOST = 4,
    STATE_SLEEP = 5
};

// WiFi Record Structure
struct WiFiRecord {
    uint32_t timestamp;
    char ssid[33];
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
};

// Utility Functions
String bssidToString(const uint8_t* bssid);
String generateBikeId();

#endif