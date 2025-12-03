#include "firebase.h"
#include "config.h"
#include "wifi_scanner.h"
#include "status_tracker.h"
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <Arduino.h>
#include <time.h>

void loadNTPState() {
  extern unsigned long currentRealTime;
  
  File ntpFile = LittleFS.open("/ntp_sync.txt", "r");
  if (ntpFile) {
    String data = ntpFile.readString();
    ntpFile.close();
    
    int comma1 = data.indexOf(',');
    int comma2 = data.indexOf(',', comma1 + 1);
    
    if (comma1 > 0 && comma2 > 0) {
      unsigned long lastSync = data.substring(0, comma1).toInt();
      unsigned long bootTime = data.substring(comma1 + 1, comma2).toInt();
      bool lastSuccess = data.substring(comma2 + 1).toInt();
      
      if (lastSuccess && lastSync > 1600000000) { // Validar timestamp (após 2020)
        currentRealTime = lastSync + (millis() - bootTime) / 1000;
        timeSync = true; // ✅ CORREÇÃO: Definir timeSync como true
        Serial.printf("🕰️ Horário recuperado: %lu (aprox) - timeSync ativado\n", currentRealTime);
      } else {
        Serial.println("⚠️ Timestamp inválido no arquivo NTP");
        currentRealTime = 0;
        timeSync = false;
      }
    }
  } else {
    Serial.println("🕰️ Nenhum estado NTP salvo");
    currentRealTime = 0;
  }
}

void saveNTPState(bool success) {
  extern unsigned long currentRealTime;
  
  File ntpFile = LittleFS.open("/ntp_sync.txt", "w");
  if (ntpFile) {
    ntpFile.printf("%lu,%lu,%d", 
                   success ? currentRealTime : 0,
                   millis(),
                   success ? 1 : 0);
    ntpFile.close();
  }
}

bool needsNTPSync() {
  extern unsigned long currentRealTime;
  
  File ntpFile = LittleFS.open("/ntp_sync.txt", "r");
  if (!ntpFile) return true;
  
  String data = ntpFile.readString();
  ntpFile.close();
  
  int comma1 = data.indexOf(',');
  if (comma1 > 0) {
    unsigned long lastSync = data.substring(0, comma1).toInt();
    if (lastSync < 1600000000) return true; // Timestamp inválido
    
    unsigned long now = timeSync ? currentRealTime : (lastSync + millis()/1000);
    unsigned long hoursSince = (now - lastSync) / 3600;
    return hoursSince >= config.ntpSyncIntervalHours;
  }
  return true;
}

void syncTime() {
  extern NTPClient timeClient;
  extern unsigned long currentRealTime;
  
  if (!timeSync || needsNTPSync()) {
    Serial.println("🕰️ Sincronizando horário NTP...");
    timeClient.begin();
    
    int attempts = 0;
    while (!timeClient.update() && attempts < 3) {
      Serial.printf("🔄 Tentativa NTP %d/3...\n", attempts + 1);
      delay(1000);
      attempts++;
    }
    
    if (timeClient.isTimeSet()) {
      timeSync = true;
      currentRealTime = timeClient.getEpochTime();
      saveNTPState(true);
      Serial.printf("✅ NTP OK! Epoch: %lu\n", currentRealTime);
    } else {
      saveNTPState(false);
      if (!timeSync) loadNTPState();
      Serial.println("⚠️ NTP falhou - usando horário aproximado");
    }
  }
}

String generateSessionId() {
  extern unsigned long currentRealTime;
  unsigned long now = timeSync ? currentRealTime : millis();
  // Formato mais legível: YYYYMMDD_HHMMSS_XXX
  if (timeSync && currentRealTime > 0) {
    struct tm* timeinfo = localtime((time_t*)&now);
    char buffer[20];
    sprintf(buffer, "%04d%02d%02d_%02d%02d%02d_%03d", 
            timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
            timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec,
            random(100, 999));
    return String(buffer);
  } else {
    return String(now / 1000) + "_" + String(random(100, 999));
  }
}

