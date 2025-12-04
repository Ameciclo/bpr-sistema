#pragma once
#include <Arduino.h>

// Estados da máquina de estados
enum BikeState {
  BOOT = 0,
  AT_BASE,
  SCANNING, 
  LOW_POWER,
  DEEP_SLEEP
};

// Configurações da bicicleta (recebidas da Base via BLE)
struct BikeConfig {
  uint8_t version = 1;
  uint16_t scan_interval_sec = 300;        // 5min padrão
  uint16_t scan_interval_low_batt_sec = 900; // 15min economia
  uint16_t deep_sleep_sec = 3600;          // 1h deep sleep
  float min_battery_voltage = 3.45;        // Bateria crítica
  char base_ble_name[32] = "BPR Base Station";
  uint32_t timestamp = 0;                  // Sync de tempo
};

// Registro de scan WiFi
struct WifiRecord {
  uint32_t timestamp;
  uint8_t bssid[6];
  int8_t rssi;
  uint8_t channel;
} __attribute__((packed));

// Status da bicicleta (enviado para Base)
struct BikeStatus {
  char bike_id[16];
  float battery_voltage;
  uint32_t last_scan_timestamp;
  uint8_t flags; // bit 0: low_battery, bit 1: error
  uint16_t records_count;
} __attribute__((packed));

// Pinos e constantes
#define BATTERY_PIN A0
#define LED_PIN 8
#define BUTTON_PIN 9

// BLE UUIDs
#define BLE_SERVICE_UUID "BAAD"
#define BLE_CONFIG_CHAR_UUID "F00D"  // Base -> Bike (configs)
#define BLE_STATUS_CHAR_UUID "BEEF"  // Bike -> Base (status)
#define BLE_DATA_CHAR_UUID "CAFE"    // Bike -> Base (dados WiFi)

// Configurações de energia
#define VOLTAGE_DIVIDER_RATIO 2.0
#define ADC_SAMPLES 10
#define MAX_WIFI_RECORDS 200

// Arquivos LittleFS
#define CONFIG_FILE "/config.json"
#define WIFI_DATA_FILE "/wifi_data.bin"