/*
 * BPR — Spool de lotes e recibos (implementação). Ver header para o contrato.
 */
#include <LittleFS.h>
#include <freertos/semphr.h>
#include "batch_spool.h"

static SemaphoreHandle_t batchMutex = nullptr;

static bool batchLock() {
    if (!batchMutex) batchMutex = xSemaphoreCreateMutex();
    return xSemaphoreTake(batchMutex, pdMS_TO_TICKS(2000)) == pdTRUE;
}

static void batchUnlock() {
    if (batchMutex) xSemaphoreGive(batchMutex);
}

String BatchSpool::spoolPath(const String& bikeId, uint32_t sid, uint16_t salt) {
    return String(BATCH_SPOOL_DIR) + "/" + bikeId + "_" + String(sid) + "_" + String(salt) + ".bin";
}

String BatchSpool::receiptPath(const String& bikeId, uint32_t sid, uint16_t salt) {
    return String(BATCH_RECEIPTS_DIR) + "/" + bikeId + "_" + String(sid) + "_" + String(salt) + ".receipt";
}

size_t BatchSpool::partOffset(uint16_t index) {
    return sizeof(BatchSpoolHeader) + (size_t)index * sizeof(BatchPartRecord);
}

bool BatchSpool::readHeader(const String& path, BatchSpoolHeader& h) {
    if (!LittleFS.exists(path)) return false;
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    bool ok = f.readBytes((char*)&h, sizeof(h)) == sizeof(h) &&
              h.magic == BATCH_SPOOL_MAGIC && h.version == BATCH_SPOOL_VERSION;
    f.close();
    return ok;
}

bool BatchSpool::writeHeader(const String& path, const BatchSpoolHeader& h) {
    File f = LittleFS.open(path, "r+");
    if (!f) return false;
    bool ok = f.seek(0) && f.write((uint8_t*)&h, sizeof(h)) == sizeof(h);
    f.close();
    return ok;
}

bool BatchSpool::find(const String& bikeId, uint32_t sid, uint16_t salt,
                      BatchSpoolHeader& out) {
    if (!batchLock()) return false;
    bool ok = readHeader(spoolPath(bikeId, sid, salt), out);
    batchUnlock();
    return ok;
}

bool BatchSpool::hasReceipt(const String& bikeId, uint32_t sid, uint16_t salt,
                            BatchReceiptData& out) {
    String path = receiptPath(bikeId, sid, salt);
    if (!LittleFS.exists(path)) return false;
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    bool ok = f.readBytes((char*)&out, sizeof(out)) == sizeof(out) &&
              out.magic == BATCH_RECEIPT_MAGIC &&
              out.version == BATCH_SPOOL_VERSION;
    f.close();
    return ok;
}

int BatchSpool::countAdmitted() {
    if (!LittleFS.exists(BATCH_SPOOL_DIR)) return 0;
    File root = LittleFS.open(BATCH_SPOOL_DIR);
    if (!root) return 0;
    int count = 0;
    File f = root.openNextFile();
    while (f) {
        String name = f.name();
        f.close();
        if (name.endsWith(".bin")) {
            BatchSpoolHeader h;
            if (readHeader(String(BATCH_SPOOL_DIR) + "/" + name, h) &&
                (h.state == BATCH_STATE_RECEIVING || h.state == BATCH_STATE_HASH_MISMATCH)) {
                count++;
            }
        }
        f = root.openNextFile();
    }
    root.close();
    return count;
}

