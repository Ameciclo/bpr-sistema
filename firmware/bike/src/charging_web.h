#ifndef CHARGING_WEB_H
#define CHARGING_WEB_H

#ifdef ESP8266
  #include <ESP8266WebServer.h>
  extern ESP8266WebServer chargingServer;
#else
  #include <WebServer.h>
  extern WebServer chargingServer;
#endif

#include "config.h"

// Funções principais
void startChargingWebServer();
void stopChargingWebServer();
void handleChargingWebServer();

// Handlers das páginas
void handleChargingRoot();
void handleChargingConfig();
void handleChargingData();
void handleChargingDownload();
void handleChargingUpload();
void handleChargingStatus();

// APIs REST
void handleApiConfig();
void handleApiData();
void handleApiStatus();

#endif