String expandScanWithLabels(String compactScan) {
  // Converte [timestamp,realTime,batteryLevel,[[ssid,bssid,rssi,channel]]] 
  // Para {"timestamp":X,"realTime":Y,"battery":Z,"networks":[{"ssid":"X","bssid":"Y","rssi":W,"channel":V}]}
  
  // Parse do formato compacto
  int firstBracket = compactScan.indexOf('[');
  int firstComma = compactScan.indexOf(',', firstBracket);
  int secondComma = compactScan.indexOf(',', firstComma + 1);
  int thirdComma = compactScan.indexOf(',', secondComma + 1);
  
  if (firstBracket == -1 || firstComma == -1 || secondComma == -1) {
    return compactScan; // Retorna original se não conseguir parsear
  }
  
  String timestamp = compactScan.substring(firstBracket + 1, firstComma);
  String realTime = compactScan.substring(firstComma + 1, secondComma);
  
  String result = "{";
  result += "\"timestamp\":" + timestamp + ",";
  result += "\"realTime\":" + realTime + ",";
  
  // Incluir bateria se disponível (novo formato)
  if (thirdComma != -1) {
    String battery = compactScan.substring(secondComma + 1, thirdComma);
    result += "\"battery\":" + battery + ",";
    
    // Extrair networks array após bateria
    int networksStart = compactScan.indexOf('[', thirdComma);
    int networksEnd = compactScan.lastIndexOf(']');
    
    result += "\"networks\":[";
    
    if (networksStart != -1 && networksEnd != -1) {
      String networksStr = compactScan.substring(networksStart + 1, networksEnd - 1);
      
      // Parse cada network ["ssid","bssid",rssi,channel]
      int pos = 0;
      bool first = true;
      while (pos < networksStr.length()) {
        int netStart = networksStr.indexOf('[', pos);
        if (netStart == -1) break;
        
        int netEnd = networksStr.indexOf(']', netStart);
        if (netEnd == -1) break;
        
        String network = networksStr.substring(netStart + 1, netEnd);
        
        // Parse ["ssid","bssid",rssi,channel]
        int c1 = network.indexOf(',');
        int c2 = network.indexOf(',', c1 + 1);
        int c3 = network.indexOf(',', c2 + 1);
        
        if (c1 != -1 && c2 != -1 && c3 != -1) {
          if (!first) result += ",";
          
          String ssid = network.substring(0, c1);
          String bssid = network.substring(c1 + 1, c2);
          String rssi = network.substring(c2 + 1, c3);
          String channel = network.substring(c3 + 1);
          
          result += "{";
          result += "\"ssid\":" + ssid + ",";
          result += "\"bssid\":" + bssid + ",";
          result += "\"rssi\":" + rssi + ",";
          result += "\"channel\":" + channel;
          result += "}";
          
          first = false;
        }
        
        pos = netEnd + 1;
      }
    }
  } else {
    // Formato antigo sem bateria - manter compatibilidade
    result += "\"networks\":[";
    
    int networksStart = compactScan.indexOf('[', secondComma);
    int networksEnd = compactScan.lastIndexOf(']');
    
    if (networksStart != -1 && networksEnd != -1) {
      String networksStr = compactScan.substring(networksStart + 1, networksEnd - 1);
      
      int pos = 0;
      bool first = true;
      while (pos < networksStr.length()) {
        int netStart = networksStr.indexOf('[', pos);
        if (netStart == -1) break;
        
        int netEnd = networksStr.indexOf(']', netStart);
        if (netEnd == -1) break;
        
        String network = networksStr.substring(netStart + 1, netEnd);
        
        int c1 = network.indexOf(',');
        int c2 = network.indexOf(',', c1 + 1);
        int c3 = network.indexOf(',', c2 + 1);
        
        if (c1 != -1 && c2 != -1 && c3 != -1) {
          if (!first) result += ",";
          
          String ssid = network.substring(0, c1);
          String bssid = network.substring(c1 + 1, c2);
          String rssi = network.substring(c2 + 1, c3);
          String channel = network.substring(c3 + 1);
          
          result += "{";
          result += "\"ssid\":" + ssid + ",";
          result += "\"bssid\":" + bssid + ",";
          result += "\"rssi\":" + rssi + ",";
          result += "\"channel\":" + channel;
          result += "}";
          
          first = false;
        }
        
        pos = netEnd + 1;
      }
    }
  }
  
  result += "]}";
  return result;
}

