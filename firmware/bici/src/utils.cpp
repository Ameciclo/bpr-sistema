#include "constants.h"
#include <WiFi.h>

String bssidToString(const uint8_t* bssid) {
    char bssidStr[18];
    sprintf(bssidStr, "%02X:%02X:%02X:%02X:%02X:%02X",
            bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    return String(bssidStr);
}

String generateBikeId() {
    uint64_t chipid = ESP.getEfuseMac();
    char bikeId[16];
    sprintf(bikeId, "bpr-%06llx", chipid & 0xFFFFFF);
    return String(bikeId);
}