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

    return 0;
}
