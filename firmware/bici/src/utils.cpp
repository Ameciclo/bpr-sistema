#include "constants.h"
#include <WiFi.h>

String bssidToString(const uint8_t* bssid) {
    char buffer[18];
    snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    return String(buffer);
}

String generateBikeId() {
    uint64_t chipId = ESP.getEfuseMac();
    uint32_t id = (uint32_t)(chipId & 0xFFFFFF);
    // Hex cobre todo o espaço de 24 bits sem truncar (padrão do firmware/common/bpr_types.h)
    char buffer[12];
    snprintf(buffer, sizeof(buffer), "bpr-%06x", id);
    return String(buffer);
}