#include <Arduino.h>

#define LED_PIN 8

int contador = 0;
bool ledState = false;

void setup() {
  pinMode(LED_PIN, OUTPUT);
  
  // Inicializa USB Serial
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("=== ESP32-C3 TESTE ===");
}

void loop() {
  contador++;
  ledState = !ledState;
  
  digitalWrite(LED_PIN, ledState);
  
  Serial.print("Contador: ");
  Serial.print(contador);
  Serial.print(" | LED: ");
  Serial.println(ledState ? "ON" : "OFF");
  
  delay(1000);
}