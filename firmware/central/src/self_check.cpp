#include "self_check.h"
#include <Arduino.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include "config.h"

void selfCheckTask(void *parameter) {
    while (true) {
        performSelfCheck();
        vTaskDelay(pdMS_TO_TICKS(SELFCHECK_INTERVAL_MS));
    }
}

void performSelfCheck() {
    // Check heap memory
    size_t freeHeap = esp_get_free_heap_size();
    size_t minFreeHeap = esp_get_minimum_free_heap_size();
    
    Serial.printf("Heap: %d bytes free, %d bytes minimum\n", freeHeap, minFreeHeap);
    
    if (freeHeap < 10000) { // Less than 10KB free
        Serial.println("WARNING: Low heap memory!");
    }
    
    // Check internal temperature
    float temperature = temperatureRead();
    Serial.printf("Internal temperature: %.1f°C\n", temperature);
    
    if (temperature > 80.0) {
        Serial.println("WARNING: High temperature!");
    }
    
    // Check task stack usage
    checkTaskStacks();
    
    // Feed watchdog
    esp_task_wdt_reset();
    
    Serial.println("Self-check completed");
}

void checkTaskStacks() {
    extern TaskHandle_t wifiTaskHandle;
    extern TaskHandle_t firebaseTaskHandle;
    extern TaskHandle_t bleTaskHandle;
    extern TaskHandle_t eventTaskHandle;
    extern TaskHandle_t bufferTaskHandle;
    extern TaskHandle_t selfCheckTaskHandle;
    
    TaskHandle_t tasks[] = {
        wifiTaskHandle, firebaseTaskHandle, bleTaskHandle,
        eventTaskHandle, bufferTaskHandle, selfCheckTaskHandle
    };
    
    const char* taskNames[] = {
        "WiFi", "Firebase", "BLE", "Event", "Buffer", "SelfCheck"
    };
    
    for (int i = 0; i < 6; i++) {
        if (tasks[i] != NULL) {
            UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(tasks[i]);
            Serial.printf("Task %s stack: %d bytes remaining\n", taskNames[i], stackHighWaterMark * sizeof(StackType_t));
            
            if (stackHighWaterMark < 100) {
                Serial.printf("WARNING: Task %s low on stack!\n", taskNames[i]);
            }
        }
    }
}