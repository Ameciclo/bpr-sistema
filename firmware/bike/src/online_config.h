#ifndef ONLINE_CONFIG_H
#define ONLINE_CONFIG_H

#include <Arduino.h>

struct OnlineConfig {
  // Configurações básicas
  char bikeId[10];
  char collectMode[20];
  int scanTimeActive;
  int scanTimeInactive;
  
  // Bases WiFi
  char baseSSID1[32];
  char basePassword1[32];
  char baseSSID2[32];
  char basePassword2[32];
  char baseSSID3[32];
  char basePassword3[32];
  int baseProximityRssi;
  
  // Firebase
  char firebaseUrl[128];
  char firebaseKey[64];
  
  // Configurações de operação
  bool cleanupEnabled;
  int maxUploadsHistory;
  float batteryLowThreshold;
  float batteryCriticalThreshold;
  int statusUpdateIntervalMinutes;
  
  // Configurações de WiFi
  int wifiTxPower; // Intensidade do sinal WiFi
  int wifiTimeout;
  
  // NTP
  int ntpSyncIntervalHours;
  
  // Timestamp da última sincronização
  unsigned long lastConfigSync;
  bool configSynced;
};

extern OnlineConfig onlineConfig;

// Funções principais
bool initializeOnlineConfig();
bool syncConfigFromFirebase();
bool uploadConfigStatus();
bool isConfigurationValid();
void applyOnlineConfig();

#endif