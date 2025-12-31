#include "config_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <vector>
#include "constants.h"

// Config field mapping for hybrid approach
enum FieldType {
    STRING,
    UINT32,
    UINT16,
    UINT8,
    FLOAT,
    BOOL
};

struct ConfigField
{
    const char *jsonPath;
    void *fieldPtr;
    FieldType type;
    size_t maxSize; // for strings only
};

// Static array with all config fields
static ConfigField configFields[] = {
    // Basic
    {"base_id", nullptr, STRING, sizeof(((CentralConfig *)0)->base_id)},
    {"version", nullptr, UINT32, 0},

    // Intervals
    {"intervals.sync_sec", nullptr, UINT32, 0},
    {"intervals.cleanup_sec", nullptr, UINT32, 0},
    {"intervals.log_sec", nullptr, UINT32, 0},
    {"intervals.led_count_sec", nullptr, UINT32, 0},

    // Timeouts
    {"timeouts.wifi_sec", nullptr, UINT32, 0},
    {"timeouts.firebase_ms", nullptr, UINT32, 0},

    // LED
    {"led.boot_ms", nullptr, UINT16, 0},
    {"led.ble_ready_ms", nullptr, UINT16, 0},
    {"led.wifi_sync_ms", nullptr, UINT16, 0},
    {"led.bike_arrived_ms", nullptr, UINT16, 0},
    {"led.bike_left_ms", nullptr, UINT16, 0},
    {"led.count_ms", nullptr, UINT16, 0},
    {"led.count_pause_ms", nullptr, UINT16, 0},
    {"led.error_ms", nullptr, UINT16, 0},

    // Limits
    {"limits.max_bikes", nullptr, UINT8, 0},
    {"limits.batch_size", nullptr, UINT16, 0},

    // Fallback
    {"fallback.max_failures", nullptr, UINT8, 0},
    {"fallback.timeout_min", nullptr, UINT16, 0},
    {"fallback.sync_max_retries", nullptr, UINT8, 0},
    {"fallback.config_ap_timeout_sec", nullptr, UINT16, 0},

    // Buffer
    {"buffer.max_size", nullptr, UINT8, 0},
    {"buffer.sync_threshold_percent", nullptr, UINT8, 0},
    {"buffer.auto_save_interval", nullptr, UINT8, 0},
    {"buffer.max_item_size", nullptr, UINT16, 0},

    // Compression
    {"compression.enabled", nullptr, BOOL, 0},
    {"compression.min_size_bytes", nullptr, UINT16, 0},

    // Storage
    {"storage.min_free_kb", nullptr, UINT16, 0},
    {"storage.warning_threshold_kb", nullptr, UINT16, 0},
    {"storage.aggressive_cleanup_multiplier", nullptr, FLOAT, 0},

    // Backup
    {"backup.enabled", nullptr, BOOL, 0},
    {"backup.retention_hours", nullptr, UINT16, 0}};