String buildOptimizedPayload(int batchNumber, int filesPerBatch) {
  extern unsigned long currentRealTime;
  unsigned long now = timeSync ? currentRealTime : millis();
  
  Serial.printf("📦 Construindo batch %d (max %d arquivos)...\n", batchNumber, filesPerBatch);
  
  // Ler arquivos de scan em lotes
  File root = LittleFS.open("/");
  if (!root) {
    Serial.println("❌ Falha ao abrir diretório raiz");
    return "{}";
  }
  
  String scans = "";
  int scanCount = 0;
  int skipCount = batchNumber * filesPerBatch;
  int currentFile = 0;
  
  File file = root.openNextFile();
  while (file && scanCount < filesPerBatch) {
    String fileName = file.name();
    if (fileName.startsWith("scan_") || fileName.startsWith("/scan_")) {
      if (currentFile >= skipCount) {
        String content = file.readString();
        if (content.length() > 0) {
          if (scans.length() > 0) scans += ",";
          scans += expandScanWithLabels(content);
          scanCount++;
          Serial.printf("📄 Batch %d - Arquivo %d: %s\n", batchNumber, scanCount, fileName.c_str());
        }
      }
      currentFile++;
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
  
  if (scanCount == 0) {
    Serial.printf("⚠️ Batch %d vazio\n", batchNumber);
    return "{}";
  }
  
  // Construir payload otimizado
  String payload = "{";
  payload += "\"start\":" + String(now - (scanCount * 30));
  payload += ",\"end\":" + String(now);
  payload += ",\"mode\":\"" + String(config.collectMode) + "\"";
  payload += ",\"batch\":" + String(batchNumber);
  payload += ",\"scans\":[" + scans + "]";
  payload += "}";
  
  Serial.printf("📦 Batch %d: %d scans, %d bytes\n", batchNumber, scanCount, payload.length());
  return payload;
}

bool uploadBatch(String sessionId, int batchNumber, int filesPerBatch) {
  String payload = buildOptimizedPayload(batchNumber, filesPerBatch);
  
  if (payload == "{}") {
    return false; // Batch vazio
  }
  
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(10000);
  
  String url = String(config.firebaseUrl);
  url.replace("https://", "");
  url.replace("http://", "");
  int slashIndex = url.indexOf('/');
  String host = url.substring(0, slashIndex);
  
  String path = "/bikes/" + String(config.bikeId) + "/sessions/" + sessionId + "_batch" + String(batchNumber) + ".json";
  
  Serial.printf("🔗 Enviando batch %d...\n", batchNumber);
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
      Serial.printf("✅ Batch %d enviado!\n", batchNumber);
      return true;
    } else {
      Serial.printf("❌ Batch %d falhou\n", batchNumber);
      return false;
    }
  } else {
    Serial.printf("❌ Conexão batch %d falhou\n", batchNumber);
    return false;
  }
}

void uploadCheckIn() {
  if (strlen(config.firebaseUrl) == 0) {
    Serial.println("⚠️ Firebase não configurado");
    return;
  }

  extern unsigned long currentRealTime;
  unsigned long now = timeSync ? currentRealTime : millis();
  
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(10000);
  
  String url = String(config.firebaseUrl);
  url.replace("https://", "");
  url.replace("http://", "");
  int slashIndex = url.indexOf('/');
  String host = url.substring(0, slashIndex);
  
  String path = "/bikes/" + String(config.bikeId) + "/checkins/" + String(now) + ".json";
  
  String payload = "{";
  payload += "\"timestamp\":" + String(now);
  payload += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  payload += ",\"ssid\":\"" + WiFi.SSID() + "\"";
  payload += ",\"rssi\":" + String(WiFi.RSSI());
  payload += ",\"battery\":" + String(getBatteryLevel());
  payload += "}";
  
  Serial.println("📍 Enviando check-in...");
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
      Serial.println("✅ Check-in enviado!");
    } else {
      Serial.println("❌ Check-in falhou");
    }
  } else {
    Serial.println("❌ Conexão check-in falhou");
  }
}

