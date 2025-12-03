#include "firebase_client.h"
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <ArduinoJson.h>

// Firebase config - loaded from SPIFFS
String WIFI_SSID = "";
String WIFI_PASSWORD = "";
String API_KEY = "";
String DATABASE_URL = "";

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

static bool firebaseReady = false;
static bool wifiConnected = false;

bool loadConfig() {
    if (!SPIFFS.begin(true)) {
        Serial.println("❌ SPIFFS mount failed");
        return false;
    }
    
    File file = SPIFFS.open("/config.json", "r");
    if (!file) {
        Serial.println("❌ Config file not found");
        return false;
    }
    
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        Serial.println("❌ Config parse error");
        return false;
    }
    
    WIFI_SSID = doc["wifi"]["ssid"].as<String>();
    WIFI_PASSWORD = doc["wifi"]["password"].as<String>();
    API_KEY = doc["firebase"]["api_key"].as<String>();
    DATABASE_URL = doc["firebase"]["database_url"].as<String>();
    
    Serial.println("✅ Config loaded");
    return true;
}

bool connectWiFi() {
    if (wifiConnected) return true;
    
    Serial.println("📶 Conectando WiFi...");
    WiFi.begin(WIFI_SSID.c_str(), WIFI_PASSWORD.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.printf("\n✅ WiFi conectado: %s\n", WiFi.localIP().toString().c_str());
        return true;
    }
    
    Serial.println("\n❌ WiFi falhou");
    return false;
}

bool initFirebase() {
    if (!loadConfig()) return false;
    if (!connectWiFi()) return false;
    
    Serial.println("🔥 Inicializando Firebase...");
    
    config.api_key = API_KEY.c_str();
    config.database_url = DATABASE_URL.c_str();
    
    // Anonymous auth
    if (Firebase.signUp(&config, &auth, "", "")) {
        Serial.println("✅ Firebase auth OK");
    } else {
        Serial.printf("❌ Firebase auth falhou: %s\n", config.signer.signupError.message.c_str());
        return false;
    }
    
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
    
    firebaseReady = true;
    Serial.println("✅ Firebase inicializado");
    return true;
}

bool uploadBikeData(String bikeJson) {
    if (!firebaseReady) return false;
    
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, bikeJson);
    String bikeId = doc["uid"];
    
    String path = "/bikes/" + bikeId;
    
    if (Firebase.RTDB.setJSON(&fbdo, path.c_str(), &doc)) {
        Serial.printf("✅ Bike data uploaded: %s\n", bikeId.c_str());
        return true;
    } else {
        Serial.printf("❌ Upload falhou: %s\n", fbdo.errorReason().c_str());
        return false;
    }
}

bool uploadWifiScan(String wifiJson) {
    if (!firebaseReady) return false;
    
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, wifiJson);
    String bikeId = doc["bike_id"];
    unsigned long timestamp = doc["timestamp"];
    
    String path = "/wifi_scans/" + bikeId + "/" + String(timestamp);
    JsonArray networks = doc["networks"];
    
    if (Firebase.RTDB.setArray(&fbdo, path.c_str(), &networks)) {
        Serial.printf("✅ WiFi scan uploaded: %s\n", bikeId.c_str());
        return true;
    } else {
        Serial.printf("❌ WiFi upload falhou: %s\n", fbdo.errorReason().c_str());
        return false;
    }
}

bool uploadBatteryAlert(String alertJson) {
    if (!firebaseReady) return false;
    
    DynamicJsonDocument doc(512);
    deserializeJson(doc, alertJson);
    String bikeId = doc["bike_id"];
    
    String path = "/alerts/battery_low/" + bikeId;
    unsigned long timestamp = doc["timestamp"];
    
    if (Firebase.RTDB.setInt(&fbdo, path.c_str(), timestamp)) {
        Serial.printf("🚨 Battery alert uploaded: %s\n", bikeId.c_str());
        return true;
    } else {
        Serial.printf("❌ Alert upload falhou: %s\n", fbdo.errorReason().c_str());
        return false;
    }
}

bool isFirebaseReady() {
    return firebaseReady && wifiConnected && (WiFi.status() == WL_CONNECTED);
}

String getFirebaseStatus() {
    if (!wifiConnected) return "WiFi OFF";
    if (!firebaseReady) return "Firebase OFF";
    return "Online";
}