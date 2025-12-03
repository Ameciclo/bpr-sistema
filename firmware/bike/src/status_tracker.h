#ifndef STATUS_TRACKER_H
#define STATUS_TRACKER_H

#include <Arduino.h>

struct ConnectionEvent {
  unsigned long timestamp;
  char baseSSID[32];
  char ip[16];
  bool connected; // true = conectou, false = desconectou
};

struct BatteryEvent {
  unsigned long timestamp;
  float percentage;
};

void trackConnection(const char* baseSSID, const char* ip, bool connected);
void trackBattery(float percentage);
void uploadStatus();

#endif