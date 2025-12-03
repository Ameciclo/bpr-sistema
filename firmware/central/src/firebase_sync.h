#ifndef FIREBASE_SYNC_H
#define FIREBASE_SYNC_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "structs.h"

void firebaseSyncTask(void *parameter);
void syncConfigurations();
void sendHeartbeat();
void updateBikeStatus(const char* bikeId, float batteryVoltage, uint32_t lastBleContact);
void createAlert(const char* alertType, const char* bikeId);
GlobalConfig getGlobalConfig();
BaseConfig getBaseConfig();

#endif