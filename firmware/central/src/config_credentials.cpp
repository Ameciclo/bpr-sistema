#include "config_credentials.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "constants.h"

ConfigCredentials::ConfigCredentials() {
    setDefaults();
}

void ConfigCredentials::setDefaults() {
    strcpy(credentials.base_id, "");
    strcpy(credentials.wifi_ssid, "");
    strcpy(credentials.wifi_password, "");
    strcpy(credentials.firebase_database_url, "");
    strcpy(credentials.firebase_api_key, "");
    strcpy(credentials.firebase_project_id, "");
    credentials.created_timestamp = 0;
}

bool ConfigCredentials::loadCredentials() {
    if (!LittleFS.exists(CREDENTIALS_FILE)) {
        Serial.println("📄 Credentials file not found, using defaults");
        return false;
    }

    File file = LittleFS.open(CREDENTIALS_FILE, "r");
    if (!file) {
        Serial.println("❌ Failed to open credentials file");
        return false;
    }

    DynamicJsonDocument doc(CONFIG_CREDENTIALS_SIZE);
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.printf("❌ Credentials parse error: %s\n", error.c_str());
        return false;
    }

    // Load credentials from JSON
    if (doc["base_id"]) {
        strncpy(credentials.base_id, doc["base_id"], sizeof(credentials.base_id) - 1);
        credentials.base_id[sizeof(credentials.base_id) - 1] = '\0';
    }
    
    if (doc["wifi_ssid"]) {
        strncpy(credentials.wifi_ssid, doc["wifi_ssid"], sizeof(credentials.wifi_ssid) - 1);
        credentials.wifi_ssid[sizeof(credentials.wifi_ssid) - 1] = '\0';
    }
    
    if (doc["wifi_password"]) {
        strncpy(credentials.wifi_password, doc["wifi_password"], sizeof(credentials.wifi_password) - 1);
        credentials.wifi_password[sizeof(credentials.wifi_password) - 1] = '\0';
    }
    
    if (doc["firebase_database_url"]) {
        strncpy(credentials.firebase_database_url, doc["firebase_database_url"], sizeof(credentials.firebase_database_url) - 1);
        credentials.firebase_database_url[sizeof(credentials.firebase_database_url) - 1] = '\0';
    }
    
    if (doc["firebase_api_key"]) {
        strncpy(credentials.firebase_api_key, doc["firebase_api_key"], sizeof(credentials.firebase_api_key) - 1);
        credentials.firebase_api_key[sizeof(credentials.firebase_api_key) - 1] = '\0';
    }
    
    if (doc["firebase_project_id"]) {
        strncpy(credentials.firebase_project_id, doc["firebase_project_id"], sizeof(credentials.firebase_project_id) - 1);
        credentials.firebase_project_id[sizeof(credentials.firebase_project_id) - 1] = '\0';
    }
    
    if (doc["created_timestamp"]) {
        credentials.created_timestamp = doc["created_timestamp"];
    }

    Serial.printf("✅ Credentials loaded: %s\n", credentials.base_id);
    return isCredentialsValid();
}

bool ConfigCredentials::saveCredentials() {
    DynamicJsonDocument doc(CONFIG_CREDENTIALS_SIZE);
    
    doc["base_id"] = credentials.base_id;
    doc["wifi_ssid"] = credentials.wifi_ssid;
    doc["wifi_password"] = credentials.wifi_password;
    doc["firebase_database_url"] = credentials.firebase_database_url;
    doc["firebase_api_key"] = credentials.firebase_api_key;
    doc["firebase_project_id"] = credentials.firebase_project_id;
    doc["created_timestamp"] = credentials.created_timestamp;

    File file = LittleFS.open(CREDENTIALS_FILE, "w");
    if (!file) {
        Serial.println("❌ Failed to create credentials file");
        return false;
    }

    serializeJson(doc, file);
    file.close();

    Serial.println("💾 Credentials saved");
    return true;
}

bool ConfigCredentials::isCredentialsValid() {
    bool valid = (strlen(credentials.base_id) > 0 &&
                  strlen(credentials.wifi_ssid) > 0 &&
                  strlen(credentials.wifi_password) > 0 &&
                  strlen(credentials.firebase_database_url) > 0 &&
                  strlen(credentials.firebase_api_key) > 0);

    if (valid) {
        Serial.printf("✅ Credentials valid: %s\n", credentials.base_id);
    } else {
        Serial.println("❌ Credentials invalid - missing required fields");
        if (strlen(credentials.base_id) == 0) Serial.println("   - base_id missing");
        if (strlen(credentials.wifi_ssid) == 0) Serial.println("   - wifi_ssid missing");
        if (strlen(credentials.wifi_password) == 0) Serial.println("   - wifi_password missing");
        if (strlen(credentials.firebase_database_url) == 0) Serial.println("   - firebase_database_url missing");
        if (strlen(credentials.firebase_api_key) == 0) Serial.println("   - firebase_api_key missing");
    }

    return valid;
}