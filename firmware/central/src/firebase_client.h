#ifndef FIREBASE_CLIENT_H
#define FIREBASE_CLIENT_H

#include <Arduino.h>

bool initFirebase();
bool uploadBikeData(String bikeJson);
bool uploadWifiScan(String wifiJson);
bool uploadBatteryAlert(String alertJson);
bool isFirebaseReady();
String getFirebaseStatus();

#endif