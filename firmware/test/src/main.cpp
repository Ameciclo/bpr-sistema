#include <Arduino.h>

int counter = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("Test firmware - Counter started");
}

void loop() {
  Serial.println(counter);
  counter++;
  delay(1000);
}