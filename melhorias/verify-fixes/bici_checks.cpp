// Verificação nativa (host) — correções do firmware bici (R01, R04, R07,
// R09, R17, R22, A04). Compila os fontes REAIS de firmware/bici/src.
// NÃO substitui teste em placa (rádio/flash/FreeRTOS reais).
#include <iostream>
#include "LittleFS.h"
#include "WiFi.h"
#include "esp_sleep.h"
#include "batch_proto.h"
#include <ArduinoJson.h>
#include "config_manager.h"
#include "buffer_manager.h"
#include "power_manager.h"
#include "scanning.h"
#include "trip_manager.h"
#include "constants.h"

// Instâncias esperadas pelos fontes
ConfigManager configManager;
BufferManager bufferManager;
PowerManager powerManager;
TripManager tripManager; // hook de sessão do buffer_manager (patch de horários)

void result(const std::string& name, const std::string& v) { std::cout << name << ": " << v << "\n"; }
void check(const std::string& name, bool ok) { result(name, ok ? "yes" : "no(BUG)"); }

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

        // N04: identidade do scan (idx) transmitida no fragmento de dados —
        // o consumidor remonta chunks de um mesmo scan por idx, não por ts
        bool idxPresent = false;
        for (uint16_t i = 0; i < total - 1; i++) {
            String out;
            if (buf.serializeFragment(i, out) && out.indexOf("\"idx\":") != -1) {
                idxPresent = true;
                break;
            }
        }
        result("n04_scan_idx_transmitted", idxPresent ? "yes" : "no(BUG)");
    }

    // ---------- N02: coleta completa além de 10 ciclos (produtor de bateria) ----------
    // Reproduz E01 da revalidação: com addBattery a cada scan, o isFull
    // antigo ("50 scans OU 10 baterias") parava a coleta em 10 scans.
    {
        mockNetworkCount = 3;
        ConfigManager cfg;
        strcpy(cfg.getConfig().bike_id, "bpr-123456");
        cfg.getConfig().wifi.scan_interval_sec = 1;   // intervalo curto p/ teste
        cfg.getConfig().wifi.scan_timeout_ms = 500;
        BufferManager buf;
        buf.beginBoot();
        buf.startSession("bpr-123456");
        ScanningState scanState(cfg, buf);

        int cycles = 0;
        while (cycles < 40 && buf.getCurrentSession().scan_count < 12) {
            mockMillis += 2000; // avança o relógio além do intervalo de scan
            for (int k = 0; k < 4; k++) scanState.update(); // start + polls + consume
            cycles++;
        }
        result("n02_cycles_run", std::to_string(cycles));
        result("n02_scans_collected",
               std::to_string(buf.getCurrentSession().scan_count)); // esperado 12
        result("n02_battery_capped_at_max",
               std::to_string(buf.getCurrentSession().battery_count)); // esperado 10
        result("n02_collection_continues_past_battery_cap",
               buf.isFull() ? "no(BUG)" : "yes"); // 12 < 50: sessão NÃO saturou
    }

    // ---------- N06 (lado bike): pior caso legítimo = 501 fragmentos ----------
    // Reproduz E02: 50 scans × 20 redes com SSID de 32 ASPAS (escape JSON
    // dobra os bytes) — o caso que a central antiga (limite 256) recusava.
    {
        BufferManager buf;
        buf.beginBoot();
        buf.startSession("bpr-123456");
        NetworkData quoted;
        memset(quoted.ssid, '"', 32); // 32 aspas: 64 B serializados
        quoted.ssid[32] = '\0';
        strcpy(quoted.bssid, "AA:BB:CC:DD:EE:FF");
        quoted.rssi = -60;
        quoted.channel = 6;
        NetworkData nets[MAX_NETWORKS_PER_SCAN];
        for (int i = 0; i < MAX_NETWORKS_PER_SCAN; i++) nets[i] = quoted;

        for (int s = 0; s < MAX_SCANS; s++) {
            buf.addScan(1000 + s, nets, MAX_NETWORKS_PER_SCAN);
        }
        uint16_t total = buf.fragmentCount();
        result("n06_worst_case_fragment_total", std::to_string(total)); // esperado 501

        bool allSerialized = true;
        size_t biggest = 0;
        for (uint16_t i = 0; i < total; i++) {
            String out;
            if (!buf.serializeFragment(i, out)) { allSerialized = false; break; }
            if (out.length() > biggest) biggest = out.length();
        }
        result("n06_all_worst_case_fragments_serialized", allSerialized ? "yes" : "no(BUG)");
        result("n06_biggest_fragment_bytes", std::to_string(biggest)); // ≤ 512
    }

    // ---------- N03 (bike): recuperação da janela de troca ----------
    // Corte de energia entre write(tmp) e rename: o canônico some e a cópia
    // de troca (ou o backup) precisa ser recuperada no boot seguinte.
    {
        BufferManager buf;
        buf.beginBoot();
        buf.startSession("bpr-123456");
        NetworkData n;
        strcpy(n.ssid, "rede");
        strcpy(n.bssid, "AA:BB:CC:DD:EE:FF");
        n.rssi = -60;
        n.channel = 6;
        buf.addScan(4242, &n, 1);
        buf.save(); // checkpoint íntegro em /session.bin

        // Cenário 1: checkpoint SEGUINTE gravou o tmp e a energia caiu
        // ANTES do rename — canônico ausente, staging presente
        mockFiles["/session.tmp"] = mockFiles["/session.bin"];
        LittleFS.remove("/session.bin");
        BufferManager recovered;
        bool ok = recovered.load();
        result("n03_recovered_from_staging", ok ? "yes" : "no(BUG)");
        result("n03_staging_scan_count",
               std::to_string(recovered.getCurrentSession().scan_count));

        // Cenário 2: queda no fallback .bak — canônico ausente, backup presente
        // (após o cenário 1, o canônico foi restaurado a partir do staging)
        mockFiles["/session.bak"] = mockFiles["/session.bin"];
        LittleFS.remove("/session.tmp");
        LittleFS.remove("/session.bin");
        BufferManager recoveredBak;
        bool okBak = recoveredBak.load();
        result("n03_recovered_from_backup", okBak ? "yes" : "no(BUG)");

        LittleFS.remove("/session.bin");
        LittleFS.remove("/session.bak");
        LittleFS.remove("/session.tmp");
        LittleFS.remove("/session.corrupt");
    }

    // ---------- N04 (bike): boot novo não adota o boot antigo ----------
    // O MEMBRO bootId permanece o da energização atual; a sessão recuperada
    // registra a origem e o fragmento final publica "resumed".
    {
        BufferManager buf;
        buf.beginBoot(); // boot "antigo"
        buf.startSession("bpr-123456");
        NetworkData n;
        strcpy(n.ssid, "rede");
        strcpy(n.bssid, "AA:BB:CC:DD:EE:FF");
        n.rssi = -60;
        n.channel = 6;
        buf.addScan(900000, &n, 1);
        buf.save();

        BufferManager resumed;
        resumed.beginBoot(); // boot NOVO (id diferente)
        resumed.load();
        String finalFrag;
        resumed.serializeFragment(resumed.fragmentCount() - 1, finalFrag);
        result("n04_final_marks_resumed_session",
               finalFrag.indexOf("\"resumed\":true") != -1 ? "yes" : "no(BUG)");
        result("n04_member_boot_keeps_current",
               resumed.getBootId() != 0 ? "yes" : "no");
        LittleFS.remove("/session.bin");
        LittleFS.remove("/session.tmp");
    }

    // ---------- N09.1: revisão MAIOR com conteúdo equivalente é adotada ----------
    // Antes: "!configChanged" recusava e a pendência da central nunca convergia.
    {
        ConfigManager cfg;
        strcpy(cfg.getConfig().bike_id, "bpr-123456");
        cfg.getConfig().version = 42;
        cfg.getConfig().wifi.scan_interval_sec = 300;

        bool adopted = cfg.processUpdate(
            "{\"config_version\":50,\"config\":{\"wifi\":{\"scan_interval_sec\":300}}}");
        result("n09_higher_revision_same_content_adopted", adopted ? "yes" : "no(BUG)");
        result("n09_version_after_adoption", std::to_string(cfg.getConfig().version));
    }

    // ---------- N09.2: campo inválido rejeita a operação INTEIRA ----------
    // Antes: intervalo válido era aplicado, o campo inválido silenciosamente
    // ignorado e a revisão confirmada com config pela metade.
    {
        ConfigManager cfg;
        strcpy(cfg.getConfig().bike_id, "bpr-123456");
        cfg.getConfig().version = 42;
        cfg.getConfig().wifi.scan_interval_sec = 300;

        bool rejected = cfg.processUpdate(
            "{\"config_version\":50,\"config\":{"
            "\"wifi\":{\"scan_interval_sec\":120,\"max_networks\":0}}}");
        result("n09_invalid_field_rejects_whole_update", rejected ? "no(BUG)" : "yes");
        result("n09_interval_untouched_after_rejection",
               std::to_string(cfg.getConfig().wifi.scan_interval_sec)); // esperado 300
        result("n09_version_untouched_after_rejection",
               std::to_string(cfg.getConfig().version)); // esperado 42
    }

    // ---------- X07: config persistida inválida preserva identidade ----------
    {
        ConfigManager cfg;
        strcpy(cfg.getConfig().bike_id, "bpr-123456");
        cfg.getConfig().version = 42;
        cfg.save();
        // Corrompe um campo no arquivo (ble.scan_time_sec = 0 => scan infinito)
        BikeConfig broken;
        memset(&broken, 0, sizeof(broken));
        strcpy(broken.bike_id, "bpr-123456");
        broken.version = 42;
        broken.wifi.scan_interval_sec = 300;
        broken.wifi.scan_timeout_ms = 5000;
        broken.wifi.max_networks = 20;
        strcpy(broken.ble.base_name, "BPR Central");
        broken.ble.scan_time_sec = 0; // INVÁLIDO
        broken.ble.connection_timeout_ms = 10000;
        broken.power.deep_sleep_duration_sec = 3600;
        broken.battery.critical_voltage = 3.2f;
        broken.battery.low_voltage = 3.45f;
        mockFiles["/config.bin"].assign((char*)&broken, sizeof(broken));

        ConfigManager loaded;
        bool ok = loaded.load();
        result("x07_invalid_persisted_config_rejected", ok ? "no(BUG)" : "yes");
        result("x07_identity_preserved",
               strcmp(loaded.getConfig().bike_id, "bpr-123456") == 0 ? "yes" : "no(BUG)");
        result("x07_safe_default_restored",
               loaded.getConfig().ble.scan_time_sec == 5 ? "yes" : "no(BUG)");
        LittleFS.remove("/config.bin");
        LittleFS.remove("/config.tmp");
        LittleFS.remove("/config.bak");
    }

    // ---------- PRD lotes/recibos (bike): freeze, estabilidade e limpeza --
    {
        // T07 bike: freeze persiste e o lote sobrevive a "reboot"
        {
            BufferManager buf;
            buf.beginBoot();
            buf.startSession("bpr-770012");
            NetworkData n;
            strcpy(n.ssid, "rede-c1");
            strcpy(n.bssid, "AA:BB:CC:DD:EE:01");
            n.rssi = -60;
            n.channel = 6;
            buf.addScan(1000, &n, 1);
            buf.addScan(2000, &n, 1);
            buf.addBattery(1500, 80);
            check("bt_freeze_closes_batch", buf.freezeBatch());

            uint16_t total = buf.getBatchTotalParts();
            result("bt_total_parts", std::to_string(total));
            check("bt_total_parts_nonzero", total > 1);

            // Hash recomputado das serializações ATUAIS == hash do freeze
            // (estabilidade dos bytes: reenvio/reboot produzem o mesmo lote)
            batch_hash_input h;
            uint32_t totalBytes = 0;
            std::vector<std::string> parts;
            for (uint16_t i = 0; i < total; i++) {
                String frag;
                if (!buf.serializeFragment(i, frag)) { check("bt_serialize_stable", false); break; }
                parts.push_back(frag.c_str());
                totalBytes += frag.length();
            }
            h.begin("bpr-770012", buf.getSessionId(), buf.getSessionSalt(), total, totalBytes);
            for (uint16_t i = 0; i < total; i++) {
                h.part(i, (const uint8_t*)parts[i].data(), (uint16_t)parts[i].size());
            }
            uint8_t sha[32];
            h.final(sha);
            char shaHex[65], batchHex[65];
            batch_sha256_hex(sha, shaHex);
            buf.getBatchSha256Hex(batchHex);
            check("bt_hash_matches_serializations", strcmp(shaHex, batchHex) == 0);
            result("bt_total_bytes", std::to_string(totalBytes));

            // T14: coleta DURANTE/APÓS o freeze fica FORA do lote — as partes
            // congeladas não mudam (bytes idênticos)
            buf.addScan(3000, &n, 1); // nova observação pós-freeze
            buf.addBattery(3500, 79);
            buf.save(); // checkpoint pós-coleta (como o ScanningState faz)
            bool stable = true;
            for (uint16_t i = 0; i < total; i++) {
                String frag;
                if (!buf.serializeFragment(i, frag) || frag != parts[i].c_str()) {
                    stable = false;
                    break;
                }
            }
            check("bt_new_collection_outside_batch", stable);
            check("bt_scan_count_grew", buf.getCurrentSession().scan_count == 3);
            check("bt_batch_scan_count_frozen", buf.getBatchScanCount() == 2);

            // "Reboot": nova instância recupera o lote congelado do arquivo
            BufferManager rec;
            rec.beginBoot();
            check("bt_load_recovers_frozen_batch", rec.load() && rec.isBatchFrozen());
            bool stableAfterReboot = true;
            for (uint16_t i = 0; i < total; i++) {
                String frag;
                if (!rec.serializeFragment(i, frag) || frag != parts[i].c_str()) {
                    stableAfterReboot = false;
                    break;
                }
            }
            check("bt_parts_identical_after_reboot", stableAfterReboot);
            char rebootHex[65];
            rec.getBatchSha256Hex(rebootHex);
            check("bt_hash_identical_after_reboot", strcmp(rebootHex, batchHex) == 0);

            // Autorização inválida (hash errado) NUNCA limpa (PRD 5.3)
            char wrong[65];
            strcpy(wrong, batchHex);
            wrong[0] = (wrong[0] == '0') ? '1' : '0';
            check("bt_wrong_hash_auth_rejected",
                  !rec.applyClearAuthorization(wrong, total, totalBytes));
            check("bt_nothing_cleared_on_reject", rec.getCurrentSession().scan_count == 3);

            // Autorização válida: persiste ANTES de limpar; limpeza cirúrgica
            check("bt_valid_auth_accepted",
                  rec.applyClearAuthorization(batchHex, total, totalBytes));
            check("bt_clear_executes", rec.performClearIfAuthorized());
            // T14: apenas o conjunto coberto (2 scans, 1 bateria) sai; a
            // coleta pós-lote (1 scan, 1 bateria) permanece
            result("bt_scans_after_clear", std::to_string(rec.getCurrentSession().scan_count));
            check("bt_post_batch_collection_preserved",
                  rec.getCurrentSession().scan_count == 1 &&
                  rec.getCurrentSession().battery_count == 1);
            check("bt_batch_reset_after_clear", !rec.isBatchFrozen());

            // T12: memória do lote limpo — "apaguei" pode ser repetido
            check("bt_cleared_batch_remembered",
                  rec.isBatchCleared(rec.getSessionId(), rec.getSessionSalt(), batchHex));
            check("bt_other_batch_not_remembered",
                  !rec.isBatchCleared(rec.getSessionId(), rec.getSessionSalt(), wrong));

            // T12 após reboot: a memória vem do arquivo
            BufferManager rec2;
            rec2.beginBoot();
            rec2.load();
            check("bt_cleared_memory_survives_reboot",
                  rec2.isBatchCleared(rec.getSessionId(), rec.getSessionSalt(), batchHex));

            LittleFS.remove("/session.bin");
            LittleFS.remove("/session.tmp");
            LittleFS.remove("/session.bak");
            LittleFS.remove("/session.corrupt");
        }
    }

    return 0;
}
