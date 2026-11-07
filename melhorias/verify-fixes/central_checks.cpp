// Verificação nativa (host) — correções R02..R14 do firmware central.
// Compila os fontes REAIS de firmware/central/src com stubs de host.
// NÃO substitui teste em placa (rádio/flash/FreeRTOS reais).
#include <iostream>
#include "LittleFS.h"
#include "config_manager.h"
#include "config_credentials.h"
#include "buffer_manager.h"
#include "bpr_json_helper.h"
#include "bike_manager.h"
#include "binary_structs.h"
#include <CRC32.h>
#include <ArduinoJson.h>

// Instâncias que os fontes esperam como extern
ConfigManager configManager;
ConfigCredentials configCredentials;
BufferManager bufferManager;

// Stub: único símbolo de BikeManager referenciado pelo bpr_json_helper
int BikeManager::getConnectedCount() { return 0; }

void result(const std::string& name, const std::string& v) { std::cout << name << ": " << v << "\n"; }

// Fragmento JSON no formato produzido pela bike
static String makeFragment(const String& bikeId, uint32_t sessionId, uint16_t salt,
                           uint16_t seq, uint16_t total, bool isFinal) {
    DynamicJsonDocument doc(1024);
    doc["type"] = "data";
    doc["bike_id"] = bikeId;
    doc["session_id"] = sessionId;
    doc["session_salt"] = salt;
    doc["seq"] = seq;
    doc["total"] = total;
    doc["is_final"] = isFinal;
    if (!isFinal) {
        JsonArray scans = doc.createNestedArray("scans");
        JsonObject scan = scans.createNestedObject();
        scan["ts"] = 1000 + seq;
        scan["net_offset"] = 0;
        JsonArray nets = scan.createNestedArray("nets");
        JsonArray net = nets.createNestedArray();
        net.add("rede");
        net.add("AA:BB:CC:DD:EE:FF");
        net.add(-60);
        net.add(6);
    } else {
        doc["session_start_millis"] = 1000;
        doc["scan_count"] = total - 1;
        JsonArray battery = doc.createNestedArray("battery");
    }
    String out;
    serializeJson(doc, out);
    return out;
}