void uploadSessionData() {
  if (strlen(config.firebaseUrl) == 0) {
    Serial.println("⚠️ Firebase não configurado");
    return;
  }
  
  if (dataCount == 0) {
    Serial.println("⚠️ Nenhum dado para upload");
    return;
  }

  Serial.println("=== UPLOAD SESSÃO COMPLETA ===");
  Serial.printf("📦 Total de arquivos: %d\n", dataCount);
  
  String sessionId = generateSessionId();
  Serial.printf("📝 Sessão ID: %s\n", sessionId.c_str());
  
  // Construir sessão completa com todos os dados
  String sessionPayload = buildCompleteSession();
  
  if (sessionPayload.length() == 0) {
    Serial.println("⚠️ Falha ao construir sessão");
    return;
  }
  
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);
  
  String url = String(config.firebaseUrl);
  url.replace("https://", "");
  url.replace("http://", "");
  int slashIndex = url.indexOf('/');
  String host = url.substring(0, slashIndex);
  
  String path = "/bikes/" + String(config.bikeId) + "/sessions/" + sessionId + ".json";
  
  Serial.println("🚀 Enviando sessão completa...");
  if (client.connect(host.c_str(), 443)) {
    String request = "PUT " + path + " HTTP/1.1\r\n";
    request += "Host: " + host + "\r\n";
    request += "Content-Type: application/json\r\n";
    request += "Content-Length: " + String(sessionPayload.length()) + "\r\n";
    request += "Connection: close\r\n\r\n";
    
    client.print(request);
    client.print(sessionPayload);
    
    unsigned long startTime = millis();
    String response = "";
    while (client.connected() && millis() - startTime < 15000) {
      if (client.available()) {
        response += client.readString();
        break;
      }
      delay(10);
    }
    
    client.stop();
    
    if (response.indexOf("200 OK") >= 0) {
      Serial.println("✅ Sessão enviada com sucesso!");
      
      if (config.cleanupEnabled) {
        Serial.println("🧹 Limpando arquivos da sessão...");
        cleanupSessionFiles();
      }
    } else {
      Serial.println("❌ Upload da sessão falhou");
    }
  } else {
    Serial.println("❌ Conexão da sessão falhou");
  }
}

