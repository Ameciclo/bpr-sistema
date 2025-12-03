#ifndef CHARGING_MODE_H
#define CHARGING_MODE_H

#include "config.h"

struct ChargingStatus {
  bool isCharging = false;
  bool isConnected = false;
  unsigned long lastUpdate = 0;
  unsigned long connectionStart = 0;
  char connectedSSID[32] = "";
  char ipAddress[16] = "";
};

extern ChargingStatus chargingStatus;

// Funções principais
bool detectCharging();
void handleChargingMode();
void sendChargingUpdate();
void connectToBaseForCharging();

// Dados para envio em tempo real
struct RealtimeData {
  unsigned long timestamp;
  float batteryLevel;
  bool isCharging;
  int networksDetected;
  char strongestSSID[32];
  int strongestRSSI;
  char ipAddress[16];
  unsigned long uptime;
};

#endif