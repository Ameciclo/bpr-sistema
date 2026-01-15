#ifndef BUFFER_MANAGER_H
#define BUFFER_MANAGER_H

#include <Arduino.h>
#include "constants.h"

class BufferManager {
private:
    SessionData currentSession;
    bool sessionActive;

public:
    BufferManager();
    
    // Session management
    void startSession(const char* bikeId);
    void endSession();
    bool isSessionActive() const { return sessionActive; }
    
    // Data collection
    void addScan(uint32_t timestamp, const NetworkData* networks, uint8_t count);
    void addBattery(uint32_t timestamp, uint8_t percent);
    
    // Persistence
    void save();
    void load();
    void clear();
    
    // Data access
    const SessionData& getCurrentSession() const { return currentSession; }
    String toJson() const;
    
    // Status
    bool isEmpty() const;
    bool isFull() const;
    size_t getSize() const;
};

#endif