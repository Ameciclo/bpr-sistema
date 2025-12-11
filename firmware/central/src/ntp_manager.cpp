#include "ntp_manager.h"
#include "config_manager.h"

WiFiUDP ntpUDP;
NTPClient* timeClient = nullptr;
bool ntpSynced = false;
unsigned long ntpEpoch = 0;
unsigned long ntpMillisBase = 0;

void initNTP() {
    timeClient = new NTPClient(ntpUDP, "pool.ntp.org", -10800, 60000);
}

bool syncNTP() {
    if (!timeClient) return false;
    
    Serial.println("🕰️ Sincronizando NTP...");
    timeClient->begin();
    
    if (timeClient->update()) {
        ntpSynced = true;
        ntpEpoch = timeClient->getEpochTime();
        ntpMillisBase = millis();
        Serial.printf("✅ NTP OK: %lu (base: %lu)\n", ntpEpoch, ntpMillisBase);
        return true;
    } else {
        Serial.println("⚠️ NTP falhou - usando millis()");
        return false;
    }
}

unsigned long getCurrentTimestamp() {
    if (ntpSynced && ntpEpoch > 0) {
        return ntpEpoch + ((millis() - ntpMillisBase) / 1000);
    }
    return millis() / 1000;
}

unsigned long correctTimestamp(unsigned long bikeTimestamp, unsigned long bikeMillis) {
    if (bikeTimestamp > 1640995200) { // 2022-01-01
        Serial.printf("🕰️ Bike com NTP válido: %lu\n", bikeTimestamp);
        return bikeTimestamp;
    }
    
    if (ntpSynced && ntpEpoch > 0) {
        unsigned long correctedTime = ntpEpoch + ((millis() - ntpMillisBase) / 1000);
        Serial.printf("🔧 Corrigindo timestamp: %lu -> %lu\n", bikeTimestamp, correctedTime);
        return correctedTime;
    }
    
    Serial.printf("⚠️ Sem correção disponível: %lu\n", bikeTimestamp);
    return bikeTimestamp;
}

void sendNTPToBike() {
    if (ntpSynced && ntpEpoch > 0) {
        unsigned long currentEpoch = getCurrentTimestamp();
        
        // TODO: Implementar envio real via BLE
        Serial.printf("📡 Enviando NTP para bike: %lu\n", currentEpoch);
        
        // Por enquanto, apenas log - implementação BLE será feita depois
    }
}