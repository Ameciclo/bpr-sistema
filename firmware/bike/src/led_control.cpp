#include "led_control.h"
#include "config.h"
#include <Arduino.h>

#define LED_PIN 8 // ESP32-C3 LED no GPIO8

void updateLED() {
  unsigned long now = millis();
  
  if (configMode) {
    // Modo AP: LONGO-curto-curto bem rápido
    unsigned long intervals[] = {300, 100, 80, 80, 80, 80, 400}; // LONGO-OFF-curto-OFF-curto-OFF-PAUSA
    if (now - lastLedBlink > intervals[ledStep]) {
      ledState = (ledStep % 2 == 0) ? 1 : 0;
      digitalWrite(LED_PIN, ledState ? LOW : HIGH); // Active LOW
      lastLedBlink = now;
      ledStep = (ledStep + 1) % 7;
    }
  } else if (config.isAtBase) {
    // Conectado na base: 1 piscada lenta + pausa longa
    unsigned long intervals[] = {800, 800, 2000}; // ON-OFF-PAUSA
    if (now - lastLedBlink > intervals[ledStep]) {
      ledState = (ledStep == 0) ? 1 : 0;
      digitalWrite(LED_PIN, ledState ? LOW : HIGH); // Active LOW
      lastLedBlink = now;
      ledStep = (ledStep + 1) % 3;
    }
  } else {
    // Coletando dados: 2 piscadas rápidas + pausa média
    unsigned long intervals[] = {250, 250, 250, 250, 1000}; // ON-OFF-ON-OFF-PAUSA
    if (now - lastLedBlink > intervals[ledStep]) {
      ledState = (ledStep % 2 == 0) ? 1 : 0;
      digitalWrite(LED_PIN, ledState ? LOW : HIGH); // Active LOW
      lastLedBlink = now;
      ledStep = (ledStep + 1) % 5;
    }
  }
}