int main() {
    strcpy(configCredentials.getCredentials().base_id, "base01");
    strcpy(configCredentials.getCredentials().firebase_database_url, "https://example.invalid");

    // ---------- R02: capacidade por bike acomoda a sessão máxima legítima ----------
    // Reproduz o caso da revisão: 10 scans de 1 rede => 11 fragmentos; a
    // central precisava confirmar a sessão e recusava o 11º.
    {
        const int TOTAL = 11;
        bool allStored = true;
        for (uint16_t seq = 0; seq < TOTAL; seq++) {
            String frag = makeFragment("bpr-123456", 4242, 7, seq, TOTAL, seq == TOTAL - 1);
            if (!bufferManager.addBikeData("bpr-123456", frag)) allStored = false;
        }
        result("r02_all_11_fragments_stored", allStored ? "yes" : "no(BUG)");
        result("r02_stored_items", std::to_string(bufferManager.getTotalDataCount()));
        result("r02_complete_session",
               bufferManager.hasCompleteSession("bpr-123456", 4242, 7, TOTAL) ? "yes" : "no");
    }

    // ---------- R03: dedupe idempotente apesar do relógio ----------
    {
        String frag = makeFragment("bpr-123456", 5353, 9, 0, 3, false);
        bufferManager.addBikeData("bpr-123456", frag);
        mockMillis += 6000; // reenvio 6s depois (ACK perdido)
        bool ok = bufferManager.addBikeData("bpr-123456", frag);
        result("r03_duplicate_returns_success", ok ? "yes" : "no");
        result("r03_items_after_duplicate", std::to_string(bufferManager.getTotalDataCount()));
    }

    // ---------- R04: falha de escrita preserva o arquivo anterior ----------
    {
        int before = bufferManager.getTotalDataCount();
        mockWriteFailure = true;
        bool ok = bufferManager.addBikeData("bpr-123456", makeFragment("bpr-123456", 6464, 1, 0, 2, false));
        mockWriteFailure = false;
        result("r04_failed_write_returns_failure", ok ? "no(BUG)" : "yes");
        result("r04_items_unchanged_after_failure",
               std::to_string(bufferManager.getTotalDataCount() == before));
    }

    // ---------- R05/R06: lote com manifesto durável e chave estável ----------
    String batchKey1;
    {
        DynamicJsonDocument doc(4096);
        int items = bufferManager.prepareUploadBatch(doc, 4, batchKey1);
        result("r05_batch_prepared_items", std::to_string(items));
        result("r05_manifest_exists",
               LittleFS.exists("/buffer/batch-" + batchKey1 + ".manifest") ? "yes" : "no");

        // Reboot sem confirmação: reconciliação devolve os itens a pendente
        BufferManager rebooted;
        rebooted.begin();
        result("r05_pending_after_reconcile", std::to_string(rebooted.getPendingCount()));

        // Mesmo conjunto => MESMA chave (identidade, não horário)
        String batchKey2;
        DynamicJsonDocument doc2(4096);
        int items2 = rebooted.prepareUploadBatch(doc2, 4, batchKey2);
        result("r06_same_batch_key_after_retry", batchKey2 == batchKey1 ? "yes" : "no(BUG)");
        result("r06_retry_items", std::to_string(items2));

        // ---------- R05: confirmação remove EXATAMENTE os itens do lote ----------
        // Estado: 13 itens (12 das sessões 4242/5353 + extra 7575); o lote
        // cobre os 4 primeiros (4242 seq 0..3). Confirmar deve remover SOMBRE
        // esses 4 e deixar os 9 demais pendentes — inclusive o extra.
        String extra = makeFragment("bpr-123456", 7575, 3, 0, 2, false);
        rebooted.addBikeData("bpr-123456", extra);
        int itemsBeforeConfirm = rebooted.getTotalDataCount(); // 13
        bool confirmed = rebooted.markAsConfirmed(batchKey2);
        result("r05_confirm_by_batch", confirmed ? "yes" : "no");
        result("r05_items_after_confirm", std::to_string(rebooted.getTotalDataCount()));
        result("r05_removed_exactly_batch",
               std::to_string(itemsBeforeConfirm - rebooted.getTotalDataCount()));
        result("r05_pending_after_confirm", std::to_string(rebooted.getPendingCount()));

        // O extra (7575) sobreviveu? As sessões pendentes continuam sendo
        // 4242 (parcial), 5353 e 7575 => 3 sessões distintas
        uint16_t pendingSessions = 0;
        uint32_t pendingBytes = 0;
        rebooted.getBikePendingInfo("bpr-123456", pendingSessions, pendingBytes);
        result("r05_extra_item_survives_confirm", pendingSessions == 3 ? "yes" : "no(BUG)");

        // Lote confirmado não deixa manifesto para trás
        result("r05_manifest_removed",
               LittleFS.exists("/buffer/batch-" + batchKey2 + ".manifest") ? "no(BUG)" : "yes");

        // ---------- R10: mutex liberado logo após confirmar ----------
        // getPendingCount usa tryLock; lock vazado => tryLock falha => 0
        result("r10_mutex_released_after_confirm",
               rebooted.getPendingCount() == 9 ? "yes" : "no(BUG)");

        // Próximo lote segue disponível (itens remanescentes)
        DynamicJsonDocument doc3(4096);
        String key3;
        int items3 = rebooted.prepareUploadBatch(doc3, 4, key3);
        result("r05_next_batch_items", std::to_string(items3));
    }

    // ---------- R12: seq fora da janela não indexa fora do array ----------
    {
        // Arquivo com um único item seq=10/total=11 (caso da revisão, ASan/UBSan)
        BufferItemBin item;
        memset(&item, 0, sizeof(item));
        item.bikeId = bikeIdToInt("bpr-999999");
        item.size = 4;
        memcpy(item.data, "abcd", 4);
        CRC32 crc;
        crc.update(item.data, item.size);
        item.crc32 = crc.finalize();
        item.session_id = 8888;
        item.session_salt = 1;
        item.seq = 10;

        BufferFileHeader h;
        h.magic = BUFFER_MAGIC;
        h.version = BUFFER_VERSION;
        h.item_count = 1;
        h.last_update = 0;
        std::string buf((char*)&h, sizeof(h));
        buf.append((char*)&item, sizeof(item));
        mockFiles["/buffer/bpr-999999.bin"] = buf;

        bool complete = bufferManager.hasCompleteSession("bpr-999999", 8888, 1, 11);
        result("r12_out_of_range_seq_no_crash", complete ? "complete(BUG)" : "rejected");
    }

    // ---------- R14/R20: validação de configuração da central ----------
    {
        ConfigManager cfg;
        // CSV completo (32 campos) com ble_ms/sync_ms/error_ms/count_ms zerados
        bool zeroAccepted = cfg.updateFromCSV(
            "90,60,15,30,60,10000,15,100,0,0,0,0,1500,150,800,10,8000,5,30,3,300,"
            "50,80,5,256,1,64,20,10,0.5,1,24", 42);
        result("r14_zero_led_durations_rejected", zeroAccepted ? "no(BUG)" : "yes");

        bool incompleteAccepted = cfg.updateFromCSV("90,60,15", 43);
        result("r20_incomplete_csv_rejected", incompleteAccepted ? "no(BUG)" : "yes");

        bool noVersionApplied = cfg.updateFromCSV(
            "90,60,15,30,60,10000,15,100,2000,500,50,300,1500,150,800,10,8000,5,"
            "30,3,300,50,80,5,256,1,64,20,10,0.5,1,24", 0);
        result("r20_zero_remote_version_rejected", noVersionApplied ? "no(BUG)" : "yes");

        bool valid = cfg.updateFromCSV(
            "90,60,15,30,60,10000,15,100,2000,500,50,300,1500,150,800,10,8000,5,"
            "30,3,300,50,80,5,256,1,64,20,10,0.5,1,24", 42);
        result("r20_valid_csv_applied", valid ? "yes" : "no(BUG)");
        result("r20_last_update_is_remote_version",
               std::to_string(cfg.getConfig().last_update == 42));
    }

    // ===== Revalidação (novembro, revisão 2): N06/N07/N08/N09.3/O04 =====
    // Estado determinístico: remove spools/manifestos dos blocos anteriores
    for (auto it = mockFiles.begin(); it != mockFiles.end();) {
        if (it->first.rfind("/buffer/", 0) == 0 || it->first.rfind("/backup/", 0) == 0) {
            it = mockFiles.erase(it);
        } else {
            ++it;
        }
    }

    // ---------- N06: sessão de pior caso (501 fragmentos) é acomodada ----------
    // Reproduz E02 da revalidação: a bike legítima gera 501 fragmentos (50
    // scans × 20 redes com SSID de 32 aspas); o limite antigo (256/bike)
    // impedia a sessão de completar e a bike nunca recebia can_clear.
    {
        const uint16_t TOTAL = 501;
        bool allStored = true;
        for (uint16_t seq = 0; seq < TOTAL; seq++) {
            String frag = makeFragment("bpr-770001", 700, 1, seq, TOTAL, seq == TOTAL - 1);
            if (!bufferManager.addBikeData("bpr-770001", frag)) { allStored = false; break; }
        }
        result("n06_all_501_fragments_stored", allStored ? "yes" : "no(BUG)");
        result("n06_complete_worst_case_session",
               bufferManager.hasCompleteSession("bpr-770001", 700, 1, TOTAL) ? "yes" : "no(BUG)");
        // Limite por bike continua explícito: 513 (contrato violado) não completa
        result("n06_contract_violation_total_rejected",
               bufferManager.hasCompleteSession("bpr-770001", 700, 1, 513) ? "no(BUG)" : "yes");
        LittleFS.remove("/buffer/bpr-770001.bin");
    }

    // ---------- N07: falha de flag propaga; manifesto ilegível tem plano ----
    {
        // Cenário A: a PRIMEIRA flag é gravada e a segunda falha — estado
        // parcial real (item marcado sem recibo). O lote não pode ficar
        // "meio preparado": rollback mantém o manifesto para o reconcile
        for (uint16_t seq = 0; seq < 3; seq++) {
            bufferManager.addBikeData("bpr-770002",
                makeFragment("bpr-770002", 800, 2, seq, 3, seq == 2));
        }
        int pendingBefore = bufferManager.getPendingCount();

        mockSingleByteWriteBudget = 1; // 1ª flag passa; 2ª falha; rollback também falha
        DynamicJsonDocument docA(4096);
        String keyA;
        int prepared = bufferManager.prepareUploadBatch(docA, 4, keyA);
        mockSingleByteWriteBudget = -1;
        result("n07_flag_failure_aborts_batch", prepared > 0 ? "no(BUG)" : "yes");
        // Manifesto mantido para o reconcile (itens podem ter ficado marcados)
        bool manifestKept = !keyA.isEmpty() &&
            LittleFS.exists("/buffer/batch-" + keyA + ".manifest");
        result("n07_manifest_kept_for_retry", manifestKept ? "yes" : "no(BUG)");

        // Boot seguinte com escrita sã: reconciliação devolve TUDO a pendente
        BufferManager afterFlagFail;
        afterFlagFail.begin();
        result("n07_pending_restored_after_reconcile",
               afterFlagFail.getPendingCount() == pendingBefore ? "yes" : "no(BUG)");
        LittleFS.remove("/buffer/bpr-770002.bin");

        // Cenário B: manifesto ILEGÍVEL no boot — quarentena + flags resetadas
        // (itens marcados como enviados não ficam sem caminho de entrega)
        for (uint16_t seq = 0; seq < 2; seq++) {
            afterFlagFail.addBikeData("bpr-770003",
                makeFragment("bpr-770003", 900, 3, seq, 2, seq == 1));
        }
        DynamicJsonDocument docB(4096);
        String keyB;
        afterFlagFail.prepareUploadBatch(docB, 2, keyB); // marca 2 como enviadas
        result("n07_pending_before_corrupt_manifest",
               std::to_string(afterFlagFail.getPendingCount())); // esperado 0
        mockFiles["/buffer/batch-" + keyB + ".manifest"] = "{invalid json";

        BufferManager afterCorruptManifest;
        afterCorruptManifest.begin();
        result("n07_corrupt_manifest_quarantined",
               LittleFS.exists("/buffer/batch-" + keyB + ".manifest.corrupt") ? "yes" : "no(BUG)");
        result("n07_items_back_to_pending_after_quarantine",
               std::to_string(afterCorruptManifest.getPendingCount())); // esperado 2
        LittleFS.remove("/buffer/bpr-770003.bin");
        LittleFS.remove("/buffer/batch-" + keyB + ".manifest.corrupt");
        for (auto it = mockFiles.begin(); it != mockFiles.end();) {
            if (it->first.rfind("/buffer/batch-", 0) == 0) it = mockFiles.erase(it);
            else ++it;
        }
    }

    // ---------- N08: corrupção no meio não descarta os registros seguintes ----
    {
        // Arquivo com 3 registros válidos; o do MEIO é corrompido (CRC)
        for (uint16_t seq = 0; seq < 3; seq++) {
            bufferManager.addBikeData("bpr-770004",
                makeFragment("bpr-770004", 910 + seq, 4, 0, 2, false));
        }
        size_t recOffset = sizeof(BufferFileHeader) + sizeof(BufferItemBin) + 28;
        mockFiles["/buffer/bpr-770004.bin"][recOffset] =
            (char)(mockFiles["/buffer/bpr-770004.bin"][recOffset] ^ 0x01);

        // Leitura conservadora: a iteração PARA na corrupção (sem modo reparo,
        // o registro seguinte não é visto — nada é descartado por engano)
        uint16_t sessions = 0;
        uint32_t bytes = 0;
        bufferManager.getBikePendingInfo("bpr-770004", sessions, bytes);
        result("n08_conservative_read_stops_at_corruption",
               std::to_string(sessions)); // esperado 1 (registro 0)

        // Reparo na confirmação: o lote cobre o registro 0; a reescrita
        // preserva o registro 2 (DEPOIS do corrompido) e quarentena o original
        DynamicJsonDocument doc(4096);
        String key;
        int items = bufferManager.prepareUploadBatch(doc, 4, key);
        result("n08_batch_from_readable_records", std::to_string(items)); // esperado 1
        bool confirmed = bufferManager.markAsConfirmed(key);
        result("n08_confirm_with_corruption_succeeds", confirmed ? "yes" : "no(BUG)");
        result("n08_original_quarantined",
               LittleFS.exists("/buffer/bpr-770004.bin.corrupt") ? "yes" : "no(BUG)");
        uint16_t afterSessions = 0;
        uint32_t afterBytes = 0;
        bufferManager.getBikePendingInfo("bpr-770004", afterSessions, afterBytes);
        result("n08_valid_record_after_corruption_kept",
               std::to_string(afterSessions)); // esperado 1 (registro 2 sobreviveu)
        LittleFS.remove("/buffer/bpr-770004.bin");
        LittleFS.remove("/buffer/bpr-770004.bin.corrupt");
        for (auto it = mockFiles.begin(); it != mockFiles.end();) {
            if (it->first.rfind("/buffer/batch-", 0) == 0) it = mockFiles.erase(it);
            else ++it;
        }
    }

    // ---------- Seção 6: flags do lote caem nos registros CERTOS ----------
    // Invariante item/índice: após o lote, EXATAMENTE os itens do documento
    // estão marcados como enviados (a ordenação da chave estável não pode
    // dissociar o par — na placa a enumeração de diretório não é alfabética)
    {
        for (int seq = 0; seq < 2; seq++) {
            bufferManager.addBikeData("bpr-770005",
                makeFragment("bpr-770005", 950, 5, seq, 2, seq == 1));
            bufferManager.addBikeData("bpr-770006",
                makeFragment("bpr-770006", 960, 6, seq, 2, seq == 1));
        }
        DynamicJsonDocument doc(4096);
        String key;
        int items = bufferManager.prepareUploadBatch(doc, 4, key);
        result("s6_batch_items", std::to_string(items)); // esperado 4

        JsonArray dataArr = doc["data"].as<JsonArray>();
        int perBike05 = 0, perBike06 = 0;
        for (JsonObject o : dataArr) {
            const char* bid = o["bike_id"].as<const char*>();
            if (bid && strcmp(bid, "bpr-770005") == 0) perBike05++;
            if (bid && strcmp(bid, "bpr-770006") == 0) perBike06++;
        }
        uint16_t pend05 = 0, pend06 = 0;
        uint32_t b05 = 0, b06 = 0;
        bufferManager.getBikePendingInfo("bpr-770005", pend05, b05);
        bufferManager.getBikePendingInfo("bpr-770006", pend06, b06);
        // 2 itens por bike no lote => 0 pendentes em cada
        result("s6_flags_match_manifest_bike05",
               (perBike05 == 2 && pend05 == 0) ? "yes" : "no(BUG)");
        result("s6_flags_match_manifest_bike06",
               (perBike06 == 2 && pend06 == 0) ? "yes" : "no(BUG)");
        LittleFS.remove("/buffer/bpr-770005.bin");
        LittleFS.remove("/buffer/bpr-770006.bin");
        for (auto it = mockFiles.begin(); it != mockFiles.end();) {
            if (it->first.rfind("/buffer/batch-", 0) == 0) it = mockFiles.erase(it);
            else ++it;
        }
    }

    // ---------- N09.3: conteúdo inválido não vira config ativa ----------
    {
        CentralConfig broken;
        memset(&broken, 0, sizeof(broken));
        broken.intervals.sync_sec = 0; // tamanho certo, conteúdo inválido
        mockFiles["/config.bin"].assign((char*)&broken, sizeof(broken));

        ConfigManager cfg;
        bool ok = cfg.loadConfig();
        result("n09_central_invalid_content_rejected", ok ? "no(BUG)" : "yes");
        result("n09_central_defaults_restored",
               cfg.getConfig().intervals.sync_sec == 90 ? "yes" : "no(BUG)");
        LittleFS.remove("/config.bin");
        LittleFS.remove("/config.tmp");
        LittleFS.remove("/config.bak");
    }

    // ---------- O04: orçamento de BYTES do /backup entre lotes ----------
    // A limpeza não podia esperar o boot: 2 stamps acima do orçamento => o
    // mais antigo sai JÁ na passagem pelo begin()
    {
        for (auto it = mockFiles.begin(); it != mockFiles.end();) {
            if (it->first.rfind("/backup/", 0) == 0) it = mockFiles.erase(it);
            else ++it;
        }
        const size_t big = 110 * 1024; // 2 × 110 KB > BACKUP_MAX_BYTES (200 KB)
        mockFiles["/backup/1000_bpr-aa0001.bkp"] = std::string(big, 'x');
        mockFiles["/backup/2000_bpr-aa0001.bkp"] = std::string(big, 'x');

        BufferManager budget;
        budget.begin(); // cleanupOldBackups + enforceBackupBudget
        result("o04_oldest_backup_stamp_removed",
               LittleFS.exists("/backup/1000_bpr-aa0001.bkp") ? "no(BUG)" : "yes");
        result("o04_newest_backup_stamp_kept",
               LittleFS.exists("/backup/2000_bpr-aa0001.bkp") ? "yes" : "no(BUG)");
        LittleFS.remove("/backup/2000_bpr-aa0001.bkp");
    }

    return 0;
}
