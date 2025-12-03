#ifndef BLE_SIMPLE_H
#define BLE_SIMPLE_H

#include <Arduino.h>

// Simple BLE functions
bool initBLESimple();
void bleScanOnce();
bool isBLEReady();
bool startBLEServer();
int getConnectedClients();

#endif