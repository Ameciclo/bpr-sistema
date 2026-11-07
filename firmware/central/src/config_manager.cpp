#include <LittleFS.h>
#include "config_manager.h"
#include "config_credentials.h"
#include "constants.h"

ConfigManager::ConfigManager()
{
    resetToDefaults();
}

void ConfigManager::resetToDefaults()
{
    // Set defaults
    config.version = 0;
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
    // N03: recupera de tmp/bak se o canônico sumiu na janela de troca
    if (!LittleFS.exists(CONFIG_FILE))
    {
        if (LittleFS.exists("/config.tmp") && LittleFS.rename("/config.tmp", CONFIG_FILE))
        {
            Serial.println("🔁 Config recovered from staging copy");
        }
        else if (LittleFS.exists("/config.bak") && LittleFS.rename("/config.bak", CONFIG_FILE))
        {
            Serial.println("🔁 Config recovered from backup copy");
        }
    }

    if (!LittleFS.exists(CONFIG_FILE))
    {
        Serial.println("📄 Config file not found, using defaults");
        resetToDefaults();
        return false;
    }

    File file = LittleFS.open(CONFIG_FILE, "r");
    if (!file)
    {
        Serial.println("❌ Failed to open config file");
        resetToDefaults();
        return false;
    }

    // R20: lê em candidata; objeto ativo só é tocado após leitura completa
    CentralConfig candidate;
    size_t bytesRead = file.readBytes((char*)&candidate, sizeof(CentralConfig));
    file.close();

    if (bytesRead != sizeof(CentralConfig))
    {
        Serial.printf("❌ Config file size mismatch: %d != %d\n", bytesRead, sizeof(CentralConfig));
        // R20: restaura padrões reais — setup salva ESTES padrões, não lixo parcial
        resetToDefaults();
        return false;
    }

    // N09.3: conteúdo inválido NÃO é atribuído ao estado ativo — antes o
    // load "succeedia" com a candidata inválida já aplicada e só retornava
    // false, deixando o setup sem saber que os padrões não estavam vigentes
    if (!isConfigValidFor(candidate))
    {
        Serial.println("❌ Config content invalid - restoring safe defaults");
        resetToDefaults();
        return false;
    }

    config = candidate;

    Serial.println("✅ Config loaded");
    return true;
}

bool ConfigManager::saveConfig()
{
    // R04: temporário + rename; falha preserva a última config válida
    File file = LittleFS.open("/config.tmp", "w");
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
        LittleFS.remove("/config.tmp");
        return false;
    }

    // N03: rename DIRETO substitui o canônico atomicamente; fallback .bak
    // preserva o íntegro durante a troca (a janela remove-then-rename perdia
    // o arquivo se faltasse energia entre as duas operações)
    if (!LittleFS.rename("/config.tmp", CONFIG_FILE))
    {
        LittleFS.remove("/config.bak");
        if (LittleFS.exists(CONFIG_FILE) && !LittleFS.rename(CONFIG_FILE, "/config.bak"))
        {
            Serial.println("❌ Config backup move failed - previous file kept");
            LittleFS.remove("/config.tmp");
            return false;
        }
        if (!LittleFS.rename("/config.tmp", CONFIG_FILE))
        {
            Serial.println("❌ Config rename failed - restoring previous file");
            LittleFS.rename("/config.bak", CONFIG_FILE);
            LittleFS.remove("/config.tmp");
            return false;
        }
        LittleFS.remove("/config.bak");
    }

    Serial.println("💾 Config saved");
    return true;
}

bool ConfigManager::isConfigValid()
{
    return isConfigValidFor(config);
}

bool ConfigManager::isConfigValidFor(const CentralConfig& cfg)
{
    bool valid = true;

    if (cfg.intervals.sync_sec == 0 || cfg.intervals.sync_sec > 86400) valid = false;
    if (cfg.timeouts.wifi_sec == 0 || cfg.timeouts.wifi_sec > 600) valid = false;
    if (cfg.limits.max_bikes == 0 || cfg.limits.max_bikes > 10) valid = false;
    // R14: TODOS os divisores de LED são validados, não só count_pause_ms —
    // ble_ms/sync_ms/error_ms entram em módulo, count_ms em divisão de ciclo
    if (cfg.led.ble_ms == 0) valid = false;
    if (cfg.led.sync_ms == 0) valid = false;
    if (cfg.led.error_ms == 0) valid = false;
    if (cfg.led.count_ms == 0) valid = false;
    if (cfg.led.count_pause_ms == 0) valid = false;
    if (cfg.led.bike_arrived_ms == 0) valid = false;
    if (cfg.led.bike_left_ms == 0) valid = false;
    if (cfg.fallback.max_failures == 0) valid = false;
    if (cfg.timeouts.pairing_busy_ms == 0) valid = false;

    if (valid) {
        Serial.println("✅ Config válida");
    } else {
        Serial.println("❌ Config inválida");
    }

    return valid;
}