bool BatchSpool::admit(const String& bikeId, uint32_t sid, uint16_t salt,
                       uint16_t total, uint32_t totalBytes, const uint8_t sha[32],
                       String& reason) {
    if (total == 0 || total > BATCH_PROTO_MAX_PARTS) {
        reason = "total fora do contrato";
        return false;
    }

    if (!batchLock()) {
        reason = "bloqueio interno";
        return false;
    }

    // Identidade já concluída? O recibo responde a autorização (T11/T18)
    BatchReceiptData receipt;
    if (hasReceipt(bikeId, sid, salt, receipt)) {
        batchUnlock();
        reason = "receipt_exists";
        return false; // chamador responde autorização pelo recibo
    }

    // Lote em andamento: MESMA identidade+conteúdo = retomada; conteúdo
    // diferente com a mesma identidade = conflito explícito (PRD 4.1)
    BatchSpoolHeader existing;
    String path = spoolPath(bikeId, sid, salt);
    if (readHeader(path, existing)) {
        if (existing.total_parts == total && existing.total_bytes == totalBytes &&
            memcmp(existing.sha256, sha, 32) == 0) {
            batchUnlock();
            reason = "resume"; // retomada: responde pendências
            return false;      // não é admissão nova
        }
        batchUnlock();
        reason = "identity_conflict";
        return false;
    }

    // Capacidade (PRD §6/T15): espaço para o lote INTEIRO + margem e limite
    // de lotes simultâneos em recepção
    size_t needed = sizeof(BatchSpoolHeader) + (size_t)total * sizeof(BatchPartRecord);
    size_t freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
    if (freeBytes < needed + BATCH_ADMISSION_MARGIN_BYTES) {
        batchUnlock();
        reason = "sem espaço para o lote completo";
        return false;
    }
    if (countAdmitted() >= MAX_ADMITTED_BATCHES) {
        batchUnlock();
        reason = "lotes em recepção no limite";
        return false;
    }

    if (!LittleFS.exists(BATCH_SPOOL_DIR)) {
        LittleFS.mkdir(BATCH_SPOOL_DIR);
    }
    if (!LittleFS.exists(BATCH_RECEIPTS_DIR)) {
        LittleFS.mkdir(BATCH_RECEIPTS_DIR);
    }

    BatchSpoolHeader h;
    memset(&h, 0, sizeof(h));
    h.magic = BATCH_SPOOL_MAGIC;
    h.version = BATCH_SPOOL_VERSION;
    strncpy(h.bike_id, bikeId.c_str(), sizeof(h.bike_id) - 1);
    h.bike_id[sizeof(h.bike_id) - 1] = '\0';
    h.session_id = sid;
    h.session_salt = salt;
    h.total_parts = total;
    h.total_bytes = totalBytes;
    memcpy(h.sha256, sha, 32);
    h.state = BATCH_STATE_RECEIVING;
    h.created_at = millis();
    h.updated_at = millis();

    File f = LittleFS.open(path, "w");
    if (!f) {
        batchUnlock();
        reason = "não criou o spool";
        return false;
    }
    // Pré-escreve o arquivo completo (header + registros zerados): offsets
    // fixos ficam coerentes e o espaço fica reservado
    bool ok = f.write((uint8_t*)&h, sizeof(h)) == sizeof(h);
    BatchPartRecord empty;
    memset(&empty, 0, sizeof(empty));
    for (uint16_t i = 0; ok && i < total; i++) {
        ok = f.write((uint8_t*)&empty, sizeof(empty)) == sizeof(empty);
    }
    f.close();
    if (!ok) {
        LittleFS.remove(path);
        batchUnlock();
        reason = "gravação do spool falhou";
        return false;
    }

    batchUnlock();
    Serial.printf("📦 Lote admitido: %s sessão %lu-%d (%d partes, %lu bytes)\n",
                  bikeId.c_str(), (unsigned long)sid, salt, total,
                  (unsigned long)totalBytes);
    return true;
}

int BatchSpool::addPart(const String& bikeId, uint32_t sid, uint16_t salt,
                        uint16_t index, const uint8_t* bytes, uint16_t len) {
    if (len == 0 || len > BATCH_PROTO_MAX_PART_BYTES) return 0;

    if (!batchLock()) return 0;

    BatchSpoolHeader h;
    String path = spoolPath(bikeId, sid, salt);
    if (!readHeader(path, h)) {
        batchUnlock();
        return 0; // lote não admitido — chamador decide (rejeita)
    }

    // T04: índice fora do intervalo não avança completude
    if (index >= h.total_parts) {
        batchUnlock();
        Serial.printf("🚨 Parte %d fora do intervalo (total %d) - rejeitada\n",
                      index, h.total_parts);
        return 0;
    }

    // CRC da variante canônica do contrato
    uint32_t crc = batch_crc32(bytes, len);

    // Duplicata/conflito (T01/T03): bit já presente -> compara o registro
    if (h.part_bitmap[index / 8] & (1u << (index % 8))) {
        BatchPartRecord rec;
        File rf = LittleFS.open(path, "r");
        bool same = false;
        if (rf) {
            same = rf.seek(partOffset(index)) &&
                   rf.readBytes((char*)&rec, sizeof(rec)) == sizeof(rec) &&
                   rec.len == len && rec.crc32 == crc &&
                   memcmp(rec.data, bytes, len) == 0;
            rf.close();
        }
        batchUnlock();
        if (same) return 1;  // duplicata idêntica: conta uma vez
        return -1;           // mesmo índice, conteúdo diferente: conflito
    }

    // Persiste o registro ANTES do bitmap (T06: só confirma após persistir;
    // crash no meio deixa órfão que o reenvio sobrescreve)
    File f = LittleFS.open(path, "r+");
    if (!f) {
        batchUnlock();
        return 0;
    }
    BatchPartRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.len = len;
    rec.crc32 = crc;
    memcpy(rec.data, bytes, len);
    bool ok = f.seek(partOffset(index)) &&
              f.write((uint8_t*)&rec, sizeof(rec)) == sizeof(rec);
    f.close();
    if (!ok) {
        batchUnlock();
        return 0;
    }

    // Releitura confirma a gravação (detecção de escrita incompleta — PRD 4.2)
    BatchPartRecord check;
    File vf = LittleFS.open(path, "r");
    bool verified = vf && vf.seek(partOffset(index)) &&
                    vf.readBytes((char*)&check, sizeof(check)) == sizeof(check) &&
                    check.len == len && check.crc32 == crc;
    vf.close();
    if (!verified) {
        batchUnlock();
        return 0;
    }

    h.part_bitmap[index / 8] |= (uint8_t)(1u << (index % 8));
    h.updated_at = millis();
    if (!writeHeader(path, h)) {
        batchUnlock();
        return 0; // registro persistiu mas bitmap não: parte NÃO confirmada
    }

    uint16_t present = 0;
    for (uint16_t i = 0; i < h.total_parts; i++) {
        if (h.part_bitmap[i / 8] & (1u << (i % 8))) present++;
    }
    batchUnlock();
    Serial.printf("📥 Parte %d/%d persistida (%d/%d presentes)\n",
                  index, h.total_parts, present, h.total_parts);
    return 1;
}

