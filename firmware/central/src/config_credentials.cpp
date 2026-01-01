#include "config_credentials.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "constants.h"

#define CREDENTIAL_FIELDS \
    FIELD(base_id, "") \
    FIELD(wifi_ssid, "") \
    FIELD(wifi_password, "") \
    FIELD(firebase_database_url, "") \
    FIELD(firebase_api_key, "") \
    FIELD(firebase_project_id, "") \
    FIELD_NUM(created_timestamp, 0) \
    FIELD_BOOL(first_sync, true)

ConfigCredentials::ConfigCredentials() {
    setDefaults();
}

void ConfigCredentials::setDefaults() {
#define FIELD(name, default_val) strcpy(credentials.name, default_val);
#define FIELD_NUM(name, default_val) credentials.name = default_val;
#define FIELD_BOOL(name, default_val) credentials.name = default_val;
    CREDENTIAL_FIELDS
#undef FIELD
#undef FIELD_NUM
#undef FIELD_BOOL
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

#define FIELD(name, default_val) \
    if (doc[#name]) { \
        strncpy(credentials.name, doc[#name], sizeof(credentials.name) - 1); \
        credentials.name[sizeof(credentials.name) - 1] = '\0'; \
    }
#define FIELD_NUM(name, default_val) \
    if (doc[#name]) credentials.name = doc[#name];
#define FIELD_BOOL(name, default_val) \
    if (doc[#name]) credentials.name = doc[#name];
    
    CREDENTIAL_FIELDS
    
#undef FIELD
#undef FIELD_NUM
#undef FIELD_BOOL

    Serial.printf("✅ Credentials loaded: %s\n", credentials.base_id);
    return isCredentialsValid();
}

bool ConfigCredentials::saveCredentials() {
    DynamicJsonDocument doc(CONFIG_CREDENTIALS_SIZE);
    
#define FIELD(name, default_val) doc[#name] = credentials.name;
#define FIELD_NUM(name, default_val) doc[#name] = credentials.name;
#define FIELD_BOOL(name, default_val) doc[#name] = credentials.name;
    CREDENTIAL_FIELDS
#undef FIELD
#undef FIELD_NUM
#undef FIELD_BOOL

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
    bool valid = true;
    
#define FIELD(name, default_val) \
    if (strlen(credentials.name) == 0) { \
        Serial.println("   - " #name " missing"); \
        valid = false; \
    }
#define FIELD_NUM(name, default_val) // Skip numeric fields
#define FIELD_BOOL(name, default_val) // Skip boolean fields
    
    if (!valid) Serial.println("❌ Credentials invalid - missing required fields:");
    CREDENTIAL_FIELDS
    
#undef FIELD
#undef FIELD_NUM
#undef FIELD_BOOL

    if (valid) {
        Serial.printf("✅ Credentials valid: %s\n", credentials.base_id);
    }

    return valid;
}