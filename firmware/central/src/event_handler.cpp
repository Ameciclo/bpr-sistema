#include "event_handler.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "structs.h"
#include "firebase_sync.h"
#include "config.h"
#include "config_loader.h"

extern QueueHandle_t eventQueue;

void eventHandlerTask(void *parameter) {
    SystemEvent event;
    
    while (true) {
        if (xQueueReceive(eventQueue, &event, pdMS_TO_TICKS(1000)) == pdTRUE) {
            processEvent(&event);
        }
    }
}

void processEvent(SystemEvent* event) {
    switch (event->type) {
        case EVENT_BIKE_CONNECTED:
            Serial.printf("Processing: Bike connected at %u\n", event->timestamp);
            createAlert("arrived_base", event->bikeId);
            break;
            
        case EVENT_BIKE_DISCONNECTED:
            Serial.printf("Processing: Bike %s disconnected at %u\n", event->bikeId, event->timestamp);
            createAlert("left_base", event->bikeId);
            break;
            
        case EVENT_BIKE_BATTERY_UPDATE:
            {
                Serial.printf("Processing: Bike %s battery %.2fV at %u\n", 
                             event->bikeId, event->batteryVoltage, event->timestamp);
                
                updateBikeStatus(event->bikeId, event->batteryVoltage, event->timestamp);
                
                // Check for low battery alert
                GlobalConfig config = getGlobalConfig();
                if (event->batteryVoltage < config.min_battery_voltage) {
                    createAlert("battery_low", event->bikeId);
                }
            }
            break;
            
        case EVENT_BIKE_STATUS_UPDATE:
            Serial.printf("Processing: Bike %s status update at %u\n", 
                         event->bikeId, event->timestamp);
            updateBikeStatus(event->bikeId, event->batteryVoltage, event->timestamp);
            break;
            
        case EVENT_WIFI_CONNECTED:
            Serial.println("Processing: WiFi connected");
            break;
            
        case EVENT_WIFI_DISCONNECTED:
            Serial.println("Processing: WiFi disconnected");
            break;
            
        case EVENT_FIREBASE_SYNC_SUCCESS:
            Serial.println("Processing: Firebase sync success");
            break;
            
        case EVENT_FIREBASE_SYNC_FAILED:
            {
                Serial.println("Processing: Firebase sync failed");
                AppConfig appConfig = getAppConfig();
                createAlert("sync_failure", appConfig.base_id);
            }
            break;
            
        default:
            Serial.printf("Processing: Unknown event type %d\n", event->type);
            break;
    }
}