#ifndef BUFFER_MANAGER_H
#define BUFFER_MANAGER_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

enum BufferEntryType {
    BUFFER_BIKE_STATUS,
    BUFFER_ALERT,
    BUFFER_HEARTBEAT
};

struct BufferEntry {
    BufferEntryType type;
    unsigned long timestamp;
    char path[128];
    char data[256];
};

void initBufferManager();
void bufferManagerTask(void *parameter);
void addToBuffer(const char* path, const char* data, BufferEntryType type);
void saveToBuffer(BufferEntry* entry);
void processBufferedEntries();
bool sendBufferEntry(BufferEntry* entry);

#endif