String buildCompleteSession() {
  extern unsigned long currentRealTime;
  unsigned long now = timeSync ? currentRealTime : millis();
  
  Serial.println("📝 Construindo sessão completa...");
  
  File root = LittleFS.open("/");
  if (!root) {
    Serial.println("❌ Falha ao abrir diretório raiz");
    return "";
  }
  
  String scans = "";
  int scanCount = 0;
  unsigned long firstTimestamp = 0;
  unsigned long lastTimestamp = 0;
  
  File file = root.openNextFile();
  while (file) {
    String fileName = file.name();
    if (fileName.startsWith("scan_") || fileName.startsWith("/scan_")) {
      String content = file.readString();
      if (content.length() > 0) {
        if (scans.length() > 0) scans += ",";
        scans += expandScanWithLabels(content);
        scanCount++;
        
            // Extrair timestamp para determinar início/fim da sessão
        int firstBracket = content.indexOf('[');
        int firstComma = content.indexOf(',', firstBracket);
        if (firstBracket != -1 && firstComma != -1) {
          unsigned long timestamp = content.substring(firstBracket + 1, firstComma).toInt();
          if (firstTimestamp == 0 || timestamp < firstTimestamp) {
            firstTimestamp = timestamp;
          }
          if (timestamp > lastTimestamp) {
            lastTimestamp = timestamp;
          }
        }
        
        Serial.printf("📄 Processando: %s\n", fileName.c_str());
      }
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
  
  if (scanCount == 0) {
    Serial.println("⚠️ Nenhum scan encontrado");
    return "";
  }
  
  // Usar timestamps reais se disponíveis, senão estimar
  if (firstTimestamp == 0) firstTimestamp = now - (scanCount * 30);
  if (lastTimestamp == 0) lastTimestamp = now;
  
  String payload = "{";
  payload += "\"start\":" + String(firstTimestamp);
  payload += ",\"end\":" + String(lastTimestamp);
  payload += ",\"mode\":\"" + String(config.collectMode) + "\"";
  payload += ",\"totalScans\":" + String(scanCount);
  payload += ",\"scans\":[" + scans + "]";
  payload += "}";
  
  Serial.printf("📦 Sessão: %d scans, %d bytes\n", scanCount, payload.length());
  return payload;
}

void cleanupSessionFiles() {
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  int removed = 0;
  
  while (file) {
    String fileName = file.name();
    if (fileName.startsWith("/scan_") || fileName.startsWith("scan_")) {
      file.close();
      if (LittleFS.remove(fileName)) {
        removed++;
      }
    } else {
      file.close();
    }
    file = root.openNextFile();
  }
  root.close();
  
  Serial.printf("🧹 %d arquivos removidos\n", removed);
  dataCount = 0;
}

void uploadLowBatteryAlert() {
  if (strlen(config.firebaseUrl) == 0) {
    Serial.println("⚠️ Firebase não configurado");
    return;
  }

  extern unsigned long currentRealTime;
  unsigned long now = timeSync ? currentRealTime : millis();
  float batteryLevel = getBatteryLevel();
  bool isCritical = batteryLevel <= config.batteryCriticalThreshold;
  
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(10000);
  
  String url = String(config.firebaseUrl);
  url.replace("https://", "");
  url.replace("http://", "");
  int slashIndex = url.indexOf('/');
  String host = url.substring(0, slashIndex);
  
  String path = "/bikes/" + String(config.bikeId) + "/alerts/" + String(now) + ".json";
  
  String payload = "{";
  payload += "\"type\":\"low_battery\"";
  payload += ",\"level\":" + String(batteryLevel, 1);
  payload += ",\"critical\":" + String(isCritical ? "true" : "false");
  payload += ",\"threshold\":" + String(config.batteryLowThreshold, 1);
  payload += ",\"base\":\"" + WiFi.SSID() + "\"";
  payload += ",\"timestamp\":" + String(now);
  payload += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  payload += "}";
  
  Serial.printf("🚨 Enviando alerta bateria baixa: %.1f%%\n", batteryLevel);
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
      Serial.println("✅ Alerta de bateria enviado!");
      
      // Salvar timestamp do último alerta
      File alertFile = LittleFS.open("/last_battery_alert.txt", "w");
      if (alertFile) {
        alertFile.print(millis() / 1000);
        alertFile.close();
      }
    } else {
      Serial.println("❌ Alerta de bateria falhou");
    }
  } else {
    Serial.println("❌ Conexão do alerta falhou");
  }
}

void uploadScheduledStatus() {
  if (strlen(config.firebaseUrl) == 0) {
    Serial.println("⚠️ Firebase não configurado");
    return;
  }

  extern unsigned long currentRealTime;
  unsigned long now = timeSync ? currentRealTime : millis();
  float batteryLevel = getBatteryLevel();
  
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(10000);
  
  String url = String(config.firebaseUrl);
  url.replace("https://", "");
  url.replace("http://", "");
  int slashIndex = url.indexOf('/');
  String host = url.substring(0, slashIndex);
  
  String path = "/bikes/" + String(config.bikeId) + "/status/" + String(now) + ".json";
  
  String payload = "{";
  payload += "\"timestamp\":" + String(now);
  payload += ",\"battery\":" + String(batteryLevel, 1);
  payload += ",\"uptime\":" + String(millis() / 1000);
  payload += ",\"mode\":\"" + String(config.collectMode) + "\"";
  payload += ",\"dataFiles\":" + String(dataCount);
  payload += ",\"freeHeap\":" + String(ESP.getFreeHeap());
  if (config.isAtBase) {
    payload += ",\"location\":\"base\"";
    payload += ",\"ssid\":\"" + WiFi.SSID() + "\"";
    payload += ",\"rssi\":" + String(WiFi.RSSI());
  } else {
    payload += ",\"location\":\"mobile\"";
  }
  payload += "}";
  
  Serial.printf("📈 Enviando status programado: %.1f%% bateria\n", batteryLevel);
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
      Serial.println("✅ Status programado enviado!");
      
      // Salvar timestamp da última atualização
      File statusFile = LittleFS.open("/last_status_update.txt", "w");
      if (statusFile) {
        statusFile.print(millis() / 1000);
        statusFile.close();
      }
    } else {
      Serial.println("❌ Status programado falhou");
    }
  } else {
    Serial.println("❌ Conexão do status falhou");
  }
}

void uploadData() {
  // Usar nova estrutura de sessão
  uploadSessionData();
}