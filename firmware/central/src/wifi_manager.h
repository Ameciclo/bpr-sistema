#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void wifiManagerTask(void *parameter);
void connectToWiFi();
void syncNTP();
bool isWiFiConnected();
uint32_t getCurrentTimestamp();

#endif