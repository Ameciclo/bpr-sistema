#ifndef UPLOAD_QUEUE_H
#define UPLOAD_QUEUE_H

#include <Arduino.h>
#include <map>
#include <vector>

struct PendingUpload {
    String bikeId;
    String data;
    uint32_t timestamp;
    uint8_t attempts;
    bool confirmed;
};

class UploadQueue {
public:
    // === UPLOAD MANAGEMENT ===
    static bool addPendingUpload(const String& bikeId, const String& data);
    static bool confirmUpload(const String& bikeId);
    static void processTimeouts();
    
    // === QUERIES ===
    static int getPendingCount();
    static std::vector<String> getPendingBikes();
    static bool hasPendingUpload(const String& bikeId);
    static String getUploadStatus();
    
    // === MAINTENANCE ===
    static void clearAll();
};

#endif