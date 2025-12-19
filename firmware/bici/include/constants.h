#ifndef CONSTANTS_H
#define CONSTANTS_H

#include "../common/bpr_protocol.h"
#include "../common/bpr_types.h"

// Hardware Configuration
#define LED_PIN 8
#define BUTTON_PIN 9
#define BATTERY_PIN A0

// WiFi Configuration
#define WIFI_SCAN_TIMEOUT 5000
#define WIFI_MAX_NETWORKS 20
#define WIFI_RSSI_THRESHOLD -90

// Power Management
#define RADIO_COORDINATION_DELAY 300
#define LIGHT_SLEEP_DURATION 1000
#define DEEP_SLEEP_DURATION 3600000000ULL // 1 hour in microseconds

// Battery Thresholds
#define BATTERY_CRITICAL_VOLTAGE 3.2
#define BATTERY_LOW_VOLTAGE 3.45
#define BATTERY_FULL_VOLTAGE 4.2

// Timing Constants
#define EMERGENCY_BUTTON_HOLD_TIME 3000
#define MAX_TIME_WITHOUT_BASE 7200000 // 2 hours

// Buffer Sizes
#define MAX_WIFI_RECORDS 100
#define CONFIG_JSON_SIZE 1024

// State Machine
enum BikeState {
    STATE_BOOT = 0,
    STATE_AT_BASE = 1,
    STATE_SCANNING = 2,
    STATE_LOW_POWER = 3,
    STATE_DEEP_SLEEP = 4
};

#endif