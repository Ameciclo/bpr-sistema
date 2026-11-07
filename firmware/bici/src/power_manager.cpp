#include "power_manager.h"
#include "constants.h"
#include <esp_sleep.h>

PowerManager::PowerManager() : lastBatteryPercent(100), lastBatteryRead(0),
                               lastVoltage(0.0f), emergencyMode(false), emergencyModeStart(0) {}
// X01: cache começa VAZIO (lastBatteryRead=0) — a primeira consulta lê o ADC
// de verdade; não existe mais default fictício de 4,0 V

uint8_t PowerManager::getBatteryPercent() {
    float voltage = getBatteryVoltage();

    // X01: leitura desconhecida (0 V) não vira estimativa — percentual 0
    // sem afirmar "bateria morta"; a tensão 0 sinaliza falha de medição
    if (voltage <= 0.1f) {
        return 0;
    }

    // X01: curva CONTÍNUA 3,2V..4,2V — sem o salto 69%→100% ao cruzar 3,9V;
    // bateria cheia (4,2V) = 100% apenas no fim real da faixa
    float percent = ((voltage - 3.2f) / (4.2f - 3.2f)) * 100.0f;

    // Clamp to 0-100%
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    return (uint8_t)percent;
}

float PowerManager::getBatteryVoltage() {
    // X01: primeira consulta LÊ o ADC — o cache só vale DEPOIS de uma
    // amostra real (o default fictício de 4,0 V mascarava bateria crítica
    // nos primeiros 5 s de boot)
    if (lastBatteryRead != 0 && millis() - lastBatteryRead < 5000) {
        return lastVoltage;
    }

    // X01: média de 4 amostras — leitura única de ADC é ruidosa e decide
    // transição de energia
    uint32_t sum = 0;
    const int SAMPLES = 4;
    for (int i = 0; i < SAMPLES; i++) {
        sum += analogRead(BATTERY_PIN);
    }
    int adc = sum / SAMPLES;
    lastBatteryRead = millis();

    if (adc < 100) {
        // X01: ADC ~0 = leitura anômala (pino solto/curto) — NÃO é "USB 100%".
        // Retorna desconhecido (0 V); decisões de energia ignoram (abaixo)
        if (lastVoltage != 0.0f) {
            Serial.printf("⚠️ ADC=%d anomalous - voltage unknown\n", adc);
        }
        lastVoltage = 0.0f;
        return lastVoltage;
    }

    // Divisor resistivo: V_bat = (adc/4095) * 3.3 * 2.0
    lastVoltage = (adc / 4095.0f) * 3.3f * 2.0f;
    return lastVoltage;
}

bool PowerManager::isBatteryCharging() {
    // X01: tensão alta isolada NÃO prova carregador — só acima do máximo
    // físico de uma Li-ion (4,2 V) há evidência compatível com fonte externa
    // no divisor. Calibração real do circuito permanece pendente de bancada.
    return getBatteryVoltage() > 4.25f;
}

BikeState PowerManager::checkBatteryState(BikeState currentState, const BikeConfig& config) {
    float voltage = getBatteryVoltage();

    // X01: medição desconhecida não decide transição de energia
    if (voltage <= 0.1f) {
        return currentState;
    }

    // Critical battery check
    if (voltage <= config.battery.critical_voltage && !config.dev_mode) {
        if (currentState != STATE_SLEEP) {
            Serial.printf("🔋 Critical battery: %.2fV - entering sleep\n", voltage);
            return STATE_SLEEP;
        }
    }

    return currentState; // No change
}

bool PowerManager::isLowBattery(const BikeConfig& config) {
    float voltage = getBatteryVoltage();
    if (voltage <= 0.1f) return false; // X01: desconhecido não é "baixo"
    return voltage <= config.battery.low_voltage;
}

bool PowerManager::isCriticalBattery(const BikeConfig& config) {
    float voltage = getBatteryVoltage();
    if (voltage <= 0.1f) return false; // X01: desconhecido não é "crítico"
    return voltage <= config.battery.critical_voltage;
}

bool PowerManager::enterDeepSleep(uint32_t seconds) {
    // X01: duração validada e resultado do armamento do wakeup CONFERIDO —
    // dormir sem wake-up armado seria brick; em falha, retorna sem dormir
    if (seconds == 0) {
        Serial.println("❌ Deep sleep with zero duration rejected - using 60s fallback");
        seconds = 60;
    }
    if (seconds > 86400UL * 7UL) {
        Serial.printf("❌ Deep sleep duration %lus capped to 7 days\n", (unsigned long)seconds);
        seconds = 86400UL * 7UL;
    }

    Serial.printf("💤 Deep sleep for %d seconds\n", seconds);

    esp_err_t err = esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
    if (err != ESP_OK) {
        Serial.printf("🚨 Failed to arm wakeup timer (0x%x) - NOT sleeping\n", err);
        return false;
    }
    esp_deep_sleep_start();
    return true; // noreturn na prática; satisfaz o contrato do chamador
}

void PowerManager::logBatteryStatus() {
    uint8_t percent = getBatteryPercent();
    float voltage = getBatteryVoltage();
    bool charging = isBatteryCharging();
    
    Serial.printf("🔋 Battery: %d%% (%.2fV) %s\n", 
                  percent, voltage, 
                  charging ? "⚡CHARGING" : "");
}

String PowerManager::getHeartbeatReport(const BikeConfig& config, bool hasPendingData, uint16_t pendingScans, size_t pendingBytes) {
    DynamicJsonDocument doc(384);
    doc["type"] = "heartbeat";
    doc["bike_id"] = config.bike_id;
    doc["config_version"] = config.version;
    doc["last_update"] = config.version;
    doc["battery_percent"] = getBatteryPercent();
    doc["battery_voltage"] = getBatteryVoltage();
    doc["is_charging"] = isBatteryCharging();
    doc["heap"] = ESP.getFreeHeap();
    doc["uptime_sec"] = millis() / 1000;

    JsonObject pending = doc.createNestedObject("pending_data");
    pending["sessions_count"] = hasPendingData ? 1 : 0;
    pending["scans_count"] = pendingScans;
    pending["total_bytes"] = (uint32_t)pendingBytes;

    String result;
    serializeJson(doc, result);
    return result;
}