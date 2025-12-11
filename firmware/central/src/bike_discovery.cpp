#include "bike_discovery.h"
#include "config_manager.h"
#include "firebase_manager.h"

std::vector<PendingBike> pendingBikes;
extern String pendingData;

void registerPendingBike(String bleName, String macAddress) {
    for (auto& bike : pendingBikes) {
        if (bike.bleName == bleName) {
            return;
        }
    }
    
    Serial.printf("🆕 Nova bike detectada: %s (%s)\n", bleName.c_str(), macAddress.c_str());
    
    PendingBike newBike;
    newBike.bleName = bleName;
    newBike.macAddress = macAddress;
    newBike.firstSeen = millis() / 1000;
    newBike.registered = false;
    pendingBikes.push_back(newBike);
    
    CentralConfigCache config = getCentralConfig();
    String pendingBikeData = "{\"mac_address\":\"" + macAddress + 
                            "\",\"first_seen\":" + String(newBike.firstSeen) + 
                            ",\"central_id\":\"" + String(config.base_id) + 
                            "\",\"status\":\"pending\"}";
    
    String pendingPath = "/pending_bikes/" + String(config.base_id) + "/" + bleName;
    String dataToAdd = "{\"type\":\"pending_bike\",\"path\":\"" + pendingPath + "\",\"data\":" + pendingBikeData + "}";
    
    // addToPendingData(dataToAdd); // TODO: implement
    
    // Adicionar aos dados pendentes diretamente
    extern String pendingData;
    if (pendingData.length() > 0) pendingData += ",";
    pendingData += dataToAdd;
    
    Serial.println("⏳ Bike registrada - aguardando aprovação humana");
}

void checkPendingApprovals() {
    if (pendingBikes.empty()) return;
    
    Serial.printf("📋 Bikes pendentes de aprovação: %d\n", pendingBikes.size());
    
    // TODO: Implementar download real de aprovações do Firebase
    // Estrutura esperada: /pending_bikes/{central_id}/{ble_name}/status = "approved"
    
    for (const auto& bike : pendingBikes) {
        CentralConfigCache config = getCentralConfig();
        String approvalPath = "/pending_bikes/" + String(config.base_id) + "/" + bike.bleName + "/status";
        String result;
        
        if (downloadFromFirebase(approvalPath, result)) {
            if (result.indexOf("approved") >= 0) {
                // Extrair bike_id da aprovação
                String bikeId = bike.bleName; // Por enquanto usar o nome BLE
                processApprovedBike(bike.bleName, bikeId);
            }
        }
        
        Serial.printf("  • %s (%s) - aguardando há %lu segundos\n", 
                     bike.bleName.c_str(), 
                     bike.macAddress.c_str(),
                     (millis()/1000) - bike.firstSeen);
    }
}

void processApprovedBike(String bleName, String bikeId) {
    Serial.printf("✅ Bike aprovada: %s -> %s\n", bleName.c_str(), bikeId.c_str());
    
    for (auto it = pendingBikes.begin(); it != pendingBikes.end(); ++it) {
        if (it->bleName == bleName) {
            pendingBikes.erase(it);
            break;
        }
    }
    
    CentralConfigCache config = getCentralConfig();
    String configData = "{\"bike_id\":\"" + bikeId + 
                       "\",\"central_id\":\"" + String(config.base_id) + 
                       "\",\"firebase\":{\"database_url\":\"https://botaprarodar-routes-default-rtdb.firebaseio.com\"}," +
                       "\"wifi_scan_interval\":25," +
                       "\"deep_sleep_sec\":300," +
                       "\"min_battery\":3.45}";
    
    Serial.printf("📝 Config preparada para %s\n", bikeId.c_str());
}