// Initialize field pointers to actual config struct members
void initConfigFieldPointers(CentralConfig *config)
{
    configFields[0].fieldPtr = config->base_id;
    configFields[1].fieldPtr = &config->version;
    configFields[2].fieldPtr = &config->intervals.sync_sec;
    configFields[3].fieldPtr = &config->intervals.cleanup_sec;
    configFields[4].fieldPtr = &config->intervals.log_sec;
    configFields[5].fieldPtr = &config->intervals.led_count_sec;
    configFields[6].fieldPtr = &config->timeouts.wifi_sec;
    configFields[7].fieldPtr = &config->timeouts.firebase_ms;
    configFields[8].fieldPtr = &config->led.boot_ms;
    configFields[9].fieldPtr = &config->led.ble_ms;
    configFields[10].fieldPtr = &config->led.sync_ms;
    configFields[11].fieldPtr = &config->led.bike_arrived_ms;
    configFields[12].fieldPtr = &config->led.bike_left_ms;
    configFields[13].fieldPtr = &config->led.count_ms;
    configFields[14].fieldPtr = &config->led.count_pause_ms;
    configFields[15].fieldPtr = &config->led.error_ms;
    configFields[16].fieldPtr = &config->limits.max_bikes;
    configFields[17].fieldPtr = &config->limits.batch_size;
    configFields[18].fieldPtr = &config->fallback.max_failures;
    configFields[19].fieldPtr = &config->fallback.timeout_min;
    configFields[20].fieldPtr = &config->fallback.sync_max_retries;
    configFields[21].fieldPtr = &config->fallback.config_ap_timeout_sec;
    configFields[22].fieldPtr = &config->buffer.max_size;
    configFields[23].fieldPtr = &config->buffer.sync_threshold_percent;
    configFields[24].fieldPtr = &config->buffer.auto_save_interval;
    configFields[25].fieldPtr = &config->buffer.max_item_size;
    configFields[26].fieldPtr = &config->compression.enabled;
    configFields[27].fieldPtr = &config->compression.min_size_bytes;
    configFields[28].fieldPtr = &config->storage.min_free_kb;
    configFields[29].fieldPtr = &config->storage.warning_threshold_kb;
    configFields[30].fieldPtr = &config->storage.aggressive_cleanup_multiplier;
    configFields[31].fieldPtr = &config->backup.enabled;
    configFields[32].fieldPtr = &config->backup.retention_hours;
}

// Helper function to save field to JSON
void saveFieldToJson(DynamicJsonDocument &doc, const ConfigField &field)
{
    // Parse nested path (e.g., "wifi.ssid" -> doc["wifi"]["ssid"])
    String path = field.jsonPath;
    int dotPos = path.indexOf('.');

    if (dotPos == -1)
    {
        // Simple field
        switch (field.type)
        {
        case STRING:
            doc[path] = (char *)field.fieldPtr;
            break;
        case UINT32:
            doc[path] = *(uint32_t *)field.fieldPtr;
            break;
        case UINT16:
            doc[path] = *(uint16_t *)field.fieldPtr;
            break;
        case UINT8:
            doc[path] = *(uint8_t *)field.fieldPtr;
            break;
        case FLOAT:
            doc[path] = *(float *)field.fieldPtr;
            break;
        case BOOL:
            doc[path] = *(bool *)field.fieldPtr;
            break;
        }
    }
    else
    {
        // Nested field
        String section = path.substring(0, dotPos);
        String key = path.substring(dotPos + 1);

        switch (field.type)
        {
        case STRING:
            doc[section][key] = (char *)field.fieldPtr;
            break;
        case UINT32:
            doc[section][key] = *(uint32_t *)field.fieldPtr;
            break;
        case UINT16:
            doc[section][key] = *(uint16_t *)field.fieldPtr;
            break;
        case UINT8:
            doc[section][key] = *(uint8_t *)field.fieldPtr;
            break;
        case FLOAT:
            doc[section][key] = *(float *)field.fieldPtr;
            break;
        case BOOL:
            doc[section][key] = *(bool *)field.fieldPtr;
            break;
        }
    }
}
void loadFieldFromJson(const DynamicJsonDocument &doc, const ConfigField &field)
{
    // Parse nested path (e.g., "wifi.ssid" -> doc["wifi"]["ssid"])
    String path = field.jsonPath;
    int dotPos = path.indexOf('.');

    JsonVariantConst value;
    if (dotPos == -1)
    {
        value = doc[path];
    }
    else
    {
        String section = path.substring(0, dotPos);
        String key = path.substring(dotPos + 1);
        value = doc[section][key];
    }

    if (value.isNull())
        return;

    // Set value based on type
    switch (field.type)
    {
    case STRING:
        if (value.is<const char *>())
        {
            strncpy((char *)field.fieldPtr, value.as<const char *>(), field.maxSize - 1);
            ((char *)field.fieldPtr)[field.maxSize - 1] = '\0';
        }
        break;
    case UINT32:
        if (value.is<uint32_t>())
        {
            *(uint32_t *)field.fieldPtr = value.as<uint32_t>();
        }
        break;
    case UINT16:
        if (value.is<uint16_t>())
        {
            *(uint16_t *)field.fieldPtr = value.as<uint16_t>();
        }
        break;
    case UINT8:
        if (value.is<uint8_t>())
        {
            *(uint8_t *)field.fieldPtr = value.as<uint8_t>();
        }
        break;
    case FLOAT:
        if (value.is<float>())
        {
            *(float *)field.fieldPtr = value.as<float>();
        }
        break;
    case BOOL:
        if (value.is<bool>())
        {
            *(bool *)field.fieldPtr = value.as<bool>();
        }
        break;
    }
}

