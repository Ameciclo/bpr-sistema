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
#include <Preferences.h>
#include "constants.h"
#include "config_manager.h"
#include "buffer_manager.h"
#include "power_manager.h"
#include "at_base.h"
#include "scanning.h"
#include "trip_manager.h"

// Global managers
ConfigManager configManager;
BufferManager bufferManager;
PowerManager powerManager;
TripManager tripManager;

// State handlers
AtBaseState* atBaseState = nullptr;
ScanningState* scanningState = nullptr;

// Current state
BikeState currentState = STATE_BOOT;

// Function declarations
BikeState handleBoot();
void handleSleep();
bool isBaseSearchDue();

static unsigned long lastBaseSearch = 0;

// X05: mount SEM formatação silenciosa. A formatação destrutiva só é
// permitida em instalação virgem (sem marca de custódia em NVS); com
// custódia assumida, a falha de mount é diagnosticada e os dados são
// preservados para recuperação — nunca descartados como "recuperação normal"
static bool mountFilesystem() {
    Preferences prefs;
    prefs.begin("bpr", false);
    bool custody = prefs.getBool("fs_ok", false);

    if (LittleFS.begin()) {
        prefs.putBool("fs_ok", true);
        prefs.end();
        return true;
    }

    if (!custody) {
        Serial.println("🔧 LittleFS ausente em instalação virgem - formatando");
        if (LittleFS.begin(true)) {
            prefs.putBool("fs_ok", true);
            prefs.end();
            return true;
        }
        prefs.end();
        return false;
    }

    // X05: havia dados sob custódia — NÃO formatar
    prefs.end();
    return false;
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    if (!mountFilesystem()) {
        Serial.println("🚨 CRITICAL: LittleFS mount failed WITH data custody - NOT formatting");
        Serial.println("   Dados preservados para recuperação via USB. Tentarei de novo em 1h.");
        // Indicação visível de falha (LED) antes de dormir
        for (int i = 0; i < 10; i++) {
            digitalWrite(LED_PIN, HIGH); delay(150);
            digitalWrite(LED_PIN, LOW); delay(150);
        }
        if (!powerManager.enterDeepSleep(3600)) {
            // X01: sem wake-up armado, dormir seria brick — permanece parado
            // e visível em vez de prosseguir sem filesystem
            Serial.println("🚨 Cannot arm sleep - halting (data preserved)");
            while (true) { delay(1000); }
        }
    }
    WiFi.mode(WIFI_STA);

    Serial.println("🚲 BPR Bici Modular v2.0");

    // R16: identidade de boot antes de qualquer sessão
    bufferManager.beginBoot();
    tripManager.beginBoot(bufferManager.memberBootId());
    tripManager.load();

    // F07: carregar config persistida ANTES de gerar identidade
    bool configLoaded = configManager.load();
    if (!configLoaded || strlen(configManager.getConfig().bike_id) == 0) {
        configManager.generateUniqueId();
    }
    // Patch de horários: a viagem carrega a identidade canônica da bike
    tripManager.setBikeId(configManager.getConfig().bike_id);

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
        // Patch de horários: a máquina de estados confirma a SAÍDA — a
        // ausência da base inicia a tolerância; recontato cancela (§2.1)
        if (currentState == STATE_AT_BASE && nextState == STATE_SCANNING) {
            tripManager.noteLeftBase();
        }
        Serial.printf("🔄 State transition: %d → %d\n", currentState, nextState);
        currentState = nextState;
    }

    // Patch de horários: confirma saída por ausência persistente (fora da
    // base); heartbeats na base NÃO abrem viagem (H05/H11)
    tripManager.poll(millis());

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
    // N04: sessão recuperada porém VAZIA não carrega dado nenhum — abrir uma
    // nova evita carimbar a coleta atual com o session_start de um boot antigo
    if (!bufferManager.isSessionActive() || bufferManager.isEmpty()) {
        bufferManager.startSession(config.bike_id);
    } else {
        Serial.printf("📦 Recovered pending session: %d scans to deliver\n",
                      bufferManager.getCurrentSession().scan_count);
    }

    return STATE_SCANNING;
}

void handleSleep() {
    // X01: persistência UMA vez por tentativa de dormir; se o armamento do
    // wakeup falhar, repetir save() a cada passada do loop desgastaria flash
    static bool persisted = false;
    if (!persisted) {
        Serial.println("💤 Entering deep sleep");

        // Patch de horários: sleep PLANEJADO registrado como evidência
        // "solicitada" (não medição do tempo transcorrido — §6)
        BikeConfig& sleepConfig = configManager.getConfig();
        tripManager.notePlannedSleep(sleepConfig.power.deep_sleep_duration_sec);

        // F08/R04: persistir ANTES de encerrar a sessão; falha do checkpoint é
        // propagada e diagnosticada (última arquivo íntegra permanece no LittleFS)
        if (!bufferManager.save()) {
            Serial.println("🚨 CHECKPOINT FAILED BEFORE SLEEP - newest data not persisted");
        }
        bufferManager.endSession();
        persisted = true;
    }

    // Deep sleep for configured duration
    BikeConfig& config = configManager.getConfig();
    if (!powerManager.enterDeepSleep(config.power.deep_sleep_duration_sec)) {
        // X01: dormir sem wake-up armado seria brick — aguarda e tenta de novo
        Serial.println("🚨 Deep sleep not armed - retrying in 10s");
        delay(10000);
        return;
    }
    persisted = false; // inalcançável na prática (noreturn), mantém o contrato
}
