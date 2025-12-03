#ifndef CONFIG_H
#define CONFIG_H

struct WiFiNetwork {
  char ssid[32];
  char bssid[18];
  int rssi;
  int channel;
  int encryption;
};

struct Config {
  char collectMode[20] = "normal";
  int scanTimeActive = 1000;
  int scanTimeInactive = 1000;
  char baseSSID1[32] = "";
  char basePassword1[32] = "";
  char baseSSID2[32] = "";
  char basePassword2[32] = "";
  char baseSSID3[32] = "";
  char basePassword3[32] = "";
  char bikeId[10] = "sl01";
  bool isAtBase = false;
  char firebaseUrl[128] = "";
  char firebaseKey[64] = "";
  bool cleanupEnabled = false;
  int maxUploadsHistory = 10;
  int baseProximityRssi = -80;
  int ntpSyncIntervalHours = 6;
  int statusUploadIntervalMinutes = 30;
  int chargingUpdateIntervalSeconds = 60; // Intervalo de update durante carregamento
  float batteryCalibration = 1.0; // Fator de calibração da bateria
  char apPassword[32] = "12345678"; // Senha do AP
  bool chargingModeEnabled = true; // Ativar modo carregamento
  float batteryLowThreshold = 15.0; // Threshold para bateria baixa (%)
  float batteryCriticalThreshold = 5.0; // Threshold para bateria crítica (%)
  bool lowBatteryAlertEnabled = true; // Ativar alertas de bateria baixa
  int statusUpdateIntervalMinutes = 60; // Intervalo para atualizações de status (minutos)
  bool statusUpdateEnabled = true; // Ativar atualizações programadas
};

struct ScanData {
  unsigned long timestamp;
  WiFiNetwork networks[10];
  int networkCount;
  float batteryLevel;
  bool isCharging;
};

struct SessionData {
  char sessionId[20];
  unsigned long startTime;
  unsigned long endTime;
  char mode[20];
  int totalScans;
  int totalNetworks;
};

extern Config config;
extern WiFiNetwork networks[30];
extern int networkCount;
extern ScanData dataBuffer[20];
extern int dataCount;
extern bool configMode;
extern bool timeSync;

void loadConfig();
void saveConfig();
void applyCollectMode(const char* mode);

#endif