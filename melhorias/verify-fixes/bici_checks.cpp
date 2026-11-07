// Verificação nativa (host) — correções do firmware bici (R01, R04, R07,
// R09, R17, R22, A04). Compila os fontes REAIS de firmware/bici/src.
// NÃO substitui teste em placa (rádio/flash/FreeRTOS reais).
#include <iostream>
#include "LittleFS.h"
#include "WiFi.h"
#include "esp_sleep.h"
#include <ArduinoJson.h>
#include "config_manager.h"
#include "buffer_manager.h"
#include "power_manager.h"
#include "scanning.h"
#include "constants.h"

// Instâncias esperadas pelos fontes
ConfigManager configManager;
BufferManager bufferManager;
PowerManager powerManager;

void result(const std::string& name, const std::string& v) { std::cout << name << ": " << v << "\n"; }

int main() {
    // ---------- R01: scan assíncrono consumido via scanComplete ----------
    // Reproduz o caso da revisão: scanNetworks(async) devolvia RUNNING (-1),
    // tratado como contagem => 0 redes coletadas.
    {
        mockNetworkCount = 3;
        ConfigManager cfg;
        strcpy(cfg.getConfig().bike_id, "bpr-123456");
        cfg.getConfig().wifi.scan_interval_sec = 5; // intervalo curto p/ teste
        BufferManager buf;
        buf.beginBoot();
        buf.startSession("bpr-123456");
        ScanningState scanState(cfg, buf);

        // update() inicia o scan; polls seguintes consultam o resultado
        int loops = 0;
        while (loops < 10) {
            scanState.update();
            loops++;
            if (buf.getCurrentSession().scan_count > 0) break;
        }
        result("r01_scan_loops_until_result", std::to_string(loops));
        result("r01_scan_records", std::to_string(buf.getCurrentSession().scan_count));
        result("r01_networks_in_record",
               std::to_string(buf.getCurrentSession().scans[0].network_count));

        // A04: produtor ativo de bateria na sessão
        result("a04_battery_samples_recorded",
               std::to_string(buf.getCurrentSession().battery_count > 0));
    }

    // ---------- R17: saturação diagnosticável (50 scans) ----------
    {
        BufferManager buf;
        buf.beginBoot();
        buf.startSession("bpr-123456");
        NetworkData n;
        strcpy(n.ssid, "rede");
        strcpy(n.bssid, "AA:BB:CC:DD:EE:FF");
        n.rssi = -60;
        n.channel = 6;
        int stored = 0;
        for (int i = 0; i < 60; i++) {
            if (buf.addScan(1000 + i, &n, 1)) stored++;
        }
        result("r17_scans_accepted_until_cap", std::to_string(stored));
        result("r17_is_full_reported", buf.isFull() ? "yes" : "no");
    }

    // ---------- R04/R22: gravação atômica + CRC do payload ----------
    {
        BufferManager buf;
        buf.beginBoot();
        buf.startSession("bpr-123456");
        NetworkData n;
        strcpy(n.ssid, "rede");
        strcpy(n.bssid, "AA:BB:CC:DD:EE:FF");
        n.rssi = -60;
        n.channel = 6;
        buf.addScan(1000, &n, 1);
        result("r04_first_save", buf.save() ? "ok" : "failed(BUG)");

        // Escrita curta: arquivo anterior permanece íntegro
        mockWriteFailure = true;
        buf.addScan(2000, &n, 1);
        bool failedSave = buf.save();
        mockWriteFailure = false;
        result("r04_short_write_reports_failure", failedSave ? "no(BUG)" : "yes");

        BufferManager previous;
        bool recovered = previous.load();
        result("r04_previous_session_recovered", recovered ? "yes" : "no(BUG)");
        result("r04_recovered_scan_count",
               std::to_string(previous.getCurrentSession().scan_count));

        // Corrupção de 1 byte no payload: CRC detecta e quarentena
        mockFiles["/session.bin"][sizeof(SessionFileHeader) + 8] =
            (char)(mockFiles["/session.bin"][sizeof(SessionFileHeader) + 8] ^ 0x01);
        BufferManager corrupted;
        bool accepted = corrupted.load();
        result("r22_corrupted_payload_rejected", accepted ? "no(BUG)" : "yes");
        result("r22_quarantine_kept_copy",
               LittleFS.exists("/session.corrupt") ? "yes" : "no");
        LittleFS.remove("/session.corrupt");
        LittleFS.remove("/session.bin");
    }

    // ---------- R07: versão idempotente / antiga rejeitada ----------
    {
        ConfigManager cfg;
        strcpy(cfg.getConfig().bike_id, "bpr-123456");
        cfg.getConfig().version = 42;
        cfg.getConfig().wifi.scan_interval_sec = 300;

        // Mesma versão + mesmo conteúdo => ACK ok, versão intacta (idempotente)
        bool dup = cfg.processUpdate(
            "{\"config_version\":42,\"config\":{\"wifi\":{\"scan_interval_sec\":300}}}");
        result("r07_duplicate_version_ack", dup ? "yes" : "no(BUG)");
        result("r07_version_after_duplicate", std::to_string(cfg.getConfig().version));

        // Mesma versão + conteúdo DIVERGENTE => conflito rejeitado
        bool conflict = cfg.processUpdate(
            "{\"config_version\":42,\"config\":{\"wifi\":{\"scan_interval_sec\":60}}}");
        result("r07_conflict_same_version_rejected", conflict ? "no(BUG)" : "yes");

        // Versão maior => aplica e assume a versão remota
        bool newer = cfg.processUpdate(
            "{\"config_version\":50,\"config\":{\"wifi\":{\"scan_interval_sec\":120}}}");
        result("r07_newer_version_applied", newer ? "yes" : "no(BUG)");
        result("r07_version_after_newer", std::to_string(cfg.getConfig().version));
        result("r07_interval_after_newer",
               std::to_string(cfg.getConfig().wifi.scan_interval_sec));

        // Versão antiga => rejeitada, config intacta
        bool stale = cfg.processUpdate(
            "{\"config_version\":41,\"config\":{\"wifi\":{\"scan_interval_sec\":999}}}");
        result("r07_stale_version_rejected", stale ? "no(BUG)" : "yes");
        result("r07_interval_after_stale",
               std::to_string(cfg.getConfig().wifi.scan_interval_sec));
    }

    // ---------- R09: persistência verificada antes do sucesso ----------
    {
        ConfigManager cfg;
        strcpy(cfg.getConfig().bike_id, "bpr-123456");
        cfg.getConfig().version = 10;
        cfg.getConfig().wifi.scan_interval_sec = 300;
        cfg.save();

        mockWriteFailure = true;
        bool applied = cfg.processUpdate(
            "{\"config_version\":11,\"config\":{\"wifi\":{\"scan_interval_sec\":42}}}");
        mockWriteFailure = false;
        result("r09_failed_persistence_denies_ack", applied ? "no(BUG)" : "yes");
        result("r09_ram_unchaged_after_failure",
               std::to_string(cfg.getConfig().wifi.scan_interval_sec));

        ConfigManager reread;
        reread.load();
        result("r09_reboot_interval",
               std::to_string(reread.getConfig().wifi.scan_interval_sec));
    }

    // ---------- O01/R16: fragmento por índice, escapes e limite ----------
    {
        BufferManager buf;
        buf.beginBoot();
        buf.startSession("bpr-123456");
        NetworkData weird;
        strcpy(weird.ssid, "re\"de\\x");  // aspas e barra exigem escape JSON
        strcpy(weird.bssid, "AA:BB:CC:DD:EE:FF");
        weird.rssi = -60;
        weird.channel = 6;
        buf.addScan(1000, &weird, 1);
        buf.addScan(2000, &weird, 1); // mesmo conteúdo, outro scan (índice distinto)

        uint16_t total = buf.fragmentCount();
        result("o01_fragment_total", std::to_string(total));
        bool allOk = true;
        String biggest;
        for (uint16_t i = 0; i < total; i++) {
            String out;
            if (!buf.serializeFragment(i, out)) { allOk = false; break; }
            if (out.length() > biggest.length()) biggest = out;
            // R16: dois scans com mesmos dados continuam distinguíveis por índice
            if (i < total - 1) {
                if (out.indexOf("\"ts\":2000") == -1 && out.indexOf("\"ts\":1000") == -1) {
                    allOk = false;
                }
            }
        }
        result("o01_all_fragments_serialized", allOk ? "yes" : "no(BUG)");
        result("o01_biggest_fragment_bytes", std::to_string(biggest.length()));
    }

    return 0;
}
