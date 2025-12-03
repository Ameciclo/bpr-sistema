#ifndef POWER_MANAGEMENT_H
#define POWER_MANAGEMENT_H

#include <Arduino.h>
#include <WiFi.h>
#include <soc/rtc_cntl_reg.h>

void setupPowerManagement() {
  // CPU para frequência baixa (economia de energia)
  setCpuFrequencyMhz(40);
  
  // CRÍTICO: Potência mínima do WiFi - reduz drasticamente o consumo
  WiFi.setTxPower(WIFI_POWER_MINUS_1dBm);
  
  // WiFi em modo de economia máxima
  WiFi.setSleep(WIFI_PS_MAX_MODEM);
  
  // Brownout será desabilitado no main após inicialização
  
  // Desabilitar Bluetooth
  btStop();
  
  // Configurar ADC para bateria
  pinMode(A0, INPUT);
  
  // Configurar pinos não utilizados
  for (int i = 1; i < 22; i++) {
    if (i != 8 && i != 9 && i != 0) {
      pinMode(i, INPUT_PULLUP);
    }
  }
  
  Serial.println("🔋 Modo bateria otimizado: WiFi -1dBm, Brownout OFF, 40MHz");
}

void enterDeepSleep(uint32_t sleepTimeMs) {
  Serial.printf("💤 Deep sleep por %d segundos\n", sleepTimeMs/1000);
  Serial.flush();
  
  // Desligar WiFi completamente
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  
  // Sem delays longos - processamento eficiente
  esp_sleep_enable_timer_wakeup(sleepTimeMs * 1000);
  esp_deep_sleep_start();
}

void enterLightSleep(uint32_t sleepTimeMs) {
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  delay(100);
  
  esp_sleep_enable_timer_wakeup(sleepTimeMs * 1000);
  esp_light_sleep_start();
  
  WiFi.mode(WIFI_STA);
}

// Verificar se bateria está crítica
bool isBatteryCritical() {
  float battery = getBatteryLevel();
  return battery < 10.0;
}

// Modo de emergência para bateria baixa
void emergencyPowerMode() {
  Serial.println("🚨 BATERIA CRÍTICA - Modo emergência");
  
  // LED rápido para indicar emergência
  for(int i = 0; i < 5; i++) {
    digitalWrite(8, LOW);
    delay(50);
    digitalWrite(8, HIGH);
    delay(50);
  }
  
  // Deep sleep por 10 minutos
  enterDeepSleep(600000);
}

#endif