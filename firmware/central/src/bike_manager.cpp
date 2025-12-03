#include "bike_manager.h"
#include "config_manager.h"
#include <vector>

static std::vector<ConnectedBike> connectedBikes;
static const int MAX_BIKES = 10;

void initBikeManager() {
    connectedBikes.clear();
    connectedBikes.reserve(MAX_BIKES);
    Serial.println("✅ Bike Manager inicializado");
}

bool addConnectedBike(String bikeId, uint16_t connHandle) {
    // Verificar se já existe
    for (auto& bike : connectedBikes) {
        if (bike.connHandle == connHandle || strcmp(bike.bikeId, bikeId.c_str()) == 0) {
            // Atualizar dados existentes
            strncpy(bike.bikeId, bikeId.c_str(), 7);
            bike.bikeId[7] = '\0';
            bike.connHandle = connHandle;
            bike.lastSeen = millis() / 1000;
            bike.needsConfig = true;
            Serial.printf("🔄 Bike %s reconectada (handle: %d)\n", bikeId.c_str(), connHandle);
            return true;
        }
    }
    
    // Verificar limite
    if (connectedBikes.size() >= MAX_BIKES) {
        Serial.println("⚠️ Limite de bikes atingido");
        return false;
    }
    
    // Adicionar nova bike
    ConnectedBike newBike = {0};
    strncpy(newBike.bikeId, bikeId.c_str(), 7);
    newBike.bikeId[7] = '\0';
    newBike.connHandle = connHandle;
    newBike.configSent = false;
    newBike.needsConfig = true;
    newBike.lastSeen = millis() / 1000;
    newBike.lastBattery = 0.0;
    
    connectedBikes.push_back(newBike);
    
    Serial.printf("🔵 ✅ Nova bike conectada: %s (handle: %d) - Total: %d\n", 
                  bikeId.c_str(), connHandle, connectedBikes.size());
    
    return true;
}

bool removeConnectedBike(uint16_t connHandle) {
    for (auto it = connectedBikes.begin(); it != connectedBikes.end(); ++it) {
        if (it->connHandle == connHandle) {
            Serial.printf("🔴 ❌ Bike %s desconectada (handle: %d) - Total: %d\n", 
                          it->bikeId, connHandle, connectedBikes.size() - 1);
            connectedBikes.erase(it);
            return true;
        }
    }
    return false;
}

ConnectedBike* findBikeByHandle(uint16_t connHandle) {
    for (auto& bike : connectedBikes) {
        if (bike.connHandle == connHandle) {
            return &bike;
        }
    }
    return nullptr;
}

ConnectedBike* findBikeById(String bikeId) {
    for (auto& bike : connectedBikes) {
        if (strcmp(bike.bikeId, bikeId.c_str()) == 0) {
            return &bike;
        }
    }
    return nullptr;
}

int getConnectedBikeCount() {
    return connectedBikes.size();
}

void updateBikeLastSeen(String bikeId) {
    ConnectedBike* bike = findBikeById(bikeId);
    if (bike) {
        bike->lastSeen = millis() / 1000;
    }
}

void markBikeNeedsConfig(String bikeId) {
    ConnectedBike* bike = findBikeById(bikeId);
    if (bike) {
        bike->needsConfig = true;
        bike->configSent = false;
        Serial.printf("📋 Bike %s marcada para reconfiguração\n", bikeId.c_str());
    }
}

bool sendConfigToBike(String bikeId) {
    ConnectedBike* bike = findBikeById(bikeId);
    if (!bike) {
        Serial.printf("❌ Bike %s não encontrada\n", bikeId.c_str());
        return false;
    }
    
    if (!isConfigValid()) {
        Serial.println("⚠️ Config inválida - não enviando");
        return false;
    }
    
    GlobalConfig config = getGlobalConfig();
    
    // Criar pacote de configuração
    BPRConfigPacket packet = {0};
    packet.version = config.version;
    packet.deepSleepSec = config.deep_sleep_after_sec;
    packet.wifiScanInterval = config.wifi_scan_interval_sec;
    packet.wifiScanLowBatt = config.wifi_scan_interval_low_batt_sec;
    packet.minBatteryVoltage = config.min_battery_voltage;
    packet.timestamp = config.update_timestamp;
    
    // TODO: Implementar envio via BLE characteristic
    // Por enquanto, simular envio
    Serial.printf("📡 ✅ Config enviada para %s: v%d, sleep:%ds, scan:%ds, bat:%.2fV\n",
                  bikeId.c_str(), packet.version, packet.deepSleepSec, 
                  packet.wifiScanInterval, packet.minBatteryVoltage);
    
    bike->configSent = true;
    bike->needsConfig = false;
    
    return true;
}

void processPendingConfigs() {
    if (!isConfigValid()) {
        return;
    }
    
    for (auto& bike : connectedBikes) {
        if (bike.needsConfig && !bike.configSent) {
            sendConfigToBike(String(bike.bikeId));
            delay(100); // Pequeno delay entre envios
        }
    }
}

void cleanupOldConnections() {
    unsigned long now = millis() / 1000;
    
    for (auto it = connectedBikes.begin(); it != connectedBikes.end();) {
        if (now - it->lastSeen > 300) { // 5 minutos sem atividade
            Serial.printf("🧹 Removendo bike inativa: %s\n", it->bikeId);
            it = connectedBikes.erase(it);
        } else {
            ++it;
        }
    }
}