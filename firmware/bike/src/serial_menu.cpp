#include "serial_menu.h"
#include "config.h"
#include "wifi_scanner.h"
#include "firebase.h"
#include "status_tracker.h"
#include <LittleFS.h>
#include <WiFi.h>
#include <Arduino.h>

void showMenu() {
  Serial.println("\n=== MENU ===");
  Serial.println("1) Monitorar redes");
  Serial.println("2) Verificar conexao com base");
  Serial.println("3) Testar conexao Firebase");
  Serial.println("4) Mostrar configuracoes");
  Serial.println("5) Ver dados salvos");
  Serial.println("6) Transferir dados (copy/paste)");
  Serial.println("7) Upload status (conexoes/bateria)");
  Serial.println("8) Ativar modo AP/Configuracao");
  Serial.println("9) Ver logs de debug");
  Serial.println("0) Testar sincronizacao NTP");
  Serial.println("q) Sair do menu");
  Serial.print("Escolha: ");
}

void handleSerialMenu() {
  if (Serial.available()) {
    char choice = Serial.read();
    Serial.println(choice);
    
    switch(choice) {
      case '1':
        Serial.println("\n=== REDES DETECTADAS ===");
        scanWiFiNetworks();
        for (int i = 0; i < networkCount; i++) {
          Serial.printf("%s | %d dBm | Canal %d\n", networks[i].ssid, networks[i].rssi, networks[i].channel);
        }
        showMenu();
        break;
        
      case '2':
        Serial.println("\n=== TESTE CONEXAO BASE ===");
        scanWiFiNetworks();
        if (checkAtBase()) {
          Serial.println("Base WiFi detectada!");
          if (connectToBase()) {
            Serial.println("Conectado com sucesso!");
            Serial.println("IP: " + WiFi.localIP().toString());
            WiFi.disconnect();
          } else {
            Serial.println("Falha na conexao");
          }
        } else {
          Serial.println("Nenhuma base WiFi detectada");
        }
        showMenu();
        break;
        
      case '3':
        Serial.println("\n=== TESTE FIREBASE ===");
        if (strlen(config.firebaseUrl) == 0) {
          Serial.println("Firebase nao configurado");
        } else {
          Serial.printf("URL: %s\n", config.firebaseUrl);
          Serial.printf("Key: %s...\n", String(config.firebaseKey).substring(0, 10).c_str());
          
          if (connectToBase()) {
            Serial.println("Conectado! Testando upload...");
            dataCount = 1;
            uploadData();
          } else {
            Serial.println("Falha ao conectar na base para teste");
          }
        }
        showMenu();
        break;
        
      case '4':
        Serial.println("\n=== CONFIGURACOES ===");
        Serial.printf("Bike ID: %s\n", config.bikeId);
        Serial.printf("Modo de Coleta: %s\n", config.collectMode);
        Serial.printf("Scan Ativo: %d ms\n", config.scanTimeActive);
        Serial.printf("Scan Inativo: %d ms\n", config.scanTimeInactive);
        Serial.printf("Base 1: '%s' / '%s'\n", config.baseSSID1, config.basePassword1);
        Serial.printf("Base 2: '%s' / '%s'\n", config.baseSSID2, config.basePassword2);
        Serial.printf("Base 3: '%s' / '%s'\n", config.baseSSID3, config.basePassword3);
        Serial.printf("RSSI Proximidade: %d dBm\n", config.baseProximityRssi);
        Serial.printf("Firebase URL: %s\n", config.firebaseUrl);
        Serial.printf("Firebase Key: %s...\n", String(config.firebaseKey).substring(0, 15).c_str());
        
        // Status NTP
        {
          extern bool timeSync;
          extern unsigned long currentRealTime;
          Serial.printf("\n--- STATUS NTP ---\n");
          Serial.printf("Sincronizado: %s\n", timeSync ? "SIM" : "NAO");
          if (timeSync && currentRealTime > 0) {
            Serial.printf("Tempo atual: %lu (epoch)\n", currentRealTime);
            struct tm* timeinfo = localtime((time_t*)&currentRealTime);
            Serial.printf("Data/Hora: %04d-%02d-%02d %02d:%02d:%02d\n",
                         timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                         timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
          }
          
          // Verificar arquivo de estado NTP
          File ntpFile = LittleFS.open("/ntp_sync.txt", "r");
          if (ntpFile) {
            String data = ntpFile.readString();
            ntpFile.close();
            Serial.printf("Estado salvo: %s\n", data.c_str());
          } else {
            Serial.println("Nenhum estado NTP salvo");
          }
        }
        
        showMenu();
        break;
        
      case '5':
        Serial.println("\n=== DADOS SALVOS ===");
        {
          File root = LittleFS.open("/");
          File file = root.openNextFile();
          int count = 0;
          while (file && count < 5) {
            String fileName = file.name();
            if (fileName.startsWith("/scan_")) {
              count++;
              Serial.printf("Arquivo: %s (%d bytes)\n", fileName.c_str(), file.size());
              file.close();
              File dataFile = LittleFS.open(fileName.c_str(), "r");
              if (dataFile) {
                String content = dataFile.readString();
                dataFile.close();
                Serial.printf("Conteúdo: %s\n", content.c_str());
              }
              Serial.println("---");
              yield(); // Permite outras tarefas
            } else {
              file.close();
            }
            file = root.openNextFile();
          }
          root.close();
          if (count >= 5) {
            Serial.println("(mostrando apenas os primeiros 5 arquivos)");
          }
          if (count == 0) {
            Serial.println("Nenhum arquivo de dados encontrado");
          }
        }
        showMenu();
        break;
        
      case '6':
        Serial.println("\n=== TRANSFERIR DADOS ===");
        Serial.println("Copie os dados abaixo e cole em um arquivo:");
        Serial.println("--- INICIO DOS DADOS ---");
        {
          File root = LittleFS.open("/");
          Serial.println("{");
          Serial.println("\"bikeId\":\"" + String(config.bikeId) + "\",");
          Serial.println("\"exportTime\":" + String(millis()) + ",");
          Serial.println("\"scans\":[");
          
          bool first = true;
          File file = root.openNextFile();
          while (file) {
            String fileName = file.name();
            if (fileName.startsWith("/scan_")) {
              if (!first) Serial.println(",");
              file.close();
              File dataFile = LittleFS.open(fileName.c_str(), "r");
              if (dataFile) {
                String content = dataFile.readString();
                dataFile.close();
                Serial.print(content);
              }
              first = false;
              yield(); // Permite outras tarefas
            } else {
              file.close();
            }
            file = root.openNextFile();
          }
          root.close();
          Serial.println("");
          Serial.println("]}");
        }
        Serial.println("--- FIM DOS DADOS ---");
        Serial.println("Dados prontos para backup!");
        showMenu();
        break;
        
      case '7':
        Serial.println("\n=== UPLOAD STATUS ===");
        if (connectToBase()) {
          uploadStatus();
          WiFi.disconnect();
        } else {
          Serial.println("Falha ao conectar na base");
        }
        showMenu();
        break;
        
      case '8':
        Serial.println("\n=== ATIVANDO MODO AP ===");
        Serial.println("Reiniciando em modo configuracao...");
        delay(1000);
        ESP.restart();
        break;
        
      case '9':
        Serial.println("\n=== LOGS DE DEBUG ===");
        {
          File debugFile = LittleFS.open("/debug.log", "r");
          if (debugFile) {
            Serial.println("Últimos logs de debug:");
            Serial.println("--- INICIO LOGS ---");
            while (debugFile.available()) {
              Serial.write(debugFile.read());
              yield();
            }
            debugFile.close();
            Serial.println("--- FIM LOGS ---");
          } else {
            Serial.println("Arquivo de debug não encontrado");
          }
        }
        showMenu();
        break;
        
      case '0':
        Serial.println("\n=== TESTE NTP ===");
        if (connectToBase()) {
          Serial.println("Conectado! Testando NTP...");
          
          extern bool timeSync;
          extern unsigned long currentRealTime;
          extern NTPClient timeClient;
          
          Serial.printf("Status NTP antes: %s\n", timeSync ? "SINCRONIZADO" : "NAO SINCRONIZADO");
          if (timeSync) {
            Serial.printf("Tempo atual: %lu (epoch)\n", currentRealTime);
            
            // Converter para data/hora legível
            struct tm* timeinfo = localtime((time_t*)&currentRealTime);
            Serial.printf("Data/Hora: %04d-%02d-%02d %02d:%02d:%02d\n",
                         timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                         timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
          }
          
          // Forçar nova sincronização
          Serial.println("Forçando nova sincronização...");
          timeSync = false;
          syncTime();
          
          Serial.printf("Status NTP depois: %s\n", timeSync ? "SINCRONIZADO" : "FALHOU");
          if (timeSync) {
            Serial.printf("Novo tempo: %lu (epoch)\n", currentRealTime);
            
            struct tm* timeinfo = localtime((time_t*)&currentRealTime);
            Serial.printf("Nova Data/Hora: %04d-%02d-%02d %02d:%02d:%02d\n",
                         timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                         timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
          }
          
          WiFi.disconnect();
        } else {
          Serial.println("Falha ao conectar na base para teste NTP");
        }
        showMenu();
        break;
        
      case 'q':
      case 'Q':
        Serial.println("Saindo do menu...");
        break;
        
      default:
        showMenu();
        break;
    }
  }
}