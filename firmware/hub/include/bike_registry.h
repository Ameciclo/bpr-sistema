#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

#define BIKE_REGISTRY_FILE "/bike_registry.json"

class BikeRegistry {
public:
    static bool init();
    static bool loadRegistry();
    static bool saveRegistry();
    static bool canConnect(const String& bikeId);
    static bool isAllowed(const String& bikeId);
    static void recordPendingVisit(const String& bikeId);
    static void addPendingBike(const String& bikeId);
    static void updateHeartbeat(const String& bikeId, int battery, int heap);
    static void updateFromFirebase(const DynamicJsonDocument& firebaseData);
    static bool getRegistryForUpload(DynamicJsonDocument& doc);
    static int getAllowedCount();
    static int getPendingCount();
};