#pragma once
#include <Arduino.h>
#include <WebServer.h>

extern WebServer setupServer;
extern bool setupComplete;

void startSetupAP();
void setupWebServer();
void handleSetupMode();