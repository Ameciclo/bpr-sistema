#include "charging_minimal.h"
#include "config.h"
#include <time.h>

// Declaração da função de bateria (implementada em power_management.cpp)
extern float getBatteryLevel();

#ifdef ESP8266
  #include <ESP8266WiFi.h>
#else
  #include <WiFi.h>
#endif

ChargingStatus chargingStatus;

bool detectCharging() {
  // Método simples: monitorar tensão crescente
  static float lastVoltage = 0;
  static unsigned long lastCheck = 0;
  
  if (millis() - lastCheck > 5000) {
    float currentVoltage = getBatteryLevel() / 100.0 * 4.2; // Aproximação baseada em %
    bool charging = (currentVoltage > lastVoltage + 0.05);
    
    lastVoltage = currentVoltage;
    lastCheck = millis();
    return charging;
  }
  
  return chargingStatus.isCharging;
}

void handleChargingMode() {
  if (!config.chargingModeEnabled) return;
  
  bool currentlyCharging = detectCharging();
  
  // Início do carregamento
  if (currentlyCharging && !chargingStatus.isCharging) {
    Serial.println("=== MODO CARREGAMENTO ATIVADO ===");
    chargingStatus.isCharging = true;
    connectToBaseForCharging();
  }
  
  // Fim do carregamento
  if (!currentlyCharging && chargingStatus.isCharging) {
    Serial.println("=== MODO CARREGAMENTO DESATIVADO ===");
    chargingStatus.isCharging = false;
    chargingStatus.isConnected = false;
    WiFi.disconnect();
  }
  
  // Durante carregamento: manter conexão
  if (chargingStatus.isCharging) {
    if (!chargingStatus.isConnected && WiFi.status() != WL_CONNECTED) {
      connectToBaseForCharging();
    }
    
    // Update periódico simples
    if (millis() - chargingStatus.lastUpdate > config.chargingUpdateIntervalSeconds * 1000) {
      Serial.printf("Carregando - Bat: %.1f%% - IP: %s\n", 
                    getBatteryLevel(), chargingStatus.ipAddress);
      chargingStatus.lastUpdate = millis();
    }
  }
}

void connectToBaseForCharging() {
  Serial.println("Conectando para carregamento...");
  
  const char* bases[][2] = {
    {config.baseSSID1, config.basePassword1},
    {config.baseSSID2, config.basePassword2},
    {config.baseSSID3, config.basePassword3}
  };
  
  for (int i = 0; i < 3; i++) {
    if (strlen(bases[i][0]) > 0) {
      Serial.printf("Tentando: %s\n", bases[i][0]);
      WiFi.begin(bases[i][0], bases[i][1]);
      
      int attempts = 0;
      while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
      }
      
      if (WiFi.status() == WL_CONNECTED) {
        chargingStatus.isConnected = true;
        strcpy(chargingStatus.connectedSSID, bases[i][0]);
        strcpy(chargingStatus.ipAddress, WiFi.localIP().toString().c_str());
        
        Serial.printf("Conectado: %s (IP: %s)\n", 
                     chargingStatus.connectedSSID, 
                     chargingStatus.ipAddress);
        return;
      }
    }
  }
  
  Serial.println("Falha ao conectar");
}