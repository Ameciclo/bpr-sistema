#ifndef STRUCTS_H
#define STRUCTS_H

#include <stdint.h>

// Pacote de Configuração da Base → Bicicleta
struct BPRConfigPacket {
    uint8_t version;
    uint16_t deepSleepSec;
    uint16_t wifiScanInterval;
    uint16_t wifiScanLowBatt;
    float minBatteryVoltage;
    uint32_t timestamp;
} __attribute__((packed));

// Pacote de Status da Bicicleta → Base
struct BPRBikeStatus {
    char bikeId[8];
    float batteryVoltage;
    uint32_t lastWifiScan;
    uint8_t flags;
} __attribute__((packed));

// Evento interno do sistema
enum EventType {
    EVENT_BIKE_CONNECTED,
    EVENT_BIKE_DISCONNECTED,
    EVENT_BIKE_BATTERY_UPDATE,
    EVENT_BIKE_STATUS_UPDATE,
    EVENT_WIFI_CONNECTED,
    EVENT_WIFI_DISCONNECTED,
    EVENT_FIREBASE_SYNC_SUCCESS,
    EVENT_FIREBASE_SYNC_FAILED
};

struct SystemEvent {
    EventType type;
    char bikeId[8];
    float batteryVoltage;
    uint32_t timestamp;
    uint8_t flags;
};

// Configuração global do Firebase
struct GlobalConfig {
    uint8_t version;
    uint16_t wifi_scan_interval_sec;
    uint16_t wifi_scan_interval_low_batt_sec;
    uint16_t deep_sleep_after_sec;
    uint16_t ble_ping_interval_sec;
    float min_battery_voltage;
    uint32_t update_timestamp;
};

// Configuração da base
struct BaseConfig {
    char name[32];
    uint8_t max_bikes;
    char wifi_ssid[32];
    char wifi_password[64];
    float lat;
    float lng;
    uint32_t last_sync;
};

// Bike conectada
struct ConnectedBike {
    char bikeId[8];
    uint16_t connHandle;
    bool configSent;
    bool needsConfig;
    uint32_t lastSeen;
    float lastBattery;
};

// Cache de configurações
struct ConfigCache {
    GlobalConfig global;
    BaseConfig base;
    uint32_t lastUpdate;
    bool valid;
};

#endif