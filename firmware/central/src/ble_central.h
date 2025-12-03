#ifndef BLE_CENTRAL_H
#define BLE_CENTRAL_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "structs.h"

// BLE Central configuration
#define BLE_SCAN_TIME_SEC 10
#define BLE_SCAN_INTERVAL_MS 30000
#define BLE_MAX_BIKES 10

// BLE Service UUIDs for bike communication
#define BIKE_SERVICE_UUID "b07a-b1c3-9000-b1c3-b1c3b1c3b1c3"
#define BIKE_STATUS_CHAR_UUID "b07a-b1c3-9001-b1c3-57a7u5b1c301"
#define BIKE_DATA_CHAR_UUID "b07a-b1c3-9002-b1c3-da7ab1c3b1c3"

struct BLEBikeData {
    char bike_id[16];
    float battery_voltage;
    uint32_t last_wifi_scan;
    int8_t rssi;
    bool is_connected;
    unsigned long last_seen;
};

// Function declarations
bool initBLECentral();
void bleCentralTask(void *parameter);
void startBikeScan();
void stopBikeScan();
bool connectToBike(const char* bike_id);
void disconnectFromBike(const char* bike_id);
int getConnectedBikesCount();
BLEBikeData* getConnectedBikes();
bool sendConfigToBike(const char* bike_id, const char* config_json);

#endif