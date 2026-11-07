// Verificação nativa (host) — PRD de transferência de lotes, integridade e
// recibos (matriz de aceite T01..T18 aplicável ao host). Compila os fontes
// REAIS de firmware/central/src (batch_spool, bpr_json_helper) com o
// contrato header-only firmware/common/batch_proto.h. NÃO substitui placa.
#include <iostream>
#include <iomanip>
#include <sstream>
#include "LittleFS.h"
#include "batch_proto.h"
#include "batch_spool.h"
#include "bpr_json_helper.h"
#include "bike_manager.h"
#include "config_credentials.h"
#include <ArduinoJson.h>

// Instâncias/stubs esperados pelos fontes linkados
ConfigCredentials configCredentials;
int BikeManager::getConnectedCount() { return 0; } // stub (bpr_json_helper)

void result(const std::string& name, const std::string& v) { std::cout << name << ": " << v << "\n"; }
void check(const std::string& name, bool ok) { result(name, ok ? "yes" : "no(BUG)"); }

static std::string toHex(const uint8_t* d, size_t n) {
    std::ostringstream os;
    for (size_t i = 0; i < n; i++) os << std::hex << std::setw(2) << std::setfill('0') << (int)d[i];
    return os.str();
}

// Estado determinístico por cenário: remove TODOS os spools de lote
// (capacidade/admissão é global — lotes de cenários anteriores contam)
static void clearBatchSpools() {
    for (auto it = mockFiles.begin(); it != mockFiles.end();) {
        if (it->first.rfind("/buffer/batches/", 0) == 0 ||
            it->first.rfind("/buffer/receipts/", 0) == 0) {
            it = mockFiles.erase(it);
        } else {
            ++it;
        }
    }
}

// Lote sintético: partes de conteúdo determinístico
struct SynthBatch {
    std::string bikeId;
    uint32_t sid;
    uint16_t salt;
    uint16_t total;
    uint32_t totalBytes;
    uint8_t sha[32];
    std::vector<std::string> parts;
};

static SynthBatch makeBatch(const std::string& bike, uint32_t sid, uint16_t salt,
                            uint16_t total, char seed = 'A') {
    SynthBatch b;
    b.bikeId = bike; b.sid = sid; b.salt = salt; b.total = total;
    for (uint16_t i = 0; i < total; i++) {
        // parte ~120 B determinística (não precisa caber no orçamento p/ testes)
        std::string p = "{\"seq\":" + std::to_string(i) + ",\"data\":\"";
        for (int k = 0; k < 90; k++) p += (char)(seed + (i + k) % 26);
        p += "\"}";
        b.parts.push_back(p);
    }
    b.totalBytes = 0;
    for (auto& p : b.parts) b.totalBytes += p.size();
    batch_hash_input h;
    h.begin(bike.c_str(), sid, salt, total, b.totalBytes);
    for (uint16_t i = 0; i < total; i++) {
        h.part(i, (const uint8_t*)b.parts[i].data(), (uint16_t)b.parts[i].size());
    }
    h.final(b.sha);
    return b;
}

