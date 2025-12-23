#include "upload_queue.h"
#include "bike_manager.h"
#include "ble_server.h"
#include "time_sync.h"
#include <ArduinoJson.h>

static std::map<String, PendingUpload> pendingUploads;
static std::map<String, uint32_t> lastUploadTime;

bool UploadQueue::addPendingUpload(const String& bikeId, const String& data) {
    PendingUpload upload;
    upload.bikeId = bikeId;
    upload.data = data;
    upload.timestamp = millis();
    upload.attempts = 0;
    upload.confirmed = false;
    
    pendingUploads[bikeId] = upload;
    lastUploadTime[bikeId] = millis();
    
    Serial.printf("📤 Added pending upload for %s (%d bytes)\n", 
                  bikeId.c_str(), data.length());
    return true;
}

bool UploadQueue::confirmUpload(const String& bikeId) {
    if (pendingUploads.find(bikeId) != pendingUploads.end()) {
        pendingUploads[bikeId].confirmed = true;
        
        // Send confirmation to bike
        String confirmation = BikeManager::confirmDataUpload(bikeId);
        BPRBLEServer::pushConfigToBike(bikeId, confirmation);
        
        // Remove from pending queue
        pendingUploads.erase(bikeId);
        
        Serial.printf("✅ Upload confirmed for %s - bike can clear buffer\n", bikeId.c_str());
        return true;
    }
    return false;
}

void UploadQueue::processTimeouts() {
    uint32_t now = millis();
    std::vector<String> timedOut;
    
    for (auto& pair : pendingUploads) {
        PendingUpload& upload = pair.second;
        
        // Timeout after 2 minutes
        if ((now - upload.timestamp) > 120000) {
            if (upload.attempts < 3) {
                upload.attempts++;
                upload.timestamp = now;
                Serial.printf("⏰ Retry upload for %s (attempt %d/3)\n", 
                              upload.bikeId.c_str(), upload.attempts);
            } else {
                timedOut.push_back(upload.bikeId);
                Serial.printf("❌ Upload timeout for %s after 3 attempts\n", 
                              upload.bikeId.c_str());
            }
        }
    }
    
    // Remove timed out uploads
    for (const String& bikeId : timedOut) {
        pendingUploads.erase(bikeId);
    }
}

int UploadQueue::getPendingCount() {
    return pendingUploads.size();
}

std::vector<String> UploadQueue::getPendingBikes() {
    std::vector<String> bikes;
    for (const auto& pair : pendingUploads) {
        bikes.push_back(pair.first);
    }
    return bikes;
}

bool UploadQueue::hasPendingUpload(const String& bikeId) {
    return pendingUploads.find(bikeId) != pendingUploads.end();
}

void UploadQueue::clearAll() {
    pendingUploads.clear();
    lastUploadTime.clear();
    Serial.println("🗑️ Cleared all pending uploads");
}

String UploadQueue::getUploadStatus() {
    DynamicJsonDocument doc(512);
    doc["pending_count"] = getPendingCount();
    doc["total_bikes_uploaded"] = lastUploadTime.size();
    
    JsonArray pending = doc.createNestedArray("pending_bikes");
    for (const auto& pair : pendingUploads) {
        JsonObject bike = pending.createNestedObject();
        bike["bike_id"] = pair.second.bikeId;
        bike["attempts"] = pair.second.attempts;
        bike["age_seconds"] = (millis() - pair.second.timestamp) / 1000;
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}