bool BatchSpool::checkComplete(const String& bikeId, uint32_t sid, uint16_t salt) {
    if (!batchLock()) return false;

    BatchSpoolHeader h;
    String path = spoolPath(bikeId, sid, salt);
    if (!readHeader(path, h)) {
        batchUnlock();
        return false;
    }

    // Conjunto de índices exatamente o esperado (PRD 4.3)
    uint16_t present = 0;
    for (uint16_t i = 0; i < h.total_parts; i++) {
        if (h.part_bitmap[i / 8] & (1u << (i % 8))) present++;
    }
    if (present != h.total_parts) {
        batchUnlock();
        return false; // ainda recebendo
    }

    // Hash global sobre a representação canônica, lendo os bytes persistidos
    batch_hash_input input;
    input.begin(h.bike_id, h.session_id, h.session_salt,
                h.total_parts, h.total_bytes);
    uint32_t totalBytes = 0;
    File f = LittleFS.open(path, "r");
    if (!f) {
        batchUnlock();
        return false;
    }
    for (uint16_t i = 0; i < h.total_parts; i++) {
        BatchPartRecord rec;
        if (!f.seek(partOffset(i)) ||
            f.readBytes((char*)&rec, sizeof(rec)) != sizeof(rec)) {
            f.close();
            batchUnlock();
            return false;
        }
        input.part(i, rec.data, rec.len);
        totalBytes += rec.len;
    }
    f.close();

    uint8_t sha[32];
    input.final(sha);

    if (totalBytes != h.total_bytes || memcmp(sha, h.sha256, 32) != 0) {
        // T05: checksums por parte ok, hash global divergente — lote fica
        // PENDENTE para diagnóstico; nenhum recibo, nenhuma autorização
        h.state = BATCH_STATE_HASH_MISMATCH;
        h.updated_at = millis();
        writeHeader(path, h);
        char got[65], want[65];
        batch_sha256_hex(sha, got);
        batch_sha256_hex(h.sha256, want);
        batchUnlock();
        Serial.printf("🚨 Hash global divergente (lote %lu-%d): %s != %s — pendente\n",
                      (unsigned long)sid, salt, got, want);
        return false;
    }

    h.state = BATCH_STATE_COMPLETE_LOCAL;
    h.updated_at = millis();
    bool ok = writeHeader(path, h);
    batchUnlock();
    if (ok) {
        Serial.printf("✅ Lote %lu-%d completo localmente (%d partes, %lu bytes)\n",
                      (unsigned long)sid, salt, h.total_parts,
                      (unsigned long)h.total_bytes);
    }
    return ok;
}

bool BatchSpool::missingPage(const String& bikeId, uint32_t sid, uint16_t salt,
                             uint8_t page, uint8_t pages, uint8_t out[8]) {
    if (!batchLock()) return false;
    BatchSpoolHeader h;
    if (!readHeader(spoolPath(bikeId, sid, salt), h) || page >= pages) {
        batchUnlock();
        return false;
    }
    memset(out, 0, 8);
    for (int b = 0; b < 8; b++) {
        size_t byteIdx = (size_t)page * 8 + b;
        if (byteIdx < sizeof(h.part_bitmap)) out[b] = h.part_bitmap[byteIdx];
    }
    batchUnlock();
    return true;
}

