#include "scanning.h"
#include <WiFi.h>
#include "power_manager.h"

extern PowerManager powerManager;

ScanningState::ScanningState(ConfigManager& configMgr, BufferManager& bufferMgr)
    : configManager(configMgr), bufferManager(bufferMgr), lastScan(0), scanInFlight(false),
      scanStart(0), saturationLogged(false) {}

BikeState ScanningState::update() {
    BikeConfig& config = configManager.getConfig();

    if (scanInFlight) {
        // R01: scan assíncrono em andamento — sempre consulta, mesmo saturado
        performWiFiScan();
        lastScan = millis();
    } else if (bufferManager.isFull()) {
        // R17: buffer cheio => política de saturação explícita: não gastar
        // rádio com scans que não podem ser armazenados; a busca por base
        // continua no loop principal
        if (!saturationLogged) {
            Serial.println("⚠️ SESSION SATURATED (50 scans) - holding data, searching for base");
            saturationLogged = true;
        }
    } else if (millis() - lastScan > config.wifi.scan_interval_sec * 1000UL) {
        if (saturationLogged) {
            Serial.println("🔄 Buffer liberado - retomando coleta");
            saturationLogged = false;
        }
        performWiFiScan();
        lastScan = millis();
    }

    // Check battery (fonte única: PowerManager)
    if (powerManager.isLowBattery(config) && !config.dev_mode) {
        return STATE_SLEEP;
    }

    return STATE_SCANNING;
}

void ScanningState::performWiFiScan() {
    BikeConfig& config = configManager.getConfig();

    // R01: o scan assíncrono é INICIADO uma única vez; enquanto estiver em
    // andamento apenas consulta scanComplete() — re-chamar scanNetworks()
    // reinicia a varredura e o resultado nunca chega.
    if (!scanInFlight) {
        // scanNetworks(async=true) retorna WIFI_SCAN_RUNNING (-1) no início
        // bem-sucedido; o resultado vem depois via scanComplete(). O quarto
        // argumento é a duração máxima POR CANAL, não um prazo global.
        int16_t result = WiFi.scanNetworks(true, true, false, config.wifi.scan_timeout_ms);

        if (result == WIFI_SCAN_FAILED) {
            Serial.println("❌ WiFi scan failed to start");
            WiFi.scanDelete();
            delay(config.power.radio_coordination_delay_ms);
            return;
        }
        if (result == WIFI_SCAN_RUNNING) {
            scanInFlight = true;
            scanStart = millis();
            Serial.printf("📡 WiFi scan started (async, %dms/channel, deadline %dms)...\n",
                          config.wifi.scan_timeout_ms, config.wifi.scan_timeout_ms);
            return;
        }
        // Resultado imediato (não esperado com async, mas tolerado)
        consumeScanResults((int)result, config);
        return;
    }

    // Scan em andamento: consulta o resultado sem bloquear o loop
    int16_t complete = WiFi.scanComplete();
    if (complete == WIFI_SCAN_RUNNING) {
        if (millis() - scanStart > config.wifi.scan_timeout_ms) {
            // R01: prazo global esgotado — aborta e diagnóstica
            Serial.println("⚠️ WiFi scan global deadline reached - aborting scan");
            WiFi.scanDelete();
            scanInFlight = false;
            delay(config.power.radio_coordination_delay_ms);
        }
        return;
    }

    scanInFlight = false;
    if (complete == WIFI_SCAN_FAILED) {
        // R01: falha gera diagnóstico e recuperação limitada (próximo ciclo)
        Serial.println("❌ WiFi scan failed");
        WiFi.scanDelete();
        delay(config.power.radio_coordination_delay_ms);
        return;
    }

    consumeScanResults((int)complete, config);
}

void ScanningState::consumeScanResults(int networks, const BikeConfig& config) {
    // F09: acumula as redes DA varredura em um único registro de scan
    NetworkData found[min(MAX_NETWORKS_PER_SCAN, 32)];
    uint8_t savedCount = 0;

    for (int i = 0; i < networks && savedCount < config.wifi.max_networks; i++) {
        if (WiFi.RSSI(i) > config.wifi.rssi_threshold) {
            NetworkData& network = found[savedCount];

            String ssid = WiFi.SSID(i);
            strncpy(network.ssid, ssid.c_str(), sizeof(network.ssid) - 1);
            network.ssid[sizeof(network.ssid) - 1] = '\0';

            String bssid = WiFi.BSSIDstr(i);
            strncpy(network.bssid, bssid.c_str(), sizeof(network.bssid) - 1);
            network.bssid[sizeof(network.bssid) - 1] = '\0';

            network.rssi = WiFi.RSSI(i);
            network.channel = WiFi.channel(i);

            savedCount++;
            if (savedCount >= MAX_NETWORKS_PER_SCAN) break;
        }
    }
    WiFi.scanDelete();

    bool stored = false;
    if (savedCount > 0) {
        stored = bufferManager.addScan(millis(), found, savedCount);
        // A04: produtor ativo de telemetria de bateria na sessão
        bufferManager.addBattery(millis(), powerManager.getBatteryPercent());
        // F08: checkpoint após cada coleta
        if (stored && !bufferManager.save()) {
            // R04: checkpoint falhou — dado novo fica só em RAM; arquivo anterior íntegra
            Serial.println("⚠️ Session checkpoint failed - will retry on next scan");
        }
    }

    Serial.printf("📶 Found %d networks, saved %d in 1 scan record (stored: %s)\n",
                  networks, savedCount, stored ? "yes" : "no");

    // Radio coordination delay
    delay(config.power.radio_coordination_delay_ms);
}
