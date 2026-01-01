#include "time_sync.h"
#include <time.h>
#include "constants.h"

static bool ntpInitialized = false;
static time_t lastNtpSync = 0;

bool TimeSync::init() {
    if (ntpInitialized) {
        return true;
    }
    
    Serial.println("⏰ Initializing NTP time sync...");
    
    // Configure NTP
    configTime(TIMEZONE_OFFSET, 0, NTP_SERVER);
    
    // Wait for time sync (max 10 seconds)
    int attempts = 0;
    while (time(nullptr) < 1000000000L && attempts < 20) {
        delay(500);
        attempts++;
        Serial.print(".");
    }
    Serial.println();
    
    time_t now = time(nullptr);
    if (now > 1000000000L) {
        ntpInitialized = true;
        lastNtpSync = now;
        
        struct tm timeinfo;
        getLocalTime(&timeinfo);
        char timeStr[64];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S UTC-3", &timeinfo);
        
        Serial.printf("✅ NTP sync successful: %s\n", timeStr);
        return true;
    } else {
        Serial.println("❌ NTP sync failed");
        return false;
    }
}

time_t TimeSync::getCurrentTimestamp() {
    return time(nullptr);
}

String TimeSync::getCurrentTimeString() {
    time_t now = time(nullptr);
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S UTC-3", &timeinfo);
    
    return String(timeStr);
}

bool TimeSync::isTimeValid() {
    time_t now = time(nullptr);
    return (now > 1000000000L); // After year 2001
}

bool TimeSync::needsResync() {
    if (!ntpInitialized) return true;
    
    time_t now = time(nullptr);
    return (now - lastNtpSync) > 3600; // Resync every hour
}

bool TimeSync::forceResync() {
    ntpInitialized = false;
    return init();
}

JsonObject TimeSync::getTimeSyncResponse(JsonDocument& doc) {
    JsonObject response = doc.to<JsonObject>();
    
    time_t now = getCurrentTimestamp();
    response["type"] = "time_sync";
    response["ntp_timestamp"] = now;
    response["timezone_offset"] = TIMEZONE_OFFSET;
    response["server_time"] = getCurrentTimeString();
    
    return response;
}

void TimeSync::logTimeSync(const String& bikeId) {
    Serial.printf("⏰ Time sync sent to %s: %s (timestamp: %ld)\n", 
                  bikeId.c_str(), getCurrentTimeString().c_str(), getCurrentTimestamp());
}