std::vector<BatchSpoolHeader> BatchSpool::listReadyForUpload() {
    std::vector<BatchSpoolHeader> result;
    if (!LittleFS.exists(BATCH_SPOOL_DIR)) return result;
    File root = LittleFS.open(BATCH_SPOOL_DIR);
    if (!root) return result;
    File f = root.openNextFile();
    while (f) {
        String name = f.name();
        f.close();
        if (name.endsWith(".bin")) {
            BatchSpoolHeader h;
            if (readHeader(String(BATCH_SPOOL_DIR) + "/" + name, h) &&
                h.state == BATCH_STATE_COMPLETE_LOCAL) {
                result.push_back(h);
            }
        }
        f = root.openNextFile();
    }
    root.close();
    return result;
}

std::vector<BatchSpoolHeader> BatchSpool::listAwaitingCloud() {
    std::vector<BatchSpoolHeader> result;
    if (!LittleFS.exists(BATCH_SPOOL_DIR)) return result;
    File root = LittleFS.open(BATCH_SPOOL_DIR);
    if (!root) return result;
    File f = root.openNextFile();
    while (f) {
        String name = f.name();
        f.close();
        if (name.endsWith(".bin")) {
            BatchSpoolHeader h;
            if (readHeader(String(BATCH_SPOOL_DIR) + "/" + name, h) &&
                h.state == BATCH_STATE_AWAITING_CLOUD) {
                result.push_back(h);
            }
        }
        f = root.openNextFile();
    }
    root.close();
    return result;
}

bool BatchSpool::readPartForUpload(const BatchSpoolHeader& h, uint16_t index,
                                   BatchPartRecord& out) {
    if (index >= h.total_parts) return false;
    String path = spoolPath(String(h.bike_id), h.session_id, h.session_salt);
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    bool ok = f.seek(partOffset(index)) &&
              f.readBytes((char*)&out, sizeof(out)) == sizeof(out);
    f.close();
    return ok;
}

bool BatchSpool::markUploaded(const String& bikeId, uint32_t sid, uint16_t salt,
                              uint16_t index) {
    if (!batchLock()) return false;
    BatchSpoolHeader h;
    String path = spoolPath(bikeId, sid, salt);
    if (!readHeader(path, h) || index >= h.total_parts) {
        batchUnlock();
        return false;
    }
    h.uploaded_bitmap[index / 8] |= (uint8_t)(1u << (index % 8));
    h.updated_at = millis();
    bool ok = writeHeader(path, h);
    batchUnlock();
    return ok;
}

bool BatchSpool::markAwaitingCloud(const String& bikeId, uint32_t sid, uint16_t salt) {
    if (!batchLock()) return false;
    BatchSpoolHeader h;
    String path = spoolPath(bikeId, sid, salt);
    if (!readHeader(path, h)) {
        batchUnlock();
        return false;
    }
    h.state = BATCH_STATE_AWAITING_CLOUD;
    h.updated_at = millis();
    bool ok = writeHeader(path, h);
    batchUnlock();
    return ok;
}

bool BatchSpool::writeReceipt(const BatchSpoolHeader& h, uint32_t confirmedAt) {
    if (!LittleFS.exists(BATCH_RECEIPTS_DIR)) {
        LittleFS.mkdir(BATCH_RECEIPTS_DIR);
    }
    BatchReceiptData r;
    memset(&r, 0, sizeof(r));
    r.magic = BATCH_RECEIPT_MAGIC;
    r.version = BATCH_SPOOL_VERSION;
    strncpy(r.bike_id, h.bike_id, sizeof(r.bike_id) - 1);
    r.session_id = h.session_id;
    r.session_salt = h.session_salt;
    r.total_parts = h.total_parts;
    r.total_bytes = h.total_bytes;
    memcpy(r.sha256, h.sha256, 32);
    r.confirmed_at = confirmedAt;

    // Gravação recuperável (tmp + rename — mesmo padrão N03)
    String path = receiptPath(String(h.bike_id), h.session_id, h.session_salt);
    String tmp = path + ".tmp";
    File f = LittleFS.open(tmp, "w");
    if (!f) return false;
    bool ok = f.write((uint8_t*)&r, sizeof(r)) == sizeof(r);
    f.close();
    if (!ok) {
        LittleFS.remove(tmp);
        return false;
    }
    LittleFS.remove(path);
    if (!LittleFS.rename(tmp, path)) {
        LittleFS.remove(tmp);
        return false;
    }
    Serial.printf("🧾 Recibo persistido: %s lote %lu-%d (conf em %lu)\n",
                  h.bike_id, (unsigned long)h.session_id, h.session_salt,
                  (unsigned long)confirmedAt);
    return true;
}

bool BatchSpool::removeSpool(const String& bikeId, uint32_t sid, uint16_t salt) {
    return LittleFS.remove(spoolPath(bikeId, sid, salt));
}
