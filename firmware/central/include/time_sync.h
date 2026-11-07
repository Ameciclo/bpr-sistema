#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <time.h>

class TimeSync {
public:
    // === INITIALIZATION ===
    static bool init();
    static bool forceResync();
    
    // === TIME QUERIES ===
    static time_t getCurrentTimestamp();
    static String getCurrentTimeString();
    static bool isTimeValid();
    static bool needsResync();
    // Patch de horários: idade da sincronização (qualidade da referência)
    static time_t lastNtpSyncTime();
    
    // === BIKE SYNC ===
    static JsonObject getTimeSyncResponse(JsonDocument& doc);
    static void logTimeSync(const String& bikeId);
};

#endif