#ifndef EVENT_HANDLER_H
#define EVENT_HANDLER_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "structs.h"

void eventHandlerTask(void *parameter);
void processEvent(SystemEvent* event);

#endif