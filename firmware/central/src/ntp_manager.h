#pragma once
#include <Arduino.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

extern WiFiUDP ntpUDP;
extern NTPClient* timeClient;
extern bool ntpSynced;

void initNTP();
bool syncNTP();
unsigned long getCurrentTimestamp();
unsigned long correctTimestamp(unsigned long bikeTimestamp, unsigned long bikeMillis);
void sendNTPToBike();