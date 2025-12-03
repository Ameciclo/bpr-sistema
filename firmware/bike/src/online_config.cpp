#include "online_config.h"
#include "config.h"
#include "wifi_scanner.h"
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

OnlineConfig onlineConfig;

// Declaração da função
bool parseConfigFromJson(String jsonStr);

bool initializeOnlineConfig() {
  Serial.println("🔧 Inicializando configuração online...");
  
  // Carregar configuração local básica primeiro
  loadConfig();
  
  // Copiar configurações básicas para estrutura online
  strcpy(onlineConfig.bikeId, config.bikeId);
  strcpy(onlineConfig.firebaseUrl, config.firebaseUrl);
  strcpy(onlineConfig.firebaseKey, config.firebaseKey);
  
  // Verificar se Firebase está configurado
  if (strlen(onlineConfig.firebaseUrl) == 0 || strlen(onlineConfig.firebaseKey) == 0) {
    Serial.println("❌ Firebase não configurado - usando config local");
    onlineConfig.configSynced = false;
    return false;
  }
  
  // Tentar conectar em uma das bases para sincronizar
  Serial.println("📡 Buscando base para sincronização...");
  scanWiFiNetworks();
  
  if (connectToBase()) {
    Serial.println("🌐 Conectado! Sincronizando configurações...");
    bool success = syncConfigFromFirebase();
    
    if (success) {
      Serial.println("✅ Configurações sincronizadas com sucesso!");
      applyOnlineConfig();
      uploadConfigStatus();
      onlineConfig.configSynced = true;
    } else {
      Serial.println("⚠️ Falha na sincronização - usando config local");
      onlineConfig.configSynced = false;
    }
    
    WiFi.disconnect();
    return success;
  } else {
    Serial.println("❌ Não foi possível conectar na base - usando config local");
    onlineConfig.configSynced = false;
    return false;
  }
}

bool syncConfigFromFirebase() {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(10000);
  
  String url = String(onlineConfig.firebaseUrl);
  url.replace("https://", "");
  url.replace("http://", "");
  int slashIndex = url.indexOf('/');
  String host = url.substring(0, slashIndex);
  
  String path = "/bikes/" + String(onlineConfig.bikeId) + "/config.json";
  
  Serial.println("📥 Baixando configurações do Firebase...");
  
  if (client.connect(host.c_str(), 443)) {
    String request = "GET " + path + " HTTP/1.1\r\n";
    request += "Host: " + host + "\r\n";
    request += "Connection: close\r\n\r\n";
    
    client.print(request);
    
    unsigned long startTime = millis();
    String response = "";
    bool headersPassed = false;
    
    while (client.connected() && millis() - startTime < 10000) {
      if (client.available()) {
        String line = client.readStringUntil('\n');
        
        if (!headersPassed) {
          if (line == "\r") {
            headersPassed = true;
          }
        } else {
          response += line;
        }
      }
      delay(10);
    }
    
    client.stop();
    
    if (response.length() > 0 && response != "null") {
      return parseConfigFromJson(response);
    } else {
      Serial.println("⚠️ Configuração não encontrada no Firebase");
      return false;
    }
  } else {
    Serial.println("❌ Falha ao conectar no Firebase");
    return false;
  }
}

bool parseConfigFromJson(String jsonStr) {
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, jsonStr);
  
  if (error) {
    Serial.println("❌ Erro ao parsear JSON de configuração");
    return false;
  }
  
  // Aplicar configurações do Firebase
  if (doc.containsKey("collectMode")) {
    strcpy(onlineConfig.collectMode, doc["collectMode"]);
  }
  
  if (doc.containsKey("scanTimeActive")) {
    onlineConfig.scanTimeActive = doc["scanTimeActive"];
  }
  
  if (doc.containsKey("scanTimeInactive")) {
    onlineConfig.scanTimeInactive = doc["scanTimeInactive"];
  }
  
  // Bases WiFi
  if (doc.containsKey("bases")) {
    JsonObject bases = doc["bases"];
    
    if (bases.containsKey("base1")) {
      JsonObject base1 = bases["base1"];
      if (base1.containsKey("ssid")) strcpy(onlineConfig.baseSSID1, base1["ssid"]);
      if (base1.containsKey("password")) strcpy(onlineConfig.basePassword1, base1["password"]);
    }
    
    if (bases.containsKey("base2")) {
      JsonObject base2 = bases["base2"];
      if (base2.containsKey("ssid")) strcpy(onlineConfig.baseSSID2, base2["ssid"]);
      if (base2.containsKey("password")) strcpy(onlineConfig.basePassword2, base2["password"]);
    }
    
    if (bases.containsKey("base3")) {
      JsonObject base3 = bases["base3"];
      if (base3.containsKey("ssid")) strcpy(onlineConfig.baseSSID3, base3["ssid"]);
      if (base3.containsKey("password")) strcpy(onlineConfig.basePassword3, base3["password"]);
    }
  }
  
  if (doc.containsKey("baseProximityRssi")) {
    onlineConfig.baseProximityRssi = doc["baseProximityRssi"];
  }
  
  if (doc.containsKey("wifiTxPower")) {
    onlineConfig.wifiTxPower = doc["wifiTxPower"];
  }
  
  if (doc.containsKey("cleanupEnabled")) {
    onlineConfig.cleanupEnabled = doc["cleanupEnabled"];
  }
  
  if (doc.containsKey("batteryLowThreshold")) {
    onlineConfig.batteryLowThreshold = doc["batteryLowThreshold"];
  }
  
  if (doc.containsKey("statusUpdateIntervalMinutes")) {
    onlineConfig.statusUpdateIntervalMinutes = doc["statusUpdateIntervalMinutes"];
  }
  
  onlineConfig.lastConfigSync = millis();
  
  Serial.println("✅ Configurações parseadas do Firebase");
  return true;
}