ConfigManager::ConfigManager()
{
    // Initialize with defaults
    strcpy(config.base_id, "central_default");

    config.intervals.sync_sec = 300;
    config.intervals.cleanup_sec = 60;
    config.intervals.log_sec = 15;
    config.intervals.led_count_sec = 30;

    config.timeouts.wifi_sec = 30;
    config.timeouts.firebase_ms = 10000;
    config.timeouts.config_ap_min = 15;

    config.led.pin = LED_PIN;
    config.led.boot_ms = LED_BOOT_INTERVAL;
    config.led.ble_ms = LED_BLE_INTERVAL;
    config.led.sync_ms = LED_SYNC_INTERVAL;
    config.led.error_ms = LED_ERROR_INTERVAL;
    config.led.count_ms = LED_COUNT_INTERVAL;
    config.led.count_pause_ms = LED_COUNT_PAUSE;
    config.led.bike_arrived_ms = 150;
    config.led.bike_left_ms = 800;

    config.limits.max_bikes = MAX_BIKES;
    config.limits.batch_size = MAX_BUFFER_SIZE;

    config.fallback.max_failures = MAX_SYNC_FAILURES;
    config.fallback.timeout_min = SYNC_FAILURE_TIMEOUT_MS / 60000;
    config.fallback.sync_max_retries = 3;
    config.fallback.config_ap_timeout_sec = 300; // 5 minutes

    // Buffer defaults
    config.buffer.max_size = 50;
    config.buffer.sync_threshold_percent = 80;
    config.buffer.auto_save_interval = 5;
    config.buffer.max_item_size = 256;

    // Compression defaults
    config.compression.enabled = false;
    config.compression.min_size_bytes = 64;

    // Storage defaults
    config.storage.min_free_kb = 20;
    config.storage.warning_threshold_kb = 10;
    config.storage.aggressive_cleanup_multiplier = 0.5;

    // Backup defaults
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

    DynamicJsonDocument doc(CONFIG_JSON_BUFFER_SIZE);
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error)
    {
        Serial.printf("❌ Config parse error: %s\n", error.c_str());
        return false;
    }

    // Initialize field pointers
    initConfigFieldPointers(&config);

    // Load all fields automatically
    for (const auto &field : configFields)
    {
        loadFieldFromJson(doc, field);
    }

    Serial.printf("✅ Config loaded: %s\n", config.base_id);
    return isConfigValid();
}

bool ConfigManager::saveConfig()
{
    DynamicJsonDocument doc(CONFIG_JSON_BUFFER_SIZE);

    // Initialize field pointers
    initConfigFieldPointers(&config);

    // Save all fields automatically
    for (const auto &field : configFields)
    {
        saveFieldToJson(doc, field);
    }

    File file = LittleFS.open(CONFIG_FILE, "w");
    if (!file)
    {
        Serial.println("❌ Failed to create config file");
        return false;
    }

    serializeJson(doc, file);
    file.close();

    Serial.println("💾 Config saved");
    return true;
}

