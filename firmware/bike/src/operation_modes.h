#ifndef OPERATION_MODES_H
#define OPERATION_MODES_H

#include <Arduino.h>

enum OperationMode {
  MODE_TRAVEL,    // Modo viagem - coleta dados
  MODE_BASE,      // Modo base - sincronização e dormida
  MODE_STARTUP    // Modo inicialização
};

struct ModeState {
  OperationMode currentMode;
  unsigned long modeStartTime;
  unsigned long lastModeCheck;
  bool ntpSyncedAtStart;
  bool ntpSyncedAtEnd;
  int consecutiveBaseDetections;
  int consecutiveTravelDetections;
};

extern ModeState modeState;

// Funções principais
void initializeOperationModes();
OperationMode detectCurrentMode();
void switchToMode(OperationMode newMode);
void handleTravelMode();
void handleBaseMode();
void handleStartupMode();

// Funções auxiliares
bool isAtBaseLocation();
void performNTPSync(bool isStartup = false);
void performBaseSynchronization();
void performTravelDataCollection();
int getDelayForCurrentMode();

#endif