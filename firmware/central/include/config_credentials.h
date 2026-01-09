#pragma once
#include <Arduino.h>

struct CredentialsConfig {
    char base_id[32];
    char wifi_ssid[64];
    char wifi_password[64];
    char firebase_database_url[128];
    char firebase_api_key[128];
    char firebase_project_id[64];
    uint32_t created_timestamp;
    bool first_sync;
    uint8_t padding[3]; // Padding para alinhamento de 4 bytes
} __attribute__((packed, aligned(4)));

class ConfigCredentials {
public:
    ConfigCredentials();
    bool loadCredentials();
    bool saveCredentials();
    bool isCredentialsValid();
    
    const CredentialsConfig& getCredentials() const { return credentials; }
    CredentialsConfig& getCredentials() { return credentials; }
    
    // Helper methods
    String getBaseId() const { return String(credentials.base_id); }
    String getWiFiSSID() const { return String(credentials.wifi_ssid); }
    String getWiFiPassword() const { return String(credentials.wifi_password); }
    String getFirebaseURL() const { return String(credentials.firebase_database_url); }
    String getFirebaseKey() const { return String(credentials.firebase_api_key); }
    String getFirebaseProject() const { return String(credentials.firebase_project_id); }
    
    // First sync management
    bool isFirstSync() const { return credentials.first_sync; }
    void setFirstSyncCompleted() { credentials.first_sync = false; }
    
private:
    CredentialsConfig credentials;
    void setDefaults();
};