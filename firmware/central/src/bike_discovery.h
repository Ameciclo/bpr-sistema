#pragma once
#include <Arduino.h>
#include <vector>

struct PendingBike {
    String bleName;
    String macAddress;
    unsigned long firstSeen;
    bool registered;
};

extern std::vector<PendingBike> pendingBikes;

void registerPendingBike(String bleName, String macAddress);
void checkPendingApprovals();
void processApprovedBike(String bleName, String bikeId);
void addToPendingData(String data);