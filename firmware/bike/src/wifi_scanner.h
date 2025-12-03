#ifndef WIFI_SCANNER_H
#define WIFI_SCANNER_H

#include "config.h"
#include <Arduino.h>

void scanWiFiNetworks();
bool checkAtBase();
bool connectToBase();
String getBasePassword(String ssid);
void storeData();
float getBatteryLevel();
float getBatteryVoltage();
bool needsLowBatteryAlert();
bool needsScheduledStatusUpdate();

#endif