bool ConfigManager::updateFromCSV(const String& csvData, uint32_t remoteVersion)
{
    // R20: sem versão remota válida a configuração não é aplicada (não existe
    // mais fallback de versão para millis()/1000)
    if (remoteVersion == 0)
    {
        Serial.println("❌ CSV update rejected: no valid remote version");
        return false;
    }

    // R20: valida em CANDIDATA — objeto ativo só troca depois de validar E salvar
    CentralConfig candidate = config;

    int index = 0;
    int startPos = 0;
    int commaPos = 0;

    while ((commaPos = csvData.indexOf(',', startPos)) != -1 && index < 32) {
        String value = csvData.substring(startPos, commaPos);
        setConfigValue(index, value, candidate);
        startPos = commaPos + 1;
        index++;
    }

    // Last value (no comma)
    if (index < 32) {
        String value = csvData.substring(startPos);
        setConfigValue(index, value, candidate);
        index++;
    }

    // R20: CSV incompleto não é aplicado (campos parciais misturariam valores)
    if (index < 32)
    {
        Serial.printf("❌ CSV config incomplete: %d/32 fields - rejected\n", index);
        return false;
    }

    // F20: guarda a versão REMOTA para comparação futura
    candidate.last_update = remoteVersion;

    // R14/R20: validação completa da candidata (todos os divisores de LED)
    if (!isConfigValidFor(candidate)) {
        Serial.println("🚨 CSV config invalid - rejected (previous config kept)");
        return false;
    }

    // R20: só aplica (e confirma) se persistiu de fato
    CentralConfig backup = config;
    config = candidate;
    if (!saveConfig())
    {
        config = backup;
        Serial.println("🚨 CSV config not persisted - previous config kept");
        return false;
    }

    Serial.println("✅ CSV config applied and saved");
    return true;
}

String ConfigManager::getBaseId() const {
    extern ConfigCredentials configCredentials;
    return configCredentials.getBaseId();
}

void ConfigManager::setConfigValue(int index, const String& value, CentralConfig& target)
{
    switch (index) {
        case 0: target.intervals.sync_sec = value.toInt(); break;
        case 1: target.intervals.cleanup_sec = value.toInt(); break;
        case 2: target.intervals.log_sec = value.toInt(); break;
        case 3: target.intervals.led_count_sec = value.toInt(); break;
        case 4: target.timeouts.wifi_sec = value.toInt(); break;
        case 5: target.timeouts.pairing_busy_ms = value.toInt(); break;
        case 6: target.timeouts.config_ap_min = value.toInt(); break;
        case 7: target.led.boot_ms = value.toInt(); break;
        case 8: target.led.ble_ms = value.toInt(); break;
        case 9: target.led.sync_ms = value.toInt(); break;
        case 10: target.led.error_ms = value.toInt(); break;
        case 11: target.led.count_ms = value.toInt(); break;
        case 12: target.led.count_pause_ms = value.toInt(); break;
        case 13: target.led.bike_arrived_ms = value.toInt(); break;
        case 14: target.led.bike_left_ms = value.toInt(); break;
        case 15: target.limits.max_bikes = value.toInt(); break;
        case 16: target.limits.batch_size = value.toInt(); break;
        case 17: target.fallback.max_failures = value.toInt(); break;
        case 18: target.fallback.timeout_min = value.toInt(); break;
        case 19: target.fallback.sync_max_retries = value.toInt(); break;
        case 20: target.fallback.config_ap_timeout_sec = value.toInt(); break;
        case 21: target.buffer.max_size = value.toInt(); break;
        case 22: target.buffer.sync_threshold_percent = value.toInt(); break;
        case 23: target.buffer.auto_save_interval = value.toInt(); break;
        case 24: target.buffer.max_item_size = value.toInt(); break;
        case 25: target.compression.enabled = (value.toInt() == 1); break;
        case 26: target.compression.min_size_bytes = value.toInt(); break;
        case 27: target.storage.min_free_kb = value.toInt(); break;
        case 28: target.storage.warning_threshold_kb = value.toInt(); break;
        case 29: target.storage.aggressive_cleanup_multiplier = value.toFloat(); break;
        case 30: target.backup.enabled = (value.toInt() == 1); break;
        case 31: target.backup.retention_hours = value.toInt(); break;
    }
}