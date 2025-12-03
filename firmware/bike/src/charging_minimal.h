#ifndef CHARGING_MINIMAL_H
#define CHARGING_MINIMAL_H

// Versão mínima sem dependências externas
struct ChargingStatus {
  bool isCharging = false;
  bool isConnected = false;
  unsigned long lastUpdate = 0;
  char ipAddress[16] = "";
  char connectedSSID[32] = "";
};

extern ChargingStatus chargingStatus;

// Funções básicas
bool detectCharging();
void handleChargingMode();
void connectToBaseForCharging();

#endif