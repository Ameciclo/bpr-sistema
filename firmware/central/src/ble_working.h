#ifndef BLE_WORKING_H
#define BLE_WORKING_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

bool initBLEWorking();
void bleWorkingTask(void *parameter);
bool isBLEWorking();

#endif