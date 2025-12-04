#include "power_manager.h"
#include "bike_config.h"
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <esp_bt.h>

PowerManager::PowerManager() : bootTime(0), lastSleepTime(0) {}

void PowerManager::init() {
  bootTime = millis();
  
  // Configurações de economia de energia
  setCpuFrequencyMhz(80); // Frequência reduzida
  
  // Configurar wake-up por timer
  esp_sleep_enable_timer_wakeup(60 * 1000000ULL); // 1 minuto padrão
  
  // Configurar wake-up por GPIO (botão)
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_9, 0); // BOOT button
  
  Serial.println("⚡ Power Manager inicializado");
}

void PowerManager::enterLightSleep(uint32_t seconds) {
  Serial.printf("😴 Light sleep por %d segundos\n", seconds);
  
  // Desabilitar WiFi se ativo
  WiFi.mode(WIFI_OFF);
  
  // Configurar timer
  esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);
  
  lastSleepTime = millis();
  esp_light_sleep_start();
  
  Serial.printf("⏰ Acordou após %d ms\n", millis() - lastSleepTime);
}

void PowerManager::enterDeepSleep(uint32_t seconds) {
  Serial.printf("💤 Deep sleep por %d segundos\n", seconds);
  Serial.flush();
  
  // Desabilitar tudo
  WiFi.mode(WIFI_OFF);
  esp_bt_controller_disable();
  
  // Configurar timer
  esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);
  
  esp_deep_sleep_start();
  // Não retorna - reinicia após wake-up
}

void PowerManager::optimizeForScanning() {
  // Configurações para modo scanning
  setCpuFrequencyMhz(160); // Frequência normal para WiFi
  WiFi.setTxPower(WIFI_POWER_7dBm); // Potência reduzida
  
  Serial.println("🔧 Otimizado para scanning");
}

void PowerManager::optimizeForBLE() {
  // Configurações para modo BLE
  setCpuFrequencyMhz(80); // Frequência reduzida
  WiFi.mode(WIFI_OFF); // Desligar WiFi completamente
  
  Serial.println("🔧 Otimizado para BLE");
}

uint32_t PowerManager::getUptimeSeconds() {
  return (millis() - bootTime) / 1000;
}

esp_sleep_wakeup_cause_t PowerManager::getWakeupCause() {
  return esp_sleep_get_wakeup_cause();
}

void PowerManager::printWakeupReason() {
  esp_sleep_wakeup_cause_t cause = getWakeupCause();
  
  switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("⏰ Wake-up: Timer");
      break;
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.println("🔘 Wake-up: Botão");
      break;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      Serial.println("🔄 Wake-up: Boot normal");
      break;
    default:
      Serial.printf("❓ Wake-up: Causa %d\n", cause);
      break;
  }
}