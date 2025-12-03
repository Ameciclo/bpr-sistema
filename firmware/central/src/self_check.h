#ifndef SELF_CHECK_H
#define SELF_CHECK_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void selfCheckTask(void *parameter);
void performSelfCheck();
void checkTaskStacks();

#endif