bool ConfigManager::isConfigValid()
{
    std::vector<String> missingFields;

    // Initialize field pointers
    initConfigFieldPointers(&config);

    // Check all fields automatically
    for (const auto &field : configFields)
    {
        bool isEmpty = false;

        switch (field.type)
        {
        case STRING:
            isEmpty = (strlen((char *)field.fieldPtr) == 0);
            break;
        case UINT32:
            isEmpty = (*(uint32_t *)field.fieldPtr == 0);
            break;
        case UINT16:
            isEmpty = (*(uint16_t *)field.fieldPtr == 0);
            break;
        case UINT8:
            isEmpty = (*(uint8_t *)field.fieldPtr == 0);
            break;
        case FLOAT:
            // For floats, only check if exactly 0.0 (location can be 0.0)
            isEmpty = false; // Don't validate floats as required
            break;
        case BOOL:
            isEmpty = false; // Bools are always valid (true/false)
            break;
        }

        if (isEmpty)
        {
            missingFields.push_back(field.jsonPath);
        }
    }

    bool valid = missingFields.empty();

    if (valid)
    {
        Serial.printf("✅ Config válida: %s (todos os %d campos obrigatórios presentes)\n",
                      config.base_id, sizeof(configFields) / sizeof(configFields[0]));
    }
    else
    {
        Serial.printf("❌ Config inválida: %d campos faltando de %d obrigatórios\n",
                      missingFields.size(), sizeof(configFields) / sizeof(configFields[0]));
        for (const String &field : missingFields)
        {
            Serial.printf("   - %s\n", field.c_str());
        }
    }

    return valid;
}

void ConfigManager::updateFromFirebase(const DynamicJsonDocument &firebaseConfig)
{
    Serial.println("🔄 Updating config from Firebase...");

    // Initialize field pointers
    initConfigFieldPointers(&config);

    // Update all fields automatically
    for (const auto &field : configFields)
    {
        loadFieldFromJson(firebaseConfig, field);
    }

    // Save updated config
    saveConfig();
    Serial.println("✅ Config updated from Firebase and saved locally");
}

String ConfigManager::getCentralConfigUrl(const String& baseId, const String& dbUrl, const String& apiKey) const
{
    return dbUrl + "/bases/" + baseId + "/configs.json?auth=" + apiKey;
}

String ConfigManager::getBikeRegistryUrl(const String& baseId, const String& dbUrl, const String& apiKey) const
{
    return dbUrl + "/bases/" + baseId + "/bikes.json?auth=" + apiKey;
}

String ConfigManager::getWiFiConfigUrl(const String& baseId, const String& dbUrl, const String& apiKey) const
{
    return dbUrl + "/bases/" + baseId + "/configs/wifi.json?auth=" + apiKey;
}

String ConfigManager::getHeartbeatUrl(const String& baseId, const String& dbUrl, const String& apiKey) const
{
    return dbUrl + "/bases/" + baseId + "/last_heartbeat.json?auth=" + apiKey;
}

String ConfigManager::getBufferDataUrl(const String& baseId, const String& dbUrl, const String& apiKey) const
{
    return dbUrl + "/bases/" + baseId + "/data.json?auth=" + apiKey;
}

bool ConfigManager::updateFromJson(const String &json)
{
    if (json.length() < 100)
    {
        Serial.println("🚨 JSON too small");
        return false;
    }

    DynamicJsonDocument doc(CONFIG_JSON_BUFFER_SIZE);
    if (deserializeJson(doc, json) != DeserializationError::Ok)
    {
        Serial.println("🚨 JSON parse failed");
        return false;
    }

    // Backup config atual
    CentralConfig backup = config;

    // Initialize field pointers and apply new config
    initConfigFieldPointers(&config);
    for (const auto &field : configFields)
    {
        loadFieldFromJson(doc, field);
    }

    // Validar com MESMA função usada no boot
    if (!isConfigValid())
    {
        Serial.println("🚨 Firebase config invalid - restoring backup");
        config = backup;
        return false;
    }

    // Sucesso - salvar nova config
    saveConfig();
    Serial.println("✅ Firebase config applied and saved");
    return true;
}
