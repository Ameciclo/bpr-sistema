#ifndef BUFFER_MANAGER_H
#define BUFFER_MANAGER_H

#include <Arduino.h>
#include "constants.h"

class ConfigManager; // Forward declaration

class BufferManager {
private:
    WiFiRecord* wifiBuffer;
    int bufferCount;
    int maxRecords;

public:
    BufferManager(int maxRecords = 100);
    ~BufferManager();
    void addWiFiRecord(const WiFiRecord& record);
    void clear();
    void save();
    void load();
    int getCount() const;
    const WiFiRecord* getRecords() const;
    bool isEmpty() const;
    bool isFull() const;
    void setMaxRecords(int max);
};

#endif