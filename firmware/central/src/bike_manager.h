#ifndef BIKE_MANAGER_H
#define BIKE_MANAGER_H

#include <Arduino.h>
#include "../include/structs.h"

// Funções de gerenciamento de bikes
void initBikeManager();
bool addConnectedBike(String bikeId, uint16_t connHandle);
bool removeConnectedBike(uint16_t connHandle);
ConnectedBike* findBikeByHandle(uint16_t connHandle);
ConnectedBike* findBikeById(String bikeId);
int getConnectedBikeCount();
void updateBikeLastSeen(String bikeId);
void markBikeNeedsConfig(String bikeId);
bool sendConfigToBike(String bikeId);
void processPendingConfigs();
void cleanupOldConnections();

#endif