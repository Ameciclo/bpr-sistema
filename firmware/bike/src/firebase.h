#ifndef FIREBASE_H
#define FIREBASE_H

#include <NTPClient.h>
#include <WiFiUdp.h>

extern WiFiUDP ntpUDP;
extern NTPClient timeClient;

void loadNTPState();
void saveNTPState(bool success);
bool needsNTPSync();
void syncTime();
void uploadData();
void uploadCheckIn();
void uploadSessionData();
void uploadLowBatteryAlert();
void uploadScheduledStatus();
String generateSessionId();
String buildCompleteSession();
void cleanupSessionFiles();

#endif