#include "status_tracker.h"
#include "config.h"
#include "firebase.h"
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <LittleFS.h>

ConnectionEvent connectionHistory[10];
int connectionCount = 0;
BatteryEvent batteryHistory[20];
int batteryCount = 0;
unsigned long lastBatteryCheck = 0;

void trackConnection(const char* baseSSID, const char* ip, bool connected) {
  if (connectionCount >= 10) {
    // Shift array para remover o mais antigo
    for (int i = 0; i < 9; i++) {
      connectionHistory[i] = connectionHistory[i + 1];
    }
    connectionCount = 9;
  }
  
  ConnectionEvent& event = connectionHistory[connectionCount];
  event.timestamp = timeSync ? timeClient.getEpochTime() : millis();
  strcpy(event.baseSSID, baseSSID);
  strcpy(event.ip, ip);
  event.connected = connected;
  connectionCount++;
  
  Serial.printf("Status: %s %s (IP: %s)\n", 
                connected ? "Conectado" : "Desconectado", 
                baseSSID, ip);
}

void trackBattery(float percentage) {
  unsigned long now = millis();
  
  // Só registra se mudou significativamente ou passou muito tempo
  if (batteryCount == 0 || 
      abs(percentage - batteryHistory[batteryCount-1].percentage) > 2.0 ||
      now - lastBatteryCheck > 300000) { // 5 minutos
    
    if (batteryCount >= 20) {
      // Shift array para remover o mais antigo
      for (int i = 0; i < 19; i++) {
        batteryHistory[i] = batteryHistory[i + 1];
      }
      batteryCount = 19;
    }
    
    BatteryEvent& event = batteryHistory[batteryCount];
    event.timestamp = timeSync ? timeClient.getEpochTime() : millis();
    event.percentage = percentage;
    batteryCount++;
    lastBatteryCheck = now;
  }
}

void uploadStatus() {
  if (strlen(config.firebaseUrl) == 0) {
    Serial.println("Firebase não configurado para status");
    return;
  }

  Serial.println("=== UPLOAD STATUS ===");
  
  unsigned long timestamp = timeSync ? timeClient.getEpochTime() : millis();
  
  String payload = "{\"bike\":\"" + String(config.bikeId) + "\"";
  payload += ",\"lastUpdate\":" + String(timestamp);
  
  // Histórico de conexões
  payload += ",\"connections\":[";
  for (int i = 0; i < connectionCount; i++) {
    if (i > 0) payload += ",";
    payload += "{\"time\":" + String(connectionHistory[i].timestamp);
    payload += ",\"base\":\"" + String(connectionHistory[i].baseSSID) + "\"";
    payload += ",\"ip\":\"" + String(connectionHistory[i].ip) + "\"";
    payload += ",\"event\":\"" + String(connectionHistory[i].connected ? "connect" : "disconnect") + "\"}";
  }
  payload += "]";
  
  // Histórico de bateria
  payload += ",\"battery\":[";
  for (int i = 0; i < batteryCount; i++) {
    if (i > 0) payload += ",";
    payload += "{\"time\":" + String(batteryHistory[i].timestamp);
    payload += ",\"level\":" + String(batteryHistory[i].percentage, 1) + "}";
  }
  payload += "]}";
  
  WiFiClientSecure client;
  client.setInsecure();
  
  String url = String(config.firebaseUrl);
  url.replace("https://", "");
  url.replace("http://", "");
  int slashIndex = url.indexOf('/');
  String host = url.substring(0, slashIndex);
  
  String path = "/bikes/" + String(config.bikeId) + "/status.json";
  
  if (client.connect(host.c_str(), 443)) {
    client.print("PUT " + path + " HTTP/1.1\r\n");
    client.print("Host: " + host + "\r\n");
    client.print("Content-Type: application/json\r\n");
    client.print("Content-Length: " + String(payload.length()) + "\r\n");
    client.print("Connection: close\r\n\r\n");
    client.print(payload);
    
    delay(500); // Reduzido para não bloquear muito
    String response = "";
    int timeout = 0;
    while (client.available() && timeout < 50) { // Timeout para não travar
      response += client.readString();
      delay(10);
      timeout++;
    }
    
    if (response.indexOf("200 OK") > 0) {
      Serial.println("Status upload OK!");
    } else {
      Serial.println("Erro no upload de status");
    }
    
    client.stop();
  }
}