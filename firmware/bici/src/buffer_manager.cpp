#include "buffer_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

BufferManager::BufferManager() : sessionActive(false) {
    memset(&currentSession, 0, sizeof(SessionData));
}

void BufferManager::startSession(const char* bikeId) {
    memset(&currentSession, 0, sizeof(SessionData));
    strncpy(currentSession.bike_id, bikeId, 15);
    currentSession.bike_id[15] = '\0';
    currentSession.session_start_millis = millis();
    currentSession.scan_count = 0;
    currentSession.battery_count = 0;
    sessionActive = true;
    
    Serial.printf("📊 Session started: %s at %d\n", bikeId, currentSession.session_start_millis);
}

void BufferManager::endSession() {
    if (sessionActive) {
        sessionActive = false;
        Serial.printf("📊 Session ended: %d scans, %d battery readings\n", 
                      currentSession.scan_count, currentSession.battery_count);
    }
}

void BufferManager::addScan(uint32_t timestamp, const NetworkData* networks, uint8_t count) {
    if (!sessionActive || currentSession.scan_count >= MAX_SCANS) {
        Serial.println("⚠️ Cannot add scan: session inactive or buffer full");
        return;
    }
    
    ScanData& scan = currentSession.scans[currentSession.scan_count];
    scan.timestamp_millis = timestamp;
    scan.network_count = min(count, (uint8_t)MAX_NETWORKS_PER_SCAN);
    
    for (int i = 0; i < scan.network_count; i++) {
        memcpy(&scan.networks[i], &networks[i], sizeof(NetworkData));
    }
    
    currentSession.scan_count++;
    Serial.printf("📡 Added scan %d with %d networks\n", currentSession.scan_count, scan.network_count);
}

void BufferManager::addBattery(uint32_t timestamp, uint8_t percent) {
    if (!sessionActive || currentSession.battery_count >= MAX_BATTERY) {
        return;
    }
    
    BatteryData& battery = currentSession.battery[currentSession.battery_count];
    battery.timestamp_millis = timestamp;
    battery.percent = percent;
    
    currentSession.battery_count++;
}

void BufferManager::save() {
    if (!sessionActive) {
        Serial.println("📦 No active session to save");
        return;
    }
    
    Serial.println("💾 Saving binary session...");
    File file = LittleFS.open("/session.bin", "w");
    if (file) {
        size_t bytesWritten = file.write((uint8_t*)&currentSession, sizeof(SessionData));
        file.close();
        
        if (bytesWritten == sizeof(SessionData)) {
            Serial.printf("✅ Session saved (%d bytes)\n", bytesWritten);
        } else {
            Serial.printf("❌ Session save failed: %d/%d bytes\n", bytesWritten, sizeof(SessionData));
        }
    } else {
        Serial.println("❌ Failed to open session file");
    }
}

void BufferManager::load() {
    Serial.println("📂 Loading binary session...");
    File file = LittleFS.open("/session.bin", "r");
    if (!file) {
        Serial.println("📦 No saved session found");
        return;
    }
    
    size_t bytesRead = file.readBytes((char*)&currentSession, sizeof(SessionData));
    file.close();
    
    if (bytesRead == sizeof(SessionData)) {
        sessionActive = true;
        Serial.printf("✅ Session loaded: %s with %d scans\n", 
                      currentSession.bike_id, currentSession.scan_count);
    } else {
        Serial.printf("❌ Session load failed: %d/%d bytes\n", bytesRead, sizeof(SessionData));
        memset(&currentSession, 0, sizeof(SessionData));
        sessionActive = false;
    }
}

void BufferManager::clear() {
    memset(&currentSession, 0, sizeof(SessionData));
    sessionActive = false;
    LittleFS.remove("/session.bin");
    Serial.println("🗑️ Session buffer cleared");
}

String BufferManager::toJson() const {
    if (!sessionActive) {
        return "{}";
    }
    
    DynamicJsonDocument doc(8192);
    doc["bike_id"] = currentSession.bike_id;
    doc["type"] = "data";
    doc["session_start_millis"] = currentSession.session_start_millis;
    
    JsonArray scans = doc.createNestedArray("scans");
    for (int i = 0; i < currentSession.scan_count; i++) {
        const ScanData& scan = currentSession.scans[i];
        JsonArray scanArray = scans.createNestedArray();
        scanArray.add(scan.timestamp_millis);
        
        JsonArray networks = scanArray.createNestedArray();
        for (int j = 0; j < scan.network_count; j++) {
            const NetworkData& net = scan.networks[j];
            JsonArray netArray = networks.createNestedArray();
            netArray.add(net.ssid);
            netArray.add(net.bssid);
            netArray.add(net.rssi);
            netArray.add(net.channel);
        }
    }
    
    JsonArray battery = doc.createNestedArray("battery");
    for (int i = 0; i < currentSession.battery_count; i++) {
        const BatteryData& bat = currentSession.battery[i];
        JsonArray batArray = battery.createNestedArray();
        batArray.add(bat.timestamp_millis);
        batArray.add(bat.percent);
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}

bool BufferManager::isEmpty() const {
    return !sessionActive || (currentSession.scan_count == 0 && currentSession.battery_count == 0);
}

bool BufferManager::isFull() const {
    return sessionActive && (currentSession.scan_count >= MAX_SCANS || currentSession.battery_count >= MAX_BATTERY);
}

size_t BufferManager::getSize() const {
    return sizeof(SessionData);
}