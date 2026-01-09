#include <LittleFS.h>
#include "config_manager.h"
#include "config_credentials.h"
#include "constants.h"

ConfigManager::ConfigManager()
{
    // Set defaults
    config.last_update = 0;
    config.intervals.sync_sec = 90;
    config.intervals.cleanup_sec = 60;
    config.intervals.log_sec = 15;
    config.intervals.led_count_sec = 30;
    config.timeouts.wifi_sec = 60;
    config.timeouts.pairing_busy_ms = 10000;
    config.timeouts.config_ap_min = 15;
    config.led.boot_ms = 100;
    config.led.ble_ms = 2000;
    config.led.sync_ms = 500;
    config.led.error_ms = 50;
    config.led.count_ms = 300;
    config.led.count_pause_ms = 1500;
    config.led.bike_arrived_ms = 150;
    config.led.bike_left_ms = 800;
    config.limits.max_bikes = 10;
    config.limits.batch_size = 8000;
    config.fallback.max_failures = 5;
    config.fallback.timeout_min = 30;
    config.fallback.sync_max_retries = 3;
    config.fallback.config_ap_timeout_sec = 300;
    config.buffer.max_size = 50;
    config.buffer.sync_threshold_percent = 80;
    config.buffer.auto_save_interval = 5;
    config.buffer.max_item_size = 256;
    config.compression.enabled = true;
    config.compression.min_size_bytes = 64;
    config.storage.min_free_kb = 20;
    config.storage.warning_threshold_kb = 10;
    config.storage.aggressive_cleanup_multiplier = 0.5;
    config.backup.enabled = true;
    config.backup.retention_hours = 24;
}

bool ConfigManager::loadConfig()
{
    if (!LittleFS.exists(CONFIG_FILE))
    {
        Serial.println("📄 Config file not found, using defaults");
        return false;
    }

    File file = LittleFS.open(CONFIG_FILE, "r");
    if (!file)
    {
        Serial.println("❌ Failed to open config file");
        return false;
    }

    size_t bytesRead = file.readBytes((char*)&config, sizeof(CentralConfig));
    file.close();

    if (bytesRead != sizeof(CentralConfig))
    {
        Serial.printf("❌ Config file size mismatch: %d != %d\n", bytesRead, sizeof(CentralConfig));
        return false;
    }

    Serial.println("✅ Config loaded");
    return isConfigValid();
}

bool ConfigManager::saveConfig()
{
    File file = LittleFS.open(CONFIG_FILE, "w");
    if (!file)
    {
        Serial.println("❌ Failed to create config file");
        return false;
    }

    size_t bytesWritten = file.write((uint8_t*)&config, sizeof(CentralConfig));
    file.close();

    if (bytesWritten != sizeof(CentralConfig))
    {
        Serial.printf("❌ Failed to write config: %d != %d\n", bytesWritten, sizeof(CentralConfig));
        return false;
    }

    Serial.println("💾 Config saved");
    return true;
}

bool ConfigManager::isConfigValid()
{
    bool valid = true;
    
    if (config.intervals.sync_sec == 0) valid = false;
    if (config.timeouts.wifi_sec == 0) valid = false;
    if (config.limits.max_bikes == 0) valid = false;
    
    if (valid) {
        Serial.println("✅ Config válida");
    } else {
        Serial.println("❌ Config inválida");
    }
    
    return valid;
}

bool ConfigManager::updateFromCSV(const String& csvData)
{
    // Parse CSV: "90,60,15,30,60,10000,15,100,2000,500,50,300,1500,150,800,10,8000,5,30,3,300,50,80,5,256,1,64,20,10,0.5,1,24"
    
    // Backup config atual
    CentralConfig backup = config;
    
    // Parse CSV values
    int index = 0;
    int startPos = 0;
    int commaPos = 0;
    
    while ((commaPos = csvData.indexOf(',', startPos)) != -1 && index < 32) {
        String value = csvData.substring(startPos, commaPos);
        setConfigValue(index, value);
        startPos = commaPos + 1;
        index++;
    }
    
    // Last value (no comma)
    if (index < 32) {
        String value = csvData.substring(startPos);
        setConfigValue(index, value);
    }
    
    // Update timestamp
    config.last_update = millis() / 1000;
    
    // Validate
    if (!isConfigValid()) {
        Serial.println("🚨 CSV config invalid - restoring backup");
        config = backup;
        return false;
    }
    
    // Save
    saveConfig();
    Serial.println("✅ CSV config applied and saved");
    return true;
}

String ConfigManager::getBaseId() const {
    extern ConfigCredentials configCredentials;
    return configCredentials.getBaseId();
}

void ConfigManager::setConfigValue(int index, const String& value)
{
    switch (index) {
        case 0: config.intervals.sync_sec = value.toInt(); break;
        case 1: config.intervals.cleanup_sec = value.toInt(); break;
        case 2: config.intervals.log_sec = value.toInt(); break;
        case 3: config.intervals.led_count_sec = value.toInt(); break;
        case 4: config.timeouts.wifi_sec = value.toInt(); break;
        case 5: config.timeouts.pairing_busy_ms = value.toInt(); break;
        case 6: config.timeouts.config_ap_min = value.toInt(); break;
        case 7: config.led.boot_ms = value.toInt(); break;
        case 8: config.led.ble_ms = value.toInt(); break;
        case 9: config.led.sync_ms = value.toInt(); break;
        case 10: config.led.error_ms = value.toInt(); break;
        case 11: config.led.count_ms = value.toInt(); break;
        case 12: config.led.count_pause_ms = value.toInt(); break;
        case 13: config.led.bike_arrived_ms = value.toInt(); break;
        case 14: config.led.bike_left_ms = value.toInt(); break;
        case 15: config.limits.max_bikes = value.toInt(); break;
        case 16: config.limits.batch_size = value.toInt(); break;
        case 17: config.fallback.max_failures = value.toInt(); break;
        case 18: config.fallback.timeout_min = value.toInt(); break;
        case 19: config.fallback.sync_max_retries = value.toInt(); break;
        case 20: config.fallback.config_ap_timeout_sec = value.toInt(); break;
        case 21: config.buffer.max_size = value.toInt(); break;
        case 22: config.buffer.sync_threshold_percent = value.toInt(); break;
        case 23: config.buffer.auto_save_interval = value.toInt(); break;
        case 24: config.buffer.max_item_size = value.toInt(); break;
        case 25: config.compression.enabled = (value.toInt() == 1); break;
        case 26: config.compression.min_size_bytes = value.toInt(); break;
        case 27: config.storage.min_free_kb = value.toInt(); break;
        case 28: config.storage.warning_threshold_kb = value.toInt(); break;
        case 29: config.storage.aggressive_cleanup_multiplier = value.toFloat(); break;
        case 30: config.backup.enabled = (value.toInt() == 1); break;
        case 31: config.backup.retention_hours = value.toInt(); break;
    }
}