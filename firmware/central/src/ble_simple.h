#ifndef BLE_SIMPLE_H
#define BLE_SIMPLE_H

#include <Arduino.h>

// Simple BLE functions
bool initBLESimple();
void bleScanOnce();
bool isBLEReady();
bool startBLEServer();
int getConnectedClients();
void setBLEDeviceName(String name);
void onBLEConnect(uint16_t connHandle);
void onBLEMessage(uint16_t connHandle, String message);
void sendMessage(uint16_t connHandle, String message);

// Callback para detectar bikes novas
extern void registerPendingBike(String bleName, String macAddress);

#endif