int main() {
    // ---------- Passo 1: contrato de bytes — vetores FIPS + CRC ----------
    check("t00_contract_selftest_fips_vectors", batch_proto_selftest());

    // ---------- T17 (lado C++): vetores de concordância com o backend ----
    // Casos fixos compartilhados — o verify-functions-batch.js computa os
    // mesmos e o run.sh compara as linhas t17_*
    {
        const char* bikes[3] = {"bpr-0aa001", "bpr-0bb002", "bpr-0cc003"};
        uint32_t sids[3] = {4242, 100, 4294967295u};
        uint16_t salts[3] = {7, 65535, 1};
        uint16_t totals[3] = {1, 3, 56};
        for (int c = 0; c < 3; c++) {
            SynthBatch b = makeBatch(bikes[c], sids[c], salts[c], totals[c], 'A' + c * 7);
            char hex[65];
            batch_sha256_hex(b.sha, hex);
            std::cout << "t17_case_" << c << " " << hex << "\n";
        }
    }

    LittleFS.mkdir("/buffer");

    // ---------- T01: 56 partes fora de ordem com duplicatas idênticas ----
    {
        SynthBatch b = makeBatch("bpr-0aa001", 4242, 7, 56);
        String reason;
        check("t01_admit", BatchSpool::admit(b.bikeId.c_str(), b.sid, b.salt,
                                             b.total, b.totalBytes, b.sha, reason));
        // ordem embaralhada + duplicatas idênticas (T01)
        int order[56];
        for (int i = 0; i < 56; i++) order[i] = (i * 17) % 56;
        bool allOk = true;
        for (int k = 0; k < 56; k++) {
            int stored = BatchSpool::addPart(b.bikeId.c_str(), b.sid, b.salt, order[k],
                                             (const uint8_t*)b.parts[order[k]].data(),
                                             (uint16_t)b.parts[order[k]].size());
            if (stored != 1) allOk = false;
            // duplicata idêntica logo em seguida — conta uma vez (T01)
            if (k % 7 == 0) {
                int dup = BatchSpool::addPart(b.bikeId.c_str(), b.sid, b.salt, order[k],
                                              (const uint8_t*)b.parts[order[k]].data(),
                                              (uint16_t)b.parts[order[k]].size());
                if (dup != 1) allOk = false;
            }
        }
        check("t01_out_of_order_with_duplicates_stored", allOk);
        check("t01_complete_after_all_parts", BatchSpool::checkComplete(b.bikeId.c_str(), b.sid, b.salt));

        BatchSpoolHeader h;
        BatchSpool::find(b.bikeId.c_str(), b.sid, b.salt, h);
        check("t01_state_complete_local", h.state == BATCH_STATE_COMPLETE_LOCAL);
        // T11: sem recibo ainda — nenhuma autorização disponível
        BatchReceiptData rec;
        check("t11_no_receipt_before_cloud", !BatchSpool::hasReceipt(b.bikeId.c_str(), b.sid, b.salt, rec));
    }

    clearBatchSpools();
    // ---------- T02: parte ausente/truncada — nenhum recibo --------------
    {
        SynthBatch b = makeBatch("bpr-0bb002", 5353, 9, 10);
        String reason;
        BatchSpool::admit(b.bikeId.c_str(), b.sid, b.salt, b.total, b.totalBytes, b.sha, reason);
        for (uint16_t i = 0; i < 9; i++) { // falta a 10ª (índice 9)
            BatchSpool::addPart(b.bikeId.c_str(), b.sid, b.salt, i,
                                (const uint8_t*)b.parts[i].data(), (uint16_t)b.parts[i].size());
        }
        check("t02_missing_part_not_complete", !BatchSpool::checkComplete(b.bikeId.c_str(), b.sid, b.salt));
        // truncada (len 0) é rejeitada sem avançar completude
        int r = BatchSpool::addPart(b.bikeId.c_str(), b.sid, b.salt, 9,
                                    (const uint8_t*)b.parts[9].data(), 0);
        check("t02_truncated_part_rejected", r == 0);
        check("t02_still_not_complete", !BatchSpool::checkComplete(b.bikeId.c_str(), b.sid, b.salt));
    }

    clearBatchSpools();
    // ---------- T03: mesmo índice com conteúdo diferente = conflito -------
    {
        SynthBatch b = makeBatch("bpr-0cc003", 6464, 1, 4);
        String reason;
        BatchSpool::admit(b.bikeId.c_str(), b.sid, b.salt, b.total, b.totalBytes, b.sha, reason);
        BatchSpool::addPart(b.bikeId.c_str(), b.sid, b.salt, 0,
                            (const uint8_t*)b.parts[0].data(), (uint16_t)b.parts[0].size());
        std::string other = b.parts[0] + "X";
        int r = BatchSpool::addPart(b.bikeId.c_str(), b.sid, b.salt, 0,
                                    (const uint8_t*)other.data(), (uint16_t)other.size());
        check("t03_conflicting_content_detected", r == -1);
        // duplicata idêntica segue idempotente
        int d = BatchSpool::addPart(b.bikeId.c_str(), b.sid, b.salt, 0,
                                    (const uint8_t*)b.parts[0].data(), (uint16_t)b.parts[0].size());
        check("t03_identical_duplicate_ok", d == 1);
    }

    clearBatchSpools();
    // ---------- T04: índice/total fora do contrato ------------------------
    {
        SynthBatch b = makeBatch("bpr-0dd004", 7575, 2, 8);
        String reason;
        BatchSpool::admit(b.bikeId.c_str(), b.sid, b.salt, b.total, b.totalBytes, b.sha, reason);
        int out = BatchSpool::addPart(b.bikeId.c_str(), b.sid, b.salt, 8,
                                      (const uint8_t*)b.parts[0].data(), (uint16_t)b.parts[0].size());
        check("t04_index_out_of_range_rejected", out == 0);
        // total acima do contrato não é admitido (T04)
        SynthBatch big = makeBatch("bpr-0dd004", 8686, 3, 513);
        String reasonBig;
        check("t04_total_above_contract_rejected",
              !BatchSpool::admit(big.bikeId.c_str(), big.sid, big.salt,
                                 big.total, big.totalBytes, big.sha, reasonBig));
    }

    clearBatchSpools();
    // ---------- T05: checksums ok, hash global divergente -----------------
    {
        // Anuncia o hash do lote B, mas entrega as partes do lote C (mesma
        // identidade, conteúdo diferente no CONJUNTO)
        SynthBatch good = makeBatch("bpr-0ee005", 1111, 4, 6);
        SynthBatch evil = makeBatch("bpr-0ee005", 1111, 4, 6, 'Z');
        String reason;
        check("t05_admit_with_announced_hash",
              BatchSpool::admit(good.bikeId.c_str(), good.sid, good.salt,
                                good.total, good.totalBytes, good.sha, reason));
        for (uint16_t i = 0; i < evil.total; i++) {
            BatchSpool::addPart(evil.bikeId.c_str(), evil.sid, evil.salt, i,
                                (const uint8_t*)evil.parts[i].data(),
                                (uint16_t)evil.parts[i].size());
        }
        check("t05_global_hash_mismatch_not_confirmed",
              !BatchSpool::checkComplete(evil.bikeId.c_str(), evil.sid, evil.salt));
        BatchSpoolHeader h;
        BatchSpool::find(evil.bikeId.c_str(), evil.sid, evil.salt, h);
        check("t05_state_hash_mismatch", h.state == BATCH_STATE_HASH_MISMATCH);
        BatchReceiptData rec;
        check("t05_no_receipt_on_mismatch",
              !BatchSpool::hasReceipt(evil.bikeId.c_str(), evil.sid, evil.salt, rec));
    }

    clearBatchSpools();
    // ---------- T06: falha de gravação depois da fila → sem confirmação ---
    {
        SynthBatch b = makeBatch("bpr-0ff006", 2222, 5, 3);
        String reason;
        BatchSpool::admit(b.bikeId.c_str(), b.sid, b.salt, b.total, b.totalBytes, b.sha, reason);
        mockWriteFailure = true;
        int r = BatchSpool::addPart(b.bikeId.c_str(), b.sid, b.salt, 0,
                                    (const uint8_t*)b.parts[0].data(), (uint16_t)b.parts[0].size());
        mockWriteFailure = false;
        check("t06_write_failure_not_confirmed", r == 0);
        BatchSpoolHeader h;
        BatchSpool::find(b.bikeId.c_str(), b.sid, b.salt, h);
        check("t06_bitmap_untouched", (h.part_bitmap[0] & 0x01) == 0);
        // Reenvio da mesma parte após a falha é aceito (recuperação — T06)
        int r2 = BatchSpool::addPart(b.bikeId.c_str(), b.sid, b.salt, 0,
                                     (const uint8_t*)b.parts[0].data(), (uint16_t)b.parts[0].size());
        check("t06_resend_after_failure_ok", r2 == 1);
    }

    clearBatchSpools();
    // ---------- T07/T13: recuperação de estado após "reboot" -------------
    {
        SynthBatch b = makeBatch("bpr-100007", 3333, 6, 12);
        String reason;
        BatchSpool::admit(b.bikeId.c_str(), b.sid, b.salt, b.total, b.totalBytes, b.sha, reason);
        for (uint16_t i = 0; i < 6; i++) { // metade
            BatchSpool::addPart(b.bikeId.c_str(), b.sid, b.salt, i,
                                (const uint8_t*)b.parts[i].data(), (uint16_t)b.parts[i].size());
        }
        // "Reboot": o estado vem do ARQUIVO (nada em RAM)
        BatchSpoolHeader h;
        check("t07_state_recovered_from_disk", BatchSpool::find(b.bikeId.c_str(), b.sid, b.salt, h));
        uint16_t present = 0;
        for (uint16_t i = 0; i < h.total_parts; i++) {
            if (h.part_bitmap[i / 8] & (1u << (i % 8))) present++;
        }
        check("t07_progress_recovered", present == 6);
        // Retomada: mesmo announce (identidade+conteúdo) → "resume", sem recriar
        String reason2;
        bool admitted = BatchSpool::admit(b.bikeId.c_str(), b.sid, b.salt,
                                          b.total, b.totalBytes, b.sha, reason2);
        check("t07_retry_keeps_same_batch", !admitted && reason2 == "resume");
        // Complete na retomada
        for (uint16_t i = 6; i < b.total; i++) {
            BatchSpool::addPart(b.bikeId.c_str(), b.sid, b.salt, i,
                                (const uint8_t*)b.parts[i].data(), (uint16_t)b.parts[i].size());
        }
        check("t07_completes_after_resume", BatchSpool::checkComplete(b.bikeId.c_str(), b.sid, b.salt));
    }

    clearBatchSpools();
    // ---------- T08/T11: recibo sobrevive à remoção do payload -----------
    {
        SynthBatch b = makeBatch("bpr-110008", 4444, 7, 5);
        String reason;
        BatchSpool::admit(b.bikeId.c_str(), b.sid, b.salt, b.total, b.totalBytes, b.sha, reason);
        for (uint16_t i = 0; i < b.total; i++) {
            BatchSpool::addPart(b.bikeId.c_str(), b.sid, b.salt, i,
                                (const uint8_t*)b.parts[i].data(), (uint16_t)b.parts[i].size());
        }
        BatchSpool::checkComplete(b.bikeId.c_str(), b.sid, b.salt);

        BatchSpoolHeader h;
        BatchSpool::find(b.bikeId.c_str(), b.sid, b.salt, h);
        // PRD 5.2: recibo ANTES de remover o payload
        check("t08_receipt_written", BatchSpool::writeReceipt(h, 1731000000u));
        check("t08_spool_removed_after_receipt",
              BatchSpool::removeSpool(b.bikeId.c_str(), b.sid, b.salt));

        // T11: a autorização é repetida pelo recibo SEM payload
        BatchReceiptData rec;
        check("t11_receipt_available_without_payload",
              BatchSpool::hasReceipt(b.bikeId.c_str(), b.sid, b.salt, rec));
        char shaHex[65];
        batch_sha256_hex(rec.sha256, shaHex);
        String auth = BPRJsonHelper::createBatchClearAuth(
            b.bikeId.c_str(), rec.session_id, rec.session_salt,
            rec.total_parts, rec.total_bytes, shaHex, rec.confirmed_at);
        check("t11_auth_from_receipt_identifies_batch",
              auth.indexOf("\"type\":\"batch_clear_auth\"") != -1 &&
              auth.indexOf(shaHex) != -1);
        // T18 (lado central): nova "visita" com o mesmo announce → recibo
        BatchSpoolHeader gone;
        check("t18_no_spool_but_receipt_answers",
              !BatchSpool::find(b.bikeId.c_str(), b.sid, b.salt, gone) &&
              BatchSpool::hasReceipt(b.bikeId.c_str(), b.sid, b.salt, rec));
    }

    clearBatchSpools();
    // ---------- T15: capacidade/admissão serializada ---------------------
    {
        SynthBatch b1 = makeBatch("bpr-120009", 6001, 1, 20);
        SynthBatch b2 = makeBatch("bpr-130010", 6002, 2, 20);
        SynthBatch b3 = makeBatch("bpr-140011", 6003, 3, 20);
        String r1, r2, r3;
        bool a1 = BatchSpool::admit(b1.bikeId.c_str(), b1.sid, b1.salt, b1.total, b1.totalBytes, b1.sha, r1);
        bool a2 = BatchSpool::admit(b2.bikeId.c_str(), b2.sid, b2.salt, b2.total, b2.totalBytes, b2.sha, r2);
        bool a3 = BatchSpool::admit(b3.bikeId.c_str(), b3.sid, b3.salt, b3.total, b3.totalBytes, b3.sha, r3);
        check("t15_first_two_admitted", a1 && a2);
        check("t15_third_rejected_busy", !a3);
        result("t15_rejection_reason", r3);

        // Retomada de lote JÁ admitido não gasta nova vaga
        String r1b;
        bool again = BatchSpool::admit(b1.bikeId.c_str(), b1.sid, b1.salt,
                                       b1.total, b1.totalBytes, b1.sha, r1b);
        check("t15_retry_does_not_consume_slot", !again && r1b == "resume");
    }

    // ---------- T16: mensagens dentro do orçamento de aplicação ----------
    {
        const uint8_t zeros[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        String page = BPRJsonHelper::createBatchStatusPage(
            "bpr-150012", 4294967295u, 65535, "receiving", 7, 8, zeros);
        result("t16_status_page_bytes", std::to_string(page.length())); // <= 460
        check("t16_status_page_within_budget", page.length() <= 460);

        char shaHex[65];
        batch_sha256_hex(makeBatch("bpr-150012", 1, 1, 1).sha, shaHex);
        String auth = BPRJsonHelper::createBatchClearAuth(
            "bpr-150012", 4294967295u, 65535, 512, 4294967295u, shaHex, 4294967295u);
        result("t16_clear_auth_bytes", std::to_string(auth.length()));
        check("t16_clear_auth_within_budget", auth.length() <= 460);
    }

    return 0;
}
