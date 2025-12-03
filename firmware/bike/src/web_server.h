#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <WebServer.h>

extern WebServer server;
extern bool configMode;

void startConfigMode();
void handleRoot();
void handleConfig();
void handleSave();
void handleWifi();
void handleDados();
void handleDownload();
void handleTest();
void handleExit();

#endif