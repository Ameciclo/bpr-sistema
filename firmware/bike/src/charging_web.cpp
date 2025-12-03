#include "charging_web.h"
#include "charging_mode.h"

// Declaração da função de bateria
extern float getBatteryLevel();

#ifdef ESP8266
  #include <LittleFS.h>
  ESP8266WebServer chargingServer(8080);
#else
  #include <SPIFFS.h>
  #define LittleFS SPIFFS
  WebServer chargingServer(8080);
#endif

void startChargingWebServer() {
  if (!chargingStatus.isConnected) return;
  
  chargingServer.on("/", handleChargingRoot);
  chargingServer.on("/status", handleChargingStatus);
  chargingServer.on("/api/status", handleApiStatus);
  
  chargingServer.begin();
  Serial.printf("Servidor web carregamento: http://%s:8080\n", chargingStatus.ipAddress);
}

void stopChargingWebServer() {
  chargingServer.stop();
}

void handleChargingWebServer() {
  if (chargingStatus.isCharging && chargingStatus.isConnected) {
    chargingServer.handleClient();
  }
}

void handleChargingRoot() {
  String html = "<!DOCTYPE html><html><head><title>Bike Carregamento</title></head><body>";
  html += "<h1>Bike " + String(config.bikeId) + " - Carregamento</h1>";
  html += "<p>Status: CARREGANDO</p>";
  html += "<p>IP: " + String(chargingStatus.ipAddress) + "</p>";
  html += "<p>Bateria: " + String(getBatteryLevel(), 1) + "%</p>";
  html += "<p>Conectado: " + String(chargingStatus.connectedSSID) + "</p>";
  html += "<a href='/status'>Ver Status</a>";
  html += "</body></html>";
  
  chargingServer.send(200, "text/html", html);
}

void handleChargingStatus() {
  String html = "<!DOCTYPE html><html><head><title>Status</title></head><body>";
  html += "<h1>Status Tempo Real</h1>";
  html += "<div id='status'>Carregando...</div>";
  html += "<script>";
  html += "function update() {";
  html += "  fetch('/api/status').then(r=>r.json()).then(d=>{";
  html += "    document.getElementById('status').innerHTML = ";
  html += "    'Bateria: ' + d.battery + '%<br>' +";
  html += "    'IP: ' + d.ip + '<br>' +";
  html += "    'Uptime: ' + d.uptime + 's';";
  html += "  });";
  html += "}";
  html += "update(); setInterval(update, 5000);";
  html += "</script>";
  html += "</body></html>";
  
  chargingServer.send(200, "text/html", html);
}

void handleApiStatus() {
  String json = "{";
  json += "\"battery\":" + String(getBatteryLevel(), 1) + ",";
  json += "\"ip\":\"" + String(chargingStatus.ipAddress) + "\",";
  json += "\"uptime\":" + String(millis()/1000) + ",";
  json += "\"ssid\":\"" + String(chargingStatus.connectedSSID) + "\"";
  json += "}";
  
  chargingServer.send(200, "application/json", json);
}

// Stubs para funções não implementadas
void handleChargingConfig() {}
void handleChargingData() {}
void handleChargingDownload() {}
void handleChargingUpload() {}
void handleApiConfig() {}
void handleApiData() {}