#include <ArduinoJson.h>
#include <LittleFS.h>
#include <vector>
#include "config_manager.h"
#include "constants.h"

// Config field mapping for hybrid approach
enum FieldType
{
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
    {"timeouts.pairing_busy_ms", nullptr, UINT32, 0},

    // LED (8 campos - pin não é configurável via JSON)
    {"led.boot_ms", nullptr, UINT16, 0},
    {"led.ble_ms", nullptr, UINT16, 0},
    {"led.sync_ms", nullptr, UINT16, 0},
    {"led.error_ms", nullptr, UINT16, 0},
    {"led.count_ms", nullptr, UINT16, 0},
    {"led.count_pause_ms", nullptr, UINT16, 0},
    {"led.bike_arrived_ms", nullptr, UINT16, 0},
    {"led.bike_left_ms", nullptr, UINT16, 0},

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

// Macros para simplificar inicialização de ponteiros
#define INIT_CONFIG_FIELD(index, config, field) \
    configFields[index].fieldPtr = &config->field;

#define INIT_CONFIG_STRING_FIELD(index, config, field) \
    configFields[index].fieldPtr = config->field;

// Initialize field pointers to actual config struct members
void initConfigFieldPointers(CentralConfig *config)
{
    INIT_CONFIG_STRING_FIELD(0, config, base_id);
    INIT_CONFIG_FIELD(1, config, version);
    INIT_CONFIG_FIELD(2, config, intervals.sync_sec);
    INIT_CONFIG_FIELD(3, config, intervals.cleanup_sec);
    INIT_CONFIG_FIELD(4, config, intervals.log_sec);
    INIT_CONFIG_FIELD(5, config, intervals.led_count_sec);
    INIT_CONFIG_FIELD(6, config, timeouts.wifi_sec);
    INIT_CONFIG_FIELD(7, config, timeouts.firebase_ms);
    INIT_CONFIG_FIELD(8, config, timeouts.pairing_busy_ms);
    INIT_CONFIG_FIELD(8, config, led.boot_ms);
    INIT_CONFIG_FIELD(9, config, led.ble_ms);
    INIT_CONFIG_FIELD(10, config, led.sync_ms);
    INIT_CONFIG_FIELD(11, config, led.error_ms);
    INIT_CONFIG_FIELD(12, config, led.count_ms);
    INIT_CONFIG_FIELD(13, config, led.count_pause_ms);
    INIT_CONFIG_FIELD(14, config, led.bike_arrived_ms);
    INIT_CONFIG_FIELD(15, config, led.bike_left_ms);
    INIT_CONFIG_FIELD(16, config, limits.max_bikes);
    INIT_CONFIG_FIELD(17, config, limits.batch_size);
    INIT_CONFIG_FIELD(18, config, fallback.max_failures);
    INIT_CONFIG_FIELD(19, config, fallback.timeout_min);
    INIT_CONFIG_FIELD(20, config, fallback.sync_max_retries);
    INIT_CONFIG_FIELD(21, config, fallback.config_ap_timeout_sec);
    INIT_CONFIG_FIELD(22, config, buffer.max_size);
    INIT_CONFIG_FIELD(23, config, buffer.sync_threshold_percent);
    INIT_CONFIG_FIELD(24, config, buffer.auto_save_interval);
    INIT_CONFIG_FIELD(25, config, buffer.max_item_size);
    INIT_CONFIG_FIELD(26, config, compression.enabled);
    INIT_CONFIG_FIELD(27, config, compression.min_size_bytes);
    INIT_CONFIG_FIELD(28, config, storage.min_free_kb);
    INIT_CONFIG_FIELD(29, config, storage.warning_threshold_kb);
    INIT_CONFIG_FIELD(30, config, storage.aggressive_cleanup_multiplier);
    INIT_CONFIG_FIELD(31, config, backup.enabled);
    INIT_CONFIG_FIELD(32, config, backup.retention_hours);
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

// Default configuration as JSON string
static const char *DEFAULT_CONFIG_JSON = R"({
    "base_id": "central_default",
    "version": 1,
    "intervals": {
        "sync_sec": 90,
        "cleanup_sec": 60,
        "log_sec": 15,
        "led_count_sec": 30
    },
    "timeouts": {
        "wifi_sec": 60,
        "firebase_ms": 10000,
        "pairing_busy_ms": 10000,
        "config_ap_min": 15
    },
    "led": {
        "boot_ms": 100,
        "ble_ms": 2000,
        "sync_ms": 500,
        "error_ms": 50,
        "count_ms": 300,
        "count_pause_ms": 1500,
        "bike_arrived_ms": 150,
        "bike_left_ms": 800
    },
    "limits": {
        "max_bikes": 10,
        "batch_size": 8000
    },
    "fallback": {
        "max_failures": 5,
        "timeout_min": 30,
        "sync_max_retries": 3,
        "config_ap_timeout_sec": 300
    },
    "buffer": {
        "max_size": 50,
        "sync_threshold_percent": 80,
        "auto_save_interval": 5,
        "max_item_size": 256
    },
    "compression": {
        "enabled": true,
        "min_size_bytes": 64
    },
    "storage": {
        "min_free_kb": 20,
        "warning_threshold_kb": 10,
        "aggressive_cleanup_multiplier": 0.5
    },
    "backup": {
        "enabled": true,
        "retention_hours": 24
    }
})";

ConfigManager::ConfigManager()
{
    // Load defaults from JSON
    DynamicJsonDocument doc(CONFIG_JSON_BUFFER_SIZE);
    if (deserializeJson(doc, DEFAULT_CONFIG_JSON) == DeserializationError::Ok)
    {
        initConfigFieldPointers(&config);
        for (const auto &field : configFields)
        {
            loadFieldFromJson(doc, field);
        }
    }
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
