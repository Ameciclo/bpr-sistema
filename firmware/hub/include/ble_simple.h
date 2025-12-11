#pragma once
#include <Arduino.h>

bool initBLESimple();
bool startBLEServer();
bool isBLEReady();
int getConnectedClients();
void bleScanOnce();
void setBLEDeviceName(String name);
void onBLEConnect(uint16_t connHandle);
void onBLEMessage(uint16_t connHandle, String message);
void sendMessage(uint16_t connHandle, String message);
void registerPendingBike(String bleName, String macAddress);