void applyOnlineConfig() {
  Serial.println("🔧 Aplicando configurações online...");
  
  // Aplicar ao config global
  strcpy(config.collectMode, onlineConfig.collectMode);
  config.scanTimeActive = onlineConfig.scanTimeActive;
  config.scanTimeInactive = onlineConfig.scanTimeInactive;
  
  strcpy(config.baseSSID1, onlineConfig.baseSSID1);
  strcpy(config.basePassword1, onlineConfig.basePassword1);
  strcpy(config.baseSSID2, onlineConfig.baseSSID2);
  strcpy(config.basePassword2, onlineConfig.basePassword2);
  strcpy(config.baseSSID3, onlineConfig.baseSSID3);
  strcpy(config.basePassword3, onlineConfig.basePassword3);
  
  config.baseProximityRssi = onlineConfig.baseProximityRssi;
  config.cleanupEnabled = onlineConfig.cleanupEnabled;
  config.batteryLowThreshold = onlineConfig.batteryLowThreshold;
  config.statusUpdateIntervalMinutes = onlineConfig.statusUpdateIntervalMinutes;
  
  // Aplicar intensidade WiFi
  if (onlineConfig.wifiTxPower >= -1 && onlineConfig.wifiTxPower <= 20) {
    WiFi.setTxPower((wifi_power_t)onlineConfig.wifiTxPower);
    Serial.printf("📶 Potência WiFi ajustada: %d dBm\n", onlineConfig.wifiTxPower);
  }
  
  Serial.printf("⚙️ Modo: %s (%d/%d ms)\n", 
                config.collectMode, config.scanTimeActive, config.scanTimeInactive);
  Serial.printf("📶 RSSI base: %d dBm\n", config.baseProximityRssi);
  Serial.printf("🔋 Bateria baixa: %.1f%%\n", config.batteryLowThreshold);
}

bool uploadConfigStatus() {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(8000);
  
  String url = String(onlineConfig.firebaseUrl);
  url.replace("https://", "");
  url.replace("http://", "");
  int slashIndex = url.indexOf('/');
  String host = url.substring(0, slashIndex);
  
  String path = "/bikes/" + String(onlineConfig.bikeId) + "/configStatus.json";
  
  String payload = "{";
  payload += "\"lastSync\":" + String(millis() / 1000);
  payload += ",\"synced\":" + String(onlineConfig.configSynced ? "true" : "false");
  payload += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  payload += ",\"ssid\":\"" + WiFi.SSID() + "\"";
  payload += ",\"rssi\":" + String(WiFi.RSSI());
  payload += ",\"version\":\"2.0\"";
  payload += "}";
  
  Serial.println("📤 Enviando status de configuração...");
  
  if (client.connect(host.c_str(), 443)) {
    String request = "PUT " + path + " HTTP/1.1\r\n";
    request += "Host: " + host + "\r\n";
    request += "Content-Type: application/json\r\n";
    request += "Content-Length: " + String(payload.length()) + "\r\n";
    request += "Connection: close\r\n\r\n";
    
    client.print(request);
    client.print(payload);
    
    unsigned long startTime = millis();
    String response = "";
    while (client.connected() && millis() - startTime < 8000) {
      if (client.available()) {
        response += client.readString();
        break;
      }
      delay(10);
    }
    
    client.stop();
    
    if (response.indexOf("200 OK") >= 0) {
      Serial.println("✅ Status de configuração enviado!");
      return true;
    } else {
      Serial.println("❌ Falha ao enviar status");
      return false;
    }
  } else {
    Serial.println("❌ Falha na conexão para status");
    return false;
  }
}

bool isConfigurationValid() {
  return onlineConfig.configSynced && 
         strlen(onlineConfig.baseSSID1) > 0 &&
         strlen(onlineConfig.firebaseUrl) > 0;
}