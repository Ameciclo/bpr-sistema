#include <LittleFS.h>
#include "config_credentials.h"
#include "constants.h"

ConfigCredentials::ConfigCredentials()
{
    setDefaults();
}

void ConfigCredentials::setDefaults()
{
    strcpy(credentials.base_id, "");
    strcpy(credentials.wifi_ssid, "");
    strcpy(credentials.wifi_password, "");
    strcpy(credentials.firebase_database_url, "");
    strcpy(credentials.firebase_api_key, "");
    strcpy(credentials.firebase_project_id, "");
    credentials.created_timestamp = 0;
    credentials.first_sync = true;
}

bool ConfigCredentials::loadCredentials()
{
    if (!LittleFS.exists(CREDENTIALS_FILE))
    {
        Serial.println("📄 Credentials file not found, using defaults");
        return false;
    }

    File file = LittleFS.open(CREDENTIALS_FILE, "r");
    if (!file)
    {
        Serial.println("❌ Failed to open credentials file");
        return false;
    }

    // Verificar tamanho do arquivo
    size_t fileSize = file.size();
    size_t expectedSize = sizeof(CredentialsConfig);
    
    if (fileSize != expectedSize) {
        Serial.printf("⚠️ File size mismatch: %d != %d, recreating\n", fileSize, expectedSize);
        file.close();
        return false;
    }

    // Ler com verificação de alinhamento
    uint8_t* buffer = (uint8_t*)malloc(expectedSize);
    if (!buffer) {
        Serial.println("❌ Failed to allocate buffer");
        file.close();
        return false;
    }
    
    size_t bytesRead = file.readBytes((char*)buffer, expectedSize);
    file.close();
    
    if (bytesRead != expectedSize) {
        Serial.printf("❌ Read size mismatch: %d != %d\n", bytesRead, expectedSize);
        free(buffer);
        return false;
    }
    
    // Copiar com segurança
    memcpy(&credentials, buffer, expectedSize);
    free(buffer);

    Serial.printf("✅ Credentials loaded: %s\n", credentials.base_id);
    return isCredentialsValid();
}

bool ConfigCredentials::saveCredentials()
{
    // Usar buffer intermediário para segurança
    size_t expectedSize = sizeof(CredentialsConfig);
    uint8_t* buffer = (uint8_t*)malloc(expectedSize);
    if (!buffer) {
        Serial.println("❌ Failed to allocate buffer for save");
        return false;
    }
    
    // Copiar dados para buffer alinhado
    memcpy(buffer, &credentials, expectedSize);
    
    File file = LittleFS.open(CREDENTIALS_FILE, "w");
    if (!file)
    {
        Serial.println("❌ Failed to create credentials file");
        free(buffer);
        return false;
    }

    size_t bytesWritten = file.write(buffer, expectedSize);
    file.close();
    free(buffer);

    if (bytesWritten != expectedSize)
    {
        Serial.printf("❌ Failed to write credentials: %d != %d\n", bytesWritten, expectedSize);
        return false;
    }

    Serial.println("💾 Credentials saved");
    return true;
}

bool ConfigCredentials::isCredentialsValid()
{
    bool valid = true;

    if (strlen(credentials.base_id) == 0) {
        Serial.println("   - base_id missing");
        valid = false;
    }
    if (strlen(credentials.wifi_ssid) == 0) {
        Serial.println("   - wifi_ssid missing");
        valid = false;
    }
    if (strlen(credentials.wifi_password) == 0) {
        Serial.println("   - wifi_password missing");
        valid = false;
    }
    if (strlen(credentials.firebase_database_url) == 0) {
        Serial.println("   - firebase_database_url missing");
        valid = false;
    }
    if (strlen(credentials.firebase_api_key) == 0) {
        Serial.println("   - firebase_api_key missing");
        valid = false;
    }
    if (strlen(credentials.firebase_project_id) == 0) {
        Serial.println("   - firebase_project_id missing");
        valid = false;
    }

    if (!valid) {
        Serial.println("❌ Credentials invalid - missing required fields:");
    } else {
        Serial.printf("✅ Credentials valid: %s\n", credentials.base_id);
    }

    return valid;
}