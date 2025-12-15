#include "bike_registry.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "constants.h"
#include "config_manager.h"

extern ConfigManager configManager;

static DynamicJsonDocument bikeRegistry(2048);
static bool registryLoaded = false;

bool BikeRegistry::init() {
    return loadRegistry();
}

bool BikeRegistry::loadRegistry() {
    if (!LittleFS.exists(BIKE_REGISTRY_FILE)) {
        Serial.println("📄 Bike registry not found, creating empty");
        bikeRegistry.clear();
        registryLoaded = true;
        return saveRegistry();
    }
    
    File file = LittleFS.open(BIKE_REGISTRY_FILE, "r");
    if (!file) {
        Serial.println("❌ Failed to open bike registry");
        return false;
    }
    
    DeserializationError error = deserializeJson(bikeRegistry, file);
    file.close();
    
    if (error) {
        Serial.printf("❌ Registry parse error: %s\n", error.c_str());
        return false;
    }
    
    registryLoaded = true;
    Serial.printf("✅ Bike registry loaded: %d bikes\n", bikeRegistry.size());
    
    // Log bikes por status
    int allowed = 0, pending = 0, blocked = 0;
    JsonObject obj = bikeRegistry.as<JsonObject>();
    for (JsonPair bike : obj) {
        String status = bike.value()["status"] | "unknown";
        if (status == "allowed") allowed++;
        else if (status == "pending") pending++;
        else if (status == "blocked") blocked++;
    }
    Serial.printf("   Allowed: %d | Pending: %d | Blocked: %d\n", allowed, pending, blocked);
    
    return true;
}

bool BikeRegistry::saveRegistry() {
    File file = LittleFS.open(BIKE_REGISTRY_FILE, "w");
    if (!file) {
        Serial.println("❌ Failed to create bike registry");
        return false;
    }
    
    serializeJson(bikeRegistry, file);
    file.close();
    
    Serial.println("💾 Bike registry saved");
    return true;
}

bool BikeRegistry::canConnect(const String& bikeId) {
    if (!registryLoaded) return false;
    
    // Verificar se é formato válido BPR
    if (!bikeId.startsWith("bpr-") || bikeId.length() != 10) {
        Serial.printf("❌ Invalid bike ID format: %s\n", bikeId.c_str());
        return false;
    }
    
    if (!bikeRegistry.containsKey(bikeId)) {
        Serial.printf("🆕 New bike detected: %s - allowing connection + adding as pending\n", bikeId.c_str());
        addPendingBike(bikeId);
        return true; // Permite conexão de bikes novas
    }
    
    String status = bikeRegistry[bikeId]["status"] | "unknown";
    bool canConnect = (status != "blocked");
    
    Serial.printf("🔍 Bike %s status: %s (%s)\n", 
                 bikeId.c_str(), status.c_str(), canConnect ? "✅ Can connect" : "❌ Blocked");
    
    return canConnect;
}

bool BikeRegistry::isAllowed(const String& bikeId) {
    if (!registryLoaded) return false;
    
    // Verificar se é formato válido BPR
    if (!bikeId.startsWith("bpr-") || bikeId.length() != 10) {
        return false;
    }
    
    if (!bikeRegistry.containsKey(bikeId)) {
        return false; // Bikes novas NÃO podem enviar dados (só pending)
    }
    
    String status = bikeRegistry[bikeId]["status"] | "unknown";
    return (status == "allowed"); // Só bikes ALLOWED podem enviar dados
}

void BikeRegistry::addPendingBike(const String& bikeId) {
    time_t now = time(nullptr);
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    
    char dateStr[64];
    strftime(dateStr, sizeof(dateStr), "%Y-%m-%d %H:%M:%S UTC-3", &timeinfo);
    
    bikeRegistry[bikeId]["status"] = "pending";
    bikeRegistry[bikeId]["first_seen"] = now;
    bikeRegistry[bikeId]["first_seen_human"] = dateStr;
    bikeRegistry[bikeId]["last_visit"] = now;
    bikeRegistry[bikeId]["last_visit_human"] = dateStr;
    bikeRegistry[bikeId]["visit_count"] = 1;
    bikeRegistry[bikeId]["last_heartbeat"] = nullptr;
    
    saveRegistry();
    Serial.printf("📝 Bike %s added as pending (first seen: %s)\n", bikeId.c_str(), dateStr);
}

void BikeRegistry::updateHeartbeat(const String& bikeId, int battery, int heap) {
    if (!registryLoaded || !bikeRegistry.containsKey(bikeId)) return;
    
    time_t now = time(nullptr);
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    
    char dateStr[64];
    strftime(dateStr, sizeof(dateStr), "%Y-%m-%d %H:%M:%S UTC-3", &timeinfo);
    
    bikeRegistry[bikeId]["last_heartbeat"]["timestamp"] = now;
    bikeRegistry[bikeId]["last_heartbeat"]["timestamp_human"] = dateStr;
    bikeRegistry[bikeId]["last_heartbeat"]["battery"] = battery;
    bikeRegistry[bikeId]["last_heartbeat"]["heap"] = heap;
    
    Serial.printf("💓 Heartbeat updated: %s (bat:%d%%, heap:%d)\n", 
                 bikeId.c_str(), battery, heap);
}

void BikeRegistry::updateFromFirebase(const DynamicJsonDocument& firebaseData) {
    Serial.println("🔄 Updating bike registry from Firebase...");
    
    bikeRegistry.clear();
    
    for (JsonPair bike : firebaseData.as<JsonObject>()) {
        String bikeId = bike.key().c_str();
        bikeRegistry[bikeId] = bike.value();
        
        String status = bike.value()["status"] | "unknown";
        Serial.printf("   %s: %s\n", bikeId.c_str(), status.c_str());
    }
    
    saveRegistry();
    registryLoaded = true;
    
    Serial.printf("✅ Registry updated: %d bikes from Firebase\n", bikeRegistry.size());
}

bool BikeRegistry::getRegistryForUpload(DynamicJsonDocument& doc) {
    if (!registryLoaded) return false;
    
    doc.clear();
    
    // Só enviar bikes que tiveram heartbeat atualizado
    for (JsonPair bike : bikeRegistry.as<JsonObject>()) {
        String bikeId = bike.key().c_str();
        if (bike.value()["last_heartbeat"].isNull()) continue;
        
        doc[bikeId] = bike.value();
    }
    
    return doc.size() > 0;
}

int BikeRegistry::getAllowedCount() {
    if (!registryLoaded) return 0;
    
    int count = 0;
    for (JsonPair bike : bikeRegistry.as<JsonObject>()) {
        String status = bike.value()["status"] | "";
        if (status == "allowed") count++;
    }
    return count;
}

void BikeRegistry::recordPendingVisit(const String& bikeId) {
    if (!registryLoaded || !bikeRegistry.containsKey(bikeId)) return;
    
    String status = bikeRegistry[bikeId]["status"] | "";
    if (status != "pending") return;
    
    time_t now = time(nullptr);
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    
    char dateStr[64];
    strftime(dateStr, sizeof(dateStr), "%Y-%m-%d %H:%M:%S UTC-3", &timeinfo);
    
    bikeRegistry[bikeId]["last_visit"] = now;
    bikeRegistry[bikeId]["last_visit_human"] = dateStr;
    
    int visitCount = bikeRegistry[bikeId]["visit_count"] | 0;
    bikeRegistry[bikeId]["visit_count"] = visitCount + 1;
    
    saveRegistry();
    Serial.printf("📝 Pending bike %s visited (count: %d, time: %s)\n", 
                 bikeId.c_str(), visitCount + 1, dateStr);
}

int BikeRegistry::getPendingCount() {
    if (!registryLoaded) return 0;
    
    int count = 0;
    for (JsonPair bike : bikeRegistry.as<JsonObject>()) {
        String status = bike.value()["status"] | "";
        if (status == "pending") count++;
    }
    return count;
}