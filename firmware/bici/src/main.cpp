/*
 * BPR Sistema - Firmware Bicicleta v2.0
 * Copyright (C) 2024 BPR Sistema Contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <LittleFS.h>
#include "constants.h"
#include "config_manager.h"
#include "buffer_manager.h"
#include "power_manager.h"
#include "at_base.h"
#include "scanning.h"
#include "lost.h"

// Global managers
ConfigManager configManager;
BufferManager bufferManager;
PowerManager powerManager;

// State handlers
AtBaseState* atBaseState = nullptr;
ScanningState* scanningState = nullptr;
LostState* lostState = nullptr;

// Current state
BikeState currentState = STATE_BOOT;

// Function declarations
BikeState handleBoot();
void handleSleep();
bool isBaseSearchDue();

static unsigned long lastBaseSearch = 0;

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    if (!LittleFS.begin(true)) {
        Serial.println("🔧 LittleFS corrompido - formatando...");
        LittleFS.format();
        LittleFS.begin(true);
    }
    WiFi.mode(WIFI_STA);

    Serial.println("🚲 BPR Bici Modular v2.0");

    // F07: carregar config persistida ANTES de gerar identidade
    bool configLoaded = configManager.load();
    if (!configLoaded || strlen(configManager.getConfig().bike_id) == 0) {
        configManager.generateUniqueId();
    }

    // Initialize BLE with the bike_id (F02: nome do dispositivo identifica a bike)
    NimBLEDevice::init(configManager.getConfig().bike_id);
    NimBLEDevice::setMTU(512);

    if (!configManager.isValid()) {
        Serial.println("⚠️ Config inválida - central detecta necessidade e envia config");
    }

    // F08: recuperar pendências do boot anterior antes de qualquer sessão nova
    bufferManager.load();
    currentState = STATE_BOOT;

    // Initialize state handlers
    atBaseState = new AtBaseState(configManager, bufferManager);
    scanningState = new ScanningState(configManager, bufferManager);
    lostState = new LostState(configManager, bufferManager);
}

void loop() {
    // F01: cada handler RETORNA o próximo estado; currentState muda em um único lugar
    BikeState nextState = currentState;

    switch (currentState) {
        case STATE_BOOT:
            nextState = handleBoot();
            break;

        case STATE_SCANNING:
            nextState = scanningState->update();

            // F24: busca de central por agenda (não a cada passagem do loop)
            if (nextState == STATE_SCANNING && isBaseSearchDue()) {
                if (atBaseState->scanForBase()) {
                    nextState = STATE_AT_BASE;
                }
            }
            break;

        case STATE_AT_BASE:
            nextState = atBaseState->update();
            break;

        case STATE_LOST:
            nextState = lostState->update();
            break;

        case STATE_SLEEP:
            handleSleep();
            break;
    }

    // F24: bateria crítica tem prioridade, exceto durante transferência em curso
    BikeState batteryState = powerManager.checkBatteryState(currentState, configManager.getConfig());
    if (batteryState == STATE_SLEEP && currentState != STATE_SLEEP &&
        !(currentState == STATE_AT_BASE && atBaseState->isTransferring())) {
        nextState = STATE_SLEEP;
    }

    // State transition (F01: atribuição única)
    if (nextState != currentState) {
        Serial.printf("🔄 State transition: %d → %d\n", currentState, nextState);
        currentState = nextState;
    }

    delay(100);
}

bool isBaseSearchDue() {
    if (millis() - lastBaseSearch < BASE_SEARCH_INTERVAL_MS) {
        return false;
    }
    lastBaseSearch = millis();
    return true;
}

BikeState handleBoot() {
    Serial.println("🔄 BOOT");

    BikeConfig& config = configManager.getConfig();

    // Show current config
    Serial.println("📋 Current Configuration:");
    Serial.printf("   🆔 ID: %s (v%d)\n", config.bike_id, config.version);
    Serial.printf("   📡 WiFi: %ds interval, %dms timeout\n", config.wifi.scan_interval_sec, config.wifi.scan_timeout_ms);
    Serial.printf("   🔵 BLE: %ds scan, base='%s'\n", config.ble.scan_time_sec, config.ble.base_name);
    Serial.printf("   🔋 Battery: %.2fV critical, %.2fV low\n", config.battery.critical_voltage, config.battery.low_voltage);
    Serial.printf("   🛠️ Dev Mode: %s\n", config.dev_mode ? "ON" : "OFF");

    powerManager.logBatteryStatus();

    if (powerManager.isCriticalBattery(config) && !config.dev_mode) {
        Serial.println("🔋 Critical battery - entering sleep\n");
        return STATE_SLEEP;
    }

    // F08: só abre sessão nova se não recuperamos uma pendente no setup()
    if (!bufferManager.isSessionActive() || bufferManager.isEmpty()) {
        if (!bufferManager.isSessionActive()) {
            bufferManager.startSession(config.bike_id);
        }
    } else {
        Serial.printf("📦 Recovered pending session: %d scans to deliver\n",
                      bufferManager.getCurrentSession().scan_count);
    }

    return STATE_SCANNING;
}

void handleSleep() {
    Serial.println("💤 Entering deep sleep");

    // F08: persistir ANTES de encerrar a sessão (save aceita sessão com dados)
    bufferManager.save();
    bufferManager.endSession();

    // Deep sleep for configured duration
    BikeConfig& config = configManager.getConfig();
    powerManager.enterDeepSleep(config.power.deep_sleep_duration_sec);
}
