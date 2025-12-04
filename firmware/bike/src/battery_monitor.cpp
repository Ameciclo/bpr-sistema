#include "battery_monitor.h"
#include "bike_config.h"

BatteryMonitor::BatteryMonitor() : lastReading(0), sampleIndex(0) {
  for (int i = 0; i < ADC_SAMPLES; i++) {
    samples[i] = 0;
  }
}

void BatteryMonitor::init() {
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  
  // Pré-carrega amostras
  for (int i = 0; i < ADC_SAMPLES; i++) {
    samples[i] = analogRead(BATTERY_PIN);
    delay(10);
  }
}

float BatteryMonitor::readVoltage() {
  // Média móvel circular
  samples[sampleIndex] = analogRead(BATTERY_PIN);
  sampleIndex = (sampleIndex + 1) % ADC_SAMPLES;
  
  uint32_t sum = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) {
    sum += samples[i];
  }
  
  float avgReading = sum / (float)ADC_SAMPLES;
  float voltage = (avgReading / 4095.0) * 3.3 * VOLTAGE_DIVIDER_RATIO;
  
  lastReading = voltage;
  return voltage;
}

bool BatteryMonitor::isLowBattery(float threshold) {
  return lastReading < threshold;
}

uint8_t BatteryMonitor::getPercentage() {
  // Conversão linear 3.0V-4.2V para 0-100%
  float percentage = ((lastReading - 3.0) / 1.2) * 100.0;
  return constrain(percentage, 0, 100);
}