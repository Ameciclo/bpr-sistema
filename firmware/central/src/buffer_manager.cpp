#include <ArduinoJson.h>
#include <CRC32.h>
#include <LittleFS.h>
#include <freertos/semphr.h>
#include <algorithm>
#include <cstddef>
#include "binary_structs.h"
#include "bpr_json_helper.h"
#include "buffer_manager.h"
#include "config_manager.h"
#include "config_credentials.h"
#include "constants.h"

extern ConfigManager configManager;
extern ConfigCredentials configCredentials;

static SemaphoreHandle_t bufferMutex = nullptr;

// Converte um registro binário em DataItem em RAM
static void binToItem(const BufferItemBin& bin, DataItem& item, const String& bikeId) {
    item.bikeId = bikeId;
    item.timestamp = bin.timestamp;
    item.size = bin.size;
    item.crc32 = bin.crc32;
    item.session_id = bin.session_id;
    item.session_salt = bin.session_salt;
    item.seq = bin.seq;
    item.uploaded = (bin.uploaded != 0);
    item.confirmed = (bin.confirmed != 0);
    memcpy(item.data, bin.data, bin.size);
}

static void itemToBin(const DataItem& item, BufferItemBin& bin) {
    memset(&bin, 0, sizeof(bin));
    bin.bikeId = bikeIdToInt(item.bikeId);
    bin.timestamp = item.timestamp;
    bin.size = item.size;
    bin.crc32 = item.crc32;
    bin.session_id = item.session_id;
    bin.session_salt = item.session_salt;
    bin.seq = item.seq;
    bin.uploaded = item.uploaded ? 1 : 0;
    bin.confirmed = item.confirmed ? 1 : 0;
    memcpy(bin.data, item.data, item.size);
}

static bool sameIdentity(const DataItem& a, const DataItem& b) {
    return a.session_id == b.session_id && a.session_salt == b.session_salt &&
           a.seq == b.seq && a.crc32 == b.crc32 && a.bikeId == b.bikeId;
}

BufferManager::BufferManager() : maxCapacity(0), currentUsage(0), initialized(false), lastSync(0) {
    if (!bufferMutex) {
        bufferMutex = xSemaphoreCreateMutex();
    }
}

// R10: aquisição com resultado obrigatório — quem não consegue o lock NÃO
// executa a região protegida (chamadores verificam o retorno)
bool BufferManager::tryLock() {
    return bufferMutex && xSemaphoreTake(bufferMutex, pdMS_TO_TICKS(2000)) == pdTRUE;
}

void BufferManager::unlock() {
    if (bufferMutex) {
        xSemaphoreGive(bufferMutex);
    }
}

bool BufferManager::beginWithAvailableHeap() {
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t systemReserve = 60000;  // BLE + sistema (streaming não usa heap por sessão)

    if (freeHeap < systemReserve + 20000) {
        Serial.printf("❌ Heap insuficiente: %d bytes\n", freeHeap);
        return false;
    }

    maxCapacity = MAX_TOTAL_ITEMS;
    currentUsage = 0;
    initialized = true;

    Serial.printf("📊 Buffer: capacidade de %d itens (%d por bike)\n", maxCapacity, MAX_ITEMS_PER_BIKE);
    return true;
}

uint32_t BufferManager::calculateCapacity() {
    return MAX_TOTAL_ITEMS;
}

void BufferManager::cleanup() {
    currentUsage = 0;
    Serial.println("🧹 Buffer usage counter reset");
}

void BufferManager::begin()
{
    // N03: recupera instalações de spool interrompidas por queda de energia —
    // .tmp é a cópia mais nova; .bak é a anterior íntegra se o canônico sumiu
    recoverStagedBuffers();
    // Diretórios já criados no self-check
    cleanupOldBackups();
    // R05: lotes preparados sem confirmação voltam a pendente
    reconcilePendingBatches();
    Serial.printf("📥 DataBuffer initialized: %d total items\n", getTotalDataCount());
}

// N03: varre /buffer por cópias de troca (".bin.tmp"/".bin.bak") órfãs
void BufferManager::recoverStagedBuffers()
{
    if (!LittleFS.exists(BUFFER_DIR)) return;
    File root = LittleFS.open(BUFFER_DIR);
    if (!root) return;

    std::vector<String> staged;
    File file = root.openNextFile();
    while (file) {
        String name = file.name();
        file.close();
        if (name.startsWith("bpr-") &&
            (name.endsWith(".bin.tmp") || name.endsWith(".bin.bak"))) {
            staged.push_back(name);
        }
        file = root.openNextFile();
    }
    root.close();

    for (const String& name : staged) {
        String canonical = name.substring(0, name.length() - 4); // remove .tmp/.bak
        String fullStaged = String(BUFFER_DIR) + "/" + name;
        String fullCanonical = String(BUFFER_DIR) + "/" + canonical;

        if (!LittleFS.exists(fullCanonical)) {
            if (LittleFS.rename(fullStaged, fullCanonical)) {
                Serial.printf("🔁 Buffer recovered from staging: %s\n", canonical.c_str());
            } else {
                Serial.printf("⚠️ Could not recover staging: %s\n", name.c_str());
            }
        } else {
            // canônico presente: a cópia de troca é obsoleta — o estado
            // comprometido é o canônico + reconciliação de lotes (R05)
            LittleFS.remove(fullStaged);
            Serial.printf("🧹 Obsolete staging removed: %s\n", name.c_str());
        }
    }
}

// F15: caminho canônico e determinístico por bike
String BufferManager::getBikeBufferPath(const String& bikeId) {
    return String(BUFFER_DIR) + "/" + bikeId + ".bin";
}

String BufferManager::getManifestPath(const String& batchId) {
    return String(BUFFER_DIR) + "/batch-" + batchId + ".manifest";
}

std::vector<String> BufferManager::listBikeIds() {
    std::vector<String> ids;
    if (!LittleFS.exists(BUFFER_DIR)) {
        return ids;
    }
    File root = LittleFS.open(BUFFER_DIR);
    if (!root) return ids;

    File file = root.openNextFile();
    while (file) {
        String fileName = file.name();
        if (fileName.endsWith(".bin") && fileName.startsWith("bpr-")) {
            ids.push_back(fileName.substring(0, fileName.length() - 4));
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
    return ids;
}

std::vector<String> BufferManager::listManifestPaths() {
    std::vector<String> paths;
    if (!LittleFS.exists(BUFFER_DIR)) {
        return paths;
    }
    File root = LittleFS.open(BUFFER_DIR);
    if (!root) return paths;

    File file = root.openNextFile();
    while (file) {
        String fileName = file.name();
        if (fileName.endsWith(".manifest") && fileName.startsWith("batch-")) {
            paths.push_back(String(BUFFER_DIR) + "/" + fileName);
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
    return paths;
}

bool BufferManager::addBikeData(const String& bikeId, const String& jsonData)
{
    if (!tryLock()) return false;
    DynamicJsonDocument doc(BIKE_DATA_BUFFER);
    DeserializationError error = deserializeJson(doc, jsonData);

    if (error)
    {
        Serial.printf("❌ JSON parse error in addBikeData: %s\n", error.c_str());
        unlock();
        return false;
    }

    // R03: o CONTEÚDO da bike não é mutado — o horário de recepção fica como
    // metadado do item (timestamp), fora do CRC. Reenvio idêntico produz o
    // mesmo CRC e cai no dedupe idempotente.
    // F04: extrai identificação de sessão/fragmento para dedupe
    uint32_t sessionId = doc["session_id"] | 0;
    uint16_t salt = doc["session_salt"] | 0;
    uint16_t seq = doc["seq"] | 0;

    bool ok = addData(bikeId, (const uint8_t*)jsonData.c_str(), jsonData.length(), sessionId, salt, seq);
    unlock();
    return ok;
}

// Sem lock interno: chamadores seguram o lock
bool BufferManager::addData(const String& bikeId, const uint8_t* data, size_t length,
                            uint32_t sessionId, uint16_t sessionSalt, uint16_t seq)
{
    // F04: limite coerente com GATT (512) e com o item persistido
    if (length == 0 || length > MAX_ITEM_DATA_SIZE) {
        Serial.printf("❌ Data too large: %d bytes (max %d)\n", length, MAX_ITEM_DATA_SIZE);
        return false;
    }

    // F15/F23/O03: bikeId vira nome de arquivo — só formato canônico
    if (!isValidBikeIdFormat(bikeId)) {
        Serial.printf("❌ Invalid bike id for storage: %s\n", bikeId.c_str());
        return false;
    }

    CRC32 crc;
    crc.update(data, length);
    uint32_t checksum = crc.finalize();

    String path = getBikeBufferPath(bikeId);
    File file = LittleFS.open(path, "r+");
    if (!file) {
        // Primeiro item da bike: cria arquivo com header zerado
        file = LittleFS.open(path, "w");
        if (!file) {
            Serial.printf("❌ Cannot create buffer file: %s\n", path.c_str());
            return false;
        }
        BufferFileHeader header;
        header.magic = BUFFER_MAGIC;
        header.version = BUFFER_VERSION;
        header.item_count = 0;
        header.last_update = time(nullptr);
        bool ok = file.write((uint8_t*)&header, sizeof(header)) == sizeof(header);
        file.close();
        if (!ok) {
            LittleFS.remove(path);
            return false;
        }
        file = LittleFS.open(path, "r+");
        if (!file) return false;
    }

    BufferFileHeader header;
    if (file.readBytes((char*)&header, sizeof(header)) != sizeof(header) ||
        header.magic != BUFFER_MAGIC || header.version != BUFFER_VERSION ||
        header.item_count > MAX_ITEMS_PER_BIKE) {
        Serial.printf("❌ Buffer header invalid: %s - quarantining for recovery\n", path.c_str());
        file.close();
        // X05/C9: quarentena preserva o arquivo suspeito para análise
        // (recuperação via USB) e recria um spool limpo — antes a bike
        // recebia "busy" para sempre neste arquivo, sem caminho de reparo
        String corruptPath = path + ".corrupt";
        LittleFS.remove(corruptPath);
        if (!LittleFS.rename(path, corruptPath)) {
            Serial.printf("❌ Cannot quarantine %s - append aborted\n", path.c_str());
            return false;
        }
        file = LittleFS.open(path, "w");
        if (!file) {
            return false;
        }
        BufferFileHeader fresh;
        fresh.magic = BUFFER_MAGIC;
        fresh.version = BUFFER_VERSION;
        fresh.item_count = 0;
        fresh.last_update = time(nullptr);
        bool ok = file.write((uint8_t*)&fresh, sizeof(fresh)) == sizeof(fresh);
        file.close();
        if (!ok) {
            LittleFS.remove(path);
            return false;
        }
        file = LittleFS.open(path, "r+");
        if (!file || file.readBytes((char*)&header, sizeof(header)) != sizeof(header)) {
            file.close();
            return false;
        }
        header = fresh; // spool recém-criado: zero itens
    }

    // F05/R03: dedupe idempotente sobre (bike, sessão, salt, seq, CRC do
    // conteúdo original) — o horário de recepção NÃO entra na identidade
    DataItem candidate;
    candidate.bikeId = bikeId;
    candidate.crc32 = checksum;
    candidate.session_id = sessionId;
    candidate.session_salt = sessionSalt;
    candidate.seq = seq;
    bool duplicate = false;
    for (uint32_t i = 0; i < header.item_count && !duplicate; i++) {
        BufferItemBin bin;
        if (file.readBytes((char*)&bin, sizeof(bin)) != sizeof(bin)) {
            Serial.printf("❌ Truncated item in %s at %d\n", path.c_str(), i);
            file.close();
            return false;
        }
        if (bin.session_id == sessionId && bin.session_salt == sessionSalt &&
            bin.seq == seq && bin.crc32 == checksum) {
            duplicate = true;
        }
    }

    if (duplicate) {
        file.close();
        Serial.printf("♻️ Duplicate fragment %s sessão %lu seq %d - already stored\n",
                      bikeId.c_str(), (unsigned long)sessionId, seq);
        return true;
    }

    if (header.item_count >= MAX_ITEMS_PER_BIKE) {
        // R02: capacidade por bike dimensionada para a sessão máxima legítima;
        // exceder indica violação de contrato, não uso normal
        Serial.printf("❌ Buffer full for bike %s (%d items)\n", bikeId.c_str(), header.item_count);
        file.close();
        return false;
    }

    if (getTotalDataCountUnlocked() >= MAX_TOTAL_ITEMS) {
        Serial.println("❌ Total buffer capacity reached");
        file.close();
        return false;
    }

    // Anexa o registro no fim lógico do arquivo; o header (item_count) só é
    // atualizado DEPOIS do registro gravado — crash no meio deixa o arquivo
    // consistente no estado anterior (registro órfão é sobrescrito depois)
    DataItem item = candidate;
    item.timestamp = time(nullptr);
    item.size = length;
    item.uploaded = false;
    item.confirmed = false;
    memcpy(item.data, data, length);

    BufferItemBin bin;
    itemToBin(item, bin);

    size_t offset = sizeof(BufferFileHeader) + (size_t)header.item_count * sizeof(BufferItemBin);
    if (!file.seek(offset)) {
        file.close();
        return false;
    }
    if (file.write((uint8_t*)&bin, sizeof(bin)) != sizeof(bin)) {
        Serial.printf("❌ Failed to append item for %s (header untouched)\n", bikeId.c_str());
        file.close();
        return false;
    }

    header.item_count++;
    header.last_update = time(nullptr);
    if (!file.seek(0) ||
        file.write((uint8_t*)&header, sizeof(header)) != sizeof(header)) {
        // Registro gravado mas header não: item fica invisível (não confirmado
        // para a bike) — estado seguro, próximo append sobrescreve o órfão
        Serial.printf("❌ Failed to update header for %s - item not acked\n", bikeId.c_str());
        file.close();
        return false;
    }
    file.close();

    Serial.printf("📦 Data added: %s [%d bytes, CRC:%08X, sessão %lu seq %d]\n",
                  bikeId.c_str(), length, checksum, (unsigned long)sessionId, seq);
    return true;
}

// F04/F05/R12: sessão completa = todos os fragmentos 0..total-1 persistidos
bool BufferManager::hasCompleteSession(const String& bikeId, uint32_t sessionId, uint16_t salt, uint16_t totalFrags)
{
    if (totalFrags == 0) return false;
    // R12: total maior que a janela por bike não pode ficar completo aqui
    if (totalFrags > MAX_SESSION_FRAGMENTS) return false;
    if (!tryLock()) return false;
    bool complete = false;
    bool seen[MAX_SESSION_FRAGMENTS] = {false};
    int found = 0;

    forEachItem(getBikeBufferPath(bikeId), [&](const DataItem& item, uint32_t) {
        // R12: seq limitado pelo array E pelo total informado
        if (item.session_id == sessionId && item.session_salt == salt &&
            item.seq < totalFrags && item.seq < MAX_SESSION_FRAGMENTS && !seen[item.seq]) {
            seen[item.seq] = true;
            found++;
        }
        return true;
    });

    complete = (found == (int)totalFrags);
    unlock();
    return complete;
}

// Streaming de registros: valida header e cada item (tamanho + CRC, F17)
// N08: corrupção/truncamento/CRC inválido propagam FALSO — o arquivo NÃO é
// "válido até o erro"; quem substitui/remova o arquivo com base neste retorno
// precisa saber que há registros ilegíveis (e possivelmente válidos depois
// deles). No modo reparo (corruptRecords != nullptr) o registro corrompido é
// PULADO, a travessia continua até o fim e o total ilegível é reportado.
bool BufferManager::forEachItem(const String& path, const std::function<bool(const DataItem&, uint32_t)>& visitor,
                                uint32_t* corruptRecords)
{
    if (corruptRecords) *corruptRecords = 0;
    if (!LittleFS.exists(path)) {
        return true; // sem arquivo = buffer vazio válido
    }

    File file = LittleFS.open(path, "r");
    if (!file) return false;

    BufferFileHeader header;
    if (file.readBytes((char*)&header, sizeof(header)) != sizeof(header) ||
        header.magic != BUFFER_MAGIC || header.version != BUFFER_VERSION ||
        header.item_count > MAX_ITEMS_PER_BIKE) {
        file.close();
        Serial.printf("❌ Buffer header invalid: %s\n", path.c_str());
        return false;
    }

    String bikeId = path.substring(String(BUFFER_DIR).length() + 1);
    bikeId = bikeId.substring(0, bikeId.length() - 4);

    bool keepGoing = true;
    for (uint32_t i = 0; i < header.item_count && keepGoing; i++) {
        BufferItemBin bin;
        bool recordValid = true;
        if (file.readBytes((char*)&bin, sizeof(bin)) != sizeof(bin)) {
            // Truncamento: o arquivo ACABA aqui — não há registros seguintes
            Serial.printf("⚠️ Truncated item in %s at %d - file INVALID from here\n",
                          path.c_str(), i);
            file.close();
            return false; // N08: truncamento nunca é "arquivo válido"
        }
        if (bin.size > MAX_ITEM_DATA_SIZE) {
            Serial.printf("⚠️ Item %d in %s has invalid size %d\n", i, path.c_str(), bin.size);
            recordValid = false;
        }
        // F17: CRC verificado na leitura
        if (recordValid) {
            CRC32 crc;
            crc.update(bin.data, bin.size);
            if (crc.finalize() != bin.crc32) {
                Serial.printf("⚠️ CRC mismatch in %s item %d\n", path.c_str(), i);
                recordValid = false;
            }
        }

        if (!recordValid) {
            if (corruptRecords) {
                // N08 modo reparo: pula o registro corrompido e CONTINUA —
                // registro de tamanho fixo mantém os seguintes endereçáveis
                (*corruptRecords)++;
                continue;
            }
            file.close();
            return false; // N08
        }

        DataItem item;
        binToItem(bin, item, bikeId);
        keepGoing = visitor(item, i);
    }

    file.close();
    return true;
}

// Atualiza in-place o byte `uploaded` de um registro de tamanho fixo
bool BufferManager::setItemUploadedFlag(const String& path, uint32_t index, bool uploaded)
{
    File file = LittleFS.open(path, "r+");
    if (!file) return false;

    size_t offset = sizeof(BufferFileHeader) + (size_t)index * sizeof(BufferItemBin) +
                    offsetof(BufferItemBin, uploaded);
    uint8_t flag = uploaded ? 1 : 0;
    bool ok = file.seek(offset) && file.write(&flag, 1) == 1;
    file.close();
    return ok;
}

// F18/F19/R05/R06: prepara um lote medido com identidade estável
int BufferManager::prepareUploadBatch(DynamicJsonDocument& doc, int maxItems, String& batchId)
{
    if (!tryLock()) return 0;
    if (maxItems <= 0 || maxItems > UPLOAD_BATCH_MAX_ITEMS) {
        maxItems = UPLOAD_BATCH_MAX_ITEMS;
    }

    struct Candidate {
        String bikeId;
        uint32_t index;
        size_t itemBytes; // estimativa = metadados do envelope + bytes do item
    };
    std::vector<Candidate> candidates;

    // Passo 1: coleta só metadados dos pendentes (sem decodificar payload —
    // O01/R02: nada de materializar todos os itens em RAM)
    std::vector<String> ids = listBikeIds();
    for (const String& bikeId : ids) {
        forEachItem(getBikeBufferPath(bikeId), [&](const DataItem& item, uint32_t index) {
            if (!item.uploaded) {
                Candidate c;
                c.bikeId = bikeId;
                c.index = index;
                c.itemBytes = 120 + item.size + 16; // envelope + conteúdo + folga
                candidates.push_back(c);
            }
            return true;
        });
    }

    if (candidates.empty()) {
        unlock();
        return 0;
    }

    // Passo 2: seleciona itens que cabem no orçamento do lote
    const size_t byteBudget = BUFFER_PERSISTENCE_BUFFER - 600;
    size_t used = 200; // envelope
    std::vector<int> selected;
    for (int i = 0; i < (int)candidates.size() && (int)selected.size() < maxItems; i++) {
        if (candidates[i].itemBytes > byteBudget) continue;
        if (used + candidates[i].itemBytes > byteBudget && !selected.empty()) break;
        used += candidates[i].itemBytes;
        selected.push_back(i);
    }

    if (selected.empty()) {
        unlock();
        return 0;
    }

    // Passo 3: monta o documento do lote (decodifica SÓ os selecionados)
    BPRJsonHelper::addTimestamp(doc);
    doc["base_id"] = configCredentials.getBaseId();

    JsonArray dataArray = doc.createNestedArray("data");
    // Seção 6 da revalidação: item e índice viajam PARES daqui em diante —
    // ordenar só os itens dissociava o par e marcava flags no registro de
    // OUTRA bike/posição quando a enumeração não coincidia com a seleção
    struct ManifestEntry {
        DataItem item;
        uint32_t index; // índice do registro no arquivo da bike
    };
    std::vector<ManifestEntry> manifest;

    for (int sel : selected) {
        const Candidate& c = candidates[sel];
        bool included = false;
        forEachItem(getBikeBufferPath(c.bikeId), [&](const DataItem& item, uint32_t index) {
            if (index != c.index || item.uploaded || included) return true;
            included = true;

            JsonObject itemObj = dataArray.createNestedObject();
            itemObj["bike_id"] = item.bikeId;
            itemObj["ts"] = item.timestamp;
            itemObj["size"] = item.size;
            itemObj["crc32"] = String(item.crc32, HEX);
            itemObj["session_id"] = item.session_id;
            itemObj["session_salt"] = item.session_salt;
            itemObj["seq"] = item.seq;

            String decoded;
            decoded.reserve(item.size);
            for (size_t j = 0; j < item.size; j++) {
                decoded += (char)item.data[j];
            }
            StaticJsonDocument<BIKE_DATA_BUFFER> inner;
            if (deserializeJson(inner, decoded) == DeserializationError::Ok) {
                itemObj["data_decoded"] = inner;
            } else {
                itemObj["data"] = decoded;
            }

            manifest.push_back({item, index});
            return true;
        });
    }

    int included = dataArray.size();
    if (included == 0 || doc.overflowed()) {
        Serial.println("❌ Batch build failed (empty or overflow)");
        unlock();
        return 0;
    }

    doc["data_count"] = included;

    // R06: chave estável derivada da IDENTIDADE dos itens (ordenada), sem
    // horário — retry do mesmo conjunto produz a mesma chave.
    // O índice ACOMPANHA o item na ordenação (par item/índice preservado)
    std::sort(manifest.begin(), manifest.end(), [](const ManifestEntry& a, const ManifestEntry& b) {
        if (a.item.bikeId != b.item.bikeId) return a.item.bikeId < b.item.bikeId;
        if (a.item.session_id != b.item.session_id) return a.item.session_id < b.item.session_id;
        if (a.item.session_salt != b.item.session_salt) return a.item.session_salt < b.item.session_salt;
        return a.item.seq < b.item.seq;
    });
    CRC32 keyCrc;
    keyCrc.update((const uint8_t*)configCredentials.getBaseId().c_str(),
                  configCredentials.getBaseId().length());
    for (const ManifestEntry& entry : manifest) {
        String line = entry.item.bikeId + "|" + String(entry.item.session_id) + "|" +
                      String(entry.item.session_salt) + "|" + String(entry.item.seq) + "\n";
        keyCrc.update((const uint8_t*)line.c_str(), line.length());
    }
    char keyBuf[16];
    snprintf(keyBuf, sizeof(keyBuf), "%08lx", (unsigned long)keyCrc.finalize());
    batchId = String(keyBuf);

    // R05: manifesto durável ANTES de marcar qualquer item como enviado
    std::vector<DataItem> manifestItems;
    manifestItems.reserve(manifest.size());
    for (const ManifestEntry& entry : manifest) {
        manifestItems.push_back(entry.item);
    }
    if (!writeManifest(batchId, manifestItems)) {
        Serial.println("❌ Batch manifest write failed - batch not prepared");
        unlock();
        return 0;
    }

    // Passo 4: marca EXATAMENTE os itens incluídos (in-place) e persiste
    // N07: falha de flag é PROPAGADA — lote volta a pendente com o manifesto
    // removido, em vez de ficar "meio preparado" (itens marcados sem recibo)
    bool flagsOk = true;
    for (const ManifestEntry& entry : manifest) {
        if (!setItemUploadedFlag(getBikeBufferPath(entry.item.bikeId), entry.index, true)) {
            Serial.printf("❌ Failed to set uploaded flag for %s idx %lu\n",
                          entry.item.bikeId.c_str(), (unsigned long)entry.index);
            flagsOk = false;
        }
    }
    unlock();
    if (!flagsOk) {
        // N07: rollback FORA do lock — rollbackUpload adquire o próprio
        // mutex (chamá-lo segurando o lock faria o tryLock interno falhar
        // em no-op e o lote ficaria "meio preparado")
        rollbackUpload(batchId);
        return 0;
    }

    return included;
}

// R05: manifesto durável com a identidade exata do lote
// N07: existir ≠ estar íntegro — overflow da árvore, escrita curta, crash no
// meio da gravação e releitura inválida impedem o manifesto de ser tratado
// como durável ANTES de qualquer flag mudar. Gravação via tmp+rename.
bool BufferManager::writeManifest(const String& batchId, const std::vector<DataItem>& items)
{
    DynamicJsonDocument doc(2048);
    doc["batch_id"] = batchId;
    doc["state"] = "prepared";

    JsonArray arr = doc.createNestedArray("items");
    for (const DataItem& item : items) {
        JsonObject o = arr.createNestedObject();
        o["bike_id"] = item.bikeId;
        o["session_id"] = item.session_id;
        o["session_salt"] = item.session_salt;
        o["seq"] = item.seq;
        o["crc32"] = String(item.crc32, HEX);
    }

    if (doc.overflowed()) {
        Serial.println("❌ Manifest JSON overflow - batch not prepared");
        return false;
    }

    String out;
    serializeJson(doc, out);

    // N07: tmp + rename — crash no meio da gravação não deixa manifesto parcial
    String path = getManifestPath(batchId);
    String tmpPath = path + ".tmp";
    File file = LittleFS.open(tmpPath, "w");
    if (!file) return false;
    size_t written = file.write((const uint8_t*)out.c_str(), out.length());
    file.close();
    if (written != out.length()) {
        Serial.printf("❌ Manifest short write (%d/%d) - discarded\n",
                      (int)written, (int)out.length());
        LittleFS.remove(tmpPath);
        return false;
    }
    LittleFS.remove(path);
    if (!LittleFS.rename(tmpPath, path)) {
        LittleFS.remove(tmpPath);
        return false;
    }

    // N07: releitura valida o manifesto gravado — bytes na flash ≠ JSON legível
    std::vector<DataItem> check;
    if (!readManifest(batchId, check) || check.size() != items.size()) {
        Serial.println("❌ Manifest verification failed (re-read) - removed");
        removeManifest(batchId);
        return false;
    }
    return true;
}

bool BufferManager::readManifest(const String& batchId, std::vector<DataItem>& items)
{
    String path = getManifestPath(batchId);
    if (!LittleFS.exists(path)) return false;

    File file = LittleFS.open(path, "r");
    if (!file) return false;

    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) return false;

    for (JsonObject o : doc["items"].as<JsonArray>()) {
        DataItem item;
        item.bikeId = o["bike_id"].as<String>();
        item.session_id = o["session_id"] | 0;
        item.session_salt = o["session_salt"] | 0;
        item.seq = o["seq"] | 0;
        item.crc32 = (uint32_t)strtoul(o["crc32"].as<String>().c_str(), NULL, 16);
        items.push_back(item);
    }
    return true;
}

void BufferManager::removeManifest(const String& batchId)
{
    LittleFS.remove(getManifestPath(batchId));
}

// R05: confirmação remove SOMENTE os itens do lote; pendências ficam intactas
bool BufferManager::markAsConfirmed(const String& batchId)
{
    std::vector<DataItem> items;
    if (!readManifest(batchId, items)) {
        Serial.printf("⚠️ Confirm without manifest %s - ignoring\n", batchId.c_str());
        return false;
    }

    if (!tryLock()) return false;
    createBackup();

    // Agrupa por bike e reescreve cada arquivo excluindo os itens do lote
    std::sort(items.begin(), items.end(), [](const DataItem& a, const DataItem& b) {
        return a.bikeId < b.bikeId;
    });
    size_t i = 0;
    bool ok = true;
    while (i < items.size()) {
        String bikeId = items[i].bikeId;
        std::vector<DataItem> bikeItems;
        while (i < items.size() && items[i].bikeId == bikeId) {
            bikeItems.push_back(items[i]);
            i++;
        }
        if (!rewriteBikeBufferExcluding(bikeId, bikeItems)) {
            Serial.printf("❌ Failed to rewrite buffer for %s on confirm\n", bikeId.c_str());
            ok = false;
        }
    }

    if (ok) {
        removeManifest(batchId);
        lastSync = millis();
        Serial.printf("✅ Batch %s confirmed and cleared (%d items)\n", batchId.c_str(), (int)items.size());
    }
    unlock();
    return ok;
}

// R04/N08: reescrita recuperável — temporário + rename; falha preserva o
// original. Registros corrompidos no MEIO do arquivo não descartam os válidos
// que vêm depois: o modo reparo pula os ilegíveis, mantém os recuperáveis e o
// original é QUARENTENADO (.corrupt) em vez de descartado silenciosamente.
bool BufferManager::rewriteBikeBufferExcluding(const String& bikeId, const std::vector<DataItem>& manifestItems)
{
    String path = getBikeBufferPath(bikeId);
    String tmpPath = path + ".tmp";

    if (!LittleFS.exists(path)) {
        return true; // nada a reescrever (itens já removidos)
    }

    File out = LittleFS.open(tmpPath, "w");
    if (!out) return false;

    BufferFileHeader header;
    header.magic = BUFFER_MAGIC;
    header.version = BUFFER_VERSION;
    header.item_count = 0;
    header.last_update = time(nullptr);
    if (out.write((uint8_t*)&header, sizeof(header)) != sizeof(header)) {
        out.close();
        LittleFS.remove(tmpPath);
        return false;
    }

    uint32_t kept = 0;
    uint32_t corrupted = 0;
    bool writeOk = true;
    // N08: modo reparo — registra corrompidos, preserva os válidos DEPOIS deles
    bool valid = forEachItem(path, [&](const DataItem& item, uint32_t) {
        bool inBatch = false;
        for (const DataItem& m : manifestItems) {
            if (sameIdentity(item, m)) {
                inBatch = true;
                break;
            }
        }
        if (inBatch) return true;

        BufferItemBin bin;
        itemToBin(item, bin);
        if (out.write((uint8_t*)&bin, sizeof(bin)) != sizeof(bin)) {
            writeOk = false;
            return false; // aborta a iteração
        }
        kept++;
        return true;
    }, &corrupted);
    out.close();

    if (!valid || !writeOk) {
        // Falha de escrita ou arquivo truncado/inválido — preserva o original
        Serial.printf("❌ Rewrite failed for %s - original preserved\n", bikeId.c_str());
        LittleFS.remove(tmpPath);
        return false;
    }

    // Reescreve o header do temporário com a contagem correta
    File fix = LittleFS.open(tmpPath, "r+");
    bool ok = false;
    if (fix) {
        header.item_count = kept;
        ok = fix.seek(0) && fix.write((uint8_t*)&header, sizeof(header)) == sizeof(header);
        fix.close();
    }
    if (!ok) {
        LittleFS.remove(tmpPath);
        return false;
    }

    if (corrupted > 0) {
        // N08: o original continha registros ilegíveis — ele é quarentenado
        // (nunca apresentado como cópia completa) e o novo spool carrega
        // apenas os registros válidos recuperáveis, com diagnóstico explícito
        Serial.printf("⚠️ %s had %d corrupt records: %d valid kept, original quarantined\n",
                      bikeId.c_str(), (int)corrupted, (int)kept);
        String corruptPath = path + ".corrupt";
        LittleFS.remove(corruptPath);
        if (!LittleFS.rename(path, corruptPath)) {
            // sem como quarentenar, o original fica (estado seguro)
            Serial.printf("❌ Cannot quarantine %s - original preserved\n", bikeId.c_str());
            LittleFS.remove(tmpPath);
            return false;
        }
        if (kept == 0 || !LittleFS.rename(tmpPath, path)) {
            LittleFS.remove(tmpPath);
        }
        return true;
    }

    if (kept == 0) {
        LittleFS.remove(tmpPath);
        LittleFS.remove(path);
        Serial.printf("🗑️ Buffer fully uploaded and removed: %s\n", bikeId.c_str());
        return true;
    }

    // N03: instalação por rename DIRETO (substituição atômica); fallback .bak
    // preserva o original íntegro durante a troca — a sequência antiga
    // remove-then-rename perdia o spool inteiro se faltasse energia entre as duas
    if (!LittleFS.rename(tmpPath, path)) {
        String bakPath = path + ".bak";
        LittleFS.remove(bakPath);
        if (LittleFS.exists(path) && !LittleFS.rename(path, bakPath)) {
            Serial.printf("❌ Backup move failed for %s - original preserved\n", bikeId.c_str());
            LittleFS.remove(tmpPath);
            return false;
        }
        if (!LittleFS.rename(tmpPath, path)) {
            Serial.printf("❌ Rename failed for %s - restoring original\n", bikeId.c_str());
            LittleFS.rename(bakPath, path);
            LittleFS.remove(tmpPath);
            return false;
        }
        LittleFS.remove(bakPath);
    }
    return true;
}

// R05: lote sem entrega comprovada volta a pendente
// N07: falha ao restaurar UMA flag mantém o manifesto — o reconcile do boot
// tenta de novo; remover o recibo sem restaurar o estado deixaria itens
// marcados como enviados sem entrega comprovada
bool BufferManager::rollbackUpload(const String& batchId)
{
    std::vector<DataItem> items;
    if (!readManifest(batchId, items)) {
        return false;
    }

    if (!tryLock()) return false;
    bool restoreOk = true;
    for (const DataItem& item : items) {
        String path = getBikeBufferPath(item.bikeId);
        bool itemRestored = false;
        forEachItem(path, [&](const DataItem& it, uint32_t index) {
            if (sameIdentity(it, item) && it.uploaded) {
                if (!setItemUploadedFlag(path, index, false)) {
                    restoreOk = false;
                } else {
                    itemRestored = true;
                }
            }
            return true;
        });
        // item ausente do spool (já confirmado/removido) não é erro
        (void)itemRestored;
    }
    if (restoreOk) {
        removeManifest(batchId);
    } else {
        Serial.printf("⚠️ Rollback incomplete for %s - manifest kept for retry\n", batchId.c_str());
    }
    unlock();
    Serial.printf("⚠️ Batch %s rolled back to pending\n", batchId.c_str());
    return restoreOk;
}

// R05: boot com lotes preparados sem confirmação — reconciliação conservadora:
// os itens voltam a pendente (a nuvem deduplica pela chave estável do lote)
// N07: manifesto ILEGÍVEL tem plano alternativo — é quarentenado e TODAS as
// flags uploaded voltam a pendente (a nuvem deduplica reenvios); sem isso,
// itens marcados como enviados ficariam fora de entrega para sempre
void BufferManager::reconcilePendingBatches()
{
    std::vector<String> manifests = listManifestPaths();
    for (const String& path : manifests) {
        String fileName = path.substring(path.lastIndexOf('/') + 1);
        if (!fileName.startsWith("batch-") || !fileName.endsWith(".manifest")) continue;
        String batchId = fileName.substring(6, fileName.length() - 9);

        std::vector<DataItem> items;
        if (!readManifest(batchId, items)) {
            Serial.printf("🚨 Manifest %s unreadable - quarantining and resetting all uploaded flags\n",
                          batchId.c_str());
            String corruptPath = path + ".corrupt";
            LittleFS.remove(corruptPath);
            LittleFS.rename(path, corruptPath);

            // Conservador: tudo volta a pendente; nada fica sem caminho de envio
            if (tryLock()) {
                std::vector<String> ids = listBikeIds();
                for (const String& bikeId : ids) {
                    forEachItem(getBikeBufferPath(bikeId), [&](const DataItem& item, uint32_t index) {
                        if (item.uploaded) {
                            setItemUploadedFlag(getBikeBufferPath(bikeId), index, false);
                        }
                        return true;
                    });
                }
                unlock();
            }
            continue;
        }

        Serial.printf("🔁 Unconfirmed batch at boot: %s -> back to pending\n", batchId.c_str());
        rollbackUpload(batchId);
    }
}

// O06: pendências reais por bike
bool BufferManager::getBikePendingInfo(const String& bikeId, uint16_t& sessions, uint32_t& bytes)
{
    sessions = 0;
    bytes = 0;
    if (!tryLock()) return false;
    std::vector<uint32_t> seenSessions;
    std::vector<uint16_t> seenSalts;

    forEachItem(getBikeBufferPath(bikeId), [&](const DataItem& item, uint32_t) {
        if (item.uploaded) return true;
        bytes += item.size;
        bool known = false;
        for (size_t i = 0; i < seenSessions.size(); i++) {
            if (seenSessions[i] == item.session_id && seenSalts[i] == item.session_salt) {
                known = true;
                break;
            }
        }
        if (!known) {
            seenSessions.push_back(item.session_id);
            seenSalts.push_back(item.session_salt);
            if (sessions < 0xFFFF) sessions++;
        }
        return true;
    });

    unlock();
    return true;
}

// O04: backup bruto por cópia de arquivo íntegro (não substitui rename/manifesto)
void BufferManager::createBackup()
{
    std::vector<String> ids = listBikeIds();
    if (ids.empty()) return;

    // O04/X05: SEM espaço para temporários + spool + backup, a cópia é
    // adiada (logada) — encher a flash aqui faria a PRÓXIMA gravação crítica
    // (manifesto/spool) falhar por causa de um backup
    if (!hasEnoughSpace()) {
        Serial.println("⚠️ Backup skipped: low free space (spool writes take priority)");
        return;
    }

    if (!LittleFS.exists("/backup")) {
        LittleFS.mkdir("/backup");
    }

    String stamp = String(millis());
    int copies = 0;
    for (const String& bikeId : ids) {
        String src = getBikeBufferPath(bikeId);
        if (!LittleFS.exists(src)) continue;

        File in = LittleFS.open(src, "r");
        if (!in) continue;
        size_t srcSize = in.size();
        File out = LittleFS.open("/backup/" + stamp + "_" + bikeId + ".bkp", "w");
        if (!out) {
            in.close();
            continue;
        }
        uint8_t buf[256];
        size_t copied = 0;
        while (copied < srcSize) {
            size_t n = in.read(buf, sizeof(buf));
            if (n == 0) break;
            if (out.write(buf, n) != n) break;
            copied += n;
        }
        in.close();
        out.close();
        if (copied == srcSize) {
            copies++;
        } else {
            LittleFS.remove("/backup/" + stamp + "_" + bikeId + ".bkp");
        }
    }

    if (copies > 0) {
        Serial.printf("💾 Backup created: %d bike buffers (stamp %s)\n", copies, stamp.c_str());
        // O04 (seção 6): a limpeza não pode esperar o próximo boot — entre
        // lotes o /backup cresce sem limite. Orçamento de bytes aplicado na
        // criação (BACKUP_MAX_BYTES), além da retenção por contagem.
        cleanupOldBackups();
    }
}

void BufferManager::cleanupOldBackups()
{
    if (!LittleFS.exists("/backup")) return;

    // Mantém os 3 backups mais recentes (ordem por timestamp de nome)
    std::vector<String> stamps;
    File root = LittleFS.open("/backup");
    if (!root) return;
    File file = root.openNextFile();
    while (file) {
        String fileName = file.name();
        file.close();
        if (fileName.endsWith(".bkp")) {
            int under = fileName.indexOf('_');
            if (under > 0) {
                String stamp = fileName.substring(0, under);
                if (std::find(stamps.begin(), stamps.end(), stamp) == stamps.end()) {
                    stamps.push_back(stamp);
                }
            }
        }
        file = root.openNextFile();
    }
    root.close();

    if (stamps.size() <= 3) {
        // O04: mesmo dentro da retenção por contagem, o ORÇAMENTO de bytes
        // precisa ser respeitado (3 cópias integrais do spool cheio não cabem
        // na partição junto com spool + temporário de reescrita)
        enforceBackupBudget(stamps);
        return;
    }
    std::sort(stamps.begin(), stamps.end());

    size_t toRemove = stamps.size() - 3;
    for (size_t i = 0; i < toRemove; i++) {
        String oldStamp = stamps[i];
        File root2 = LittleFS.open("/backup");
        if (!root2) return;
        std::vector<String> doomed;
        File f = root2.openNextFile();
        while (f) {
            String name = f.name();
            f.close();
            if (name.endsWith(".bkp") && name.startsWith(oldStamp + "_")) {
                doomed.push_back("/backup/" + name);
            }
            f = root2.openNextFile();
        }
        root2.close();
        for (const String& p : doomed) {
            LittleFS.remove(p);
        }
        Serial.printf("🗑️ Old backup removed (stamp %s)\n", oldStamp.c_str());
    }

    // Recalcula os stamps sobreviventes e aplica o orçamento de bytes
    std::vector<String> remaining;
    for (size_t i = toRemove; i < stamps.size(); i++) {
        remaining.push_back(stamps[i]);
    }
    enforceBackupBudget(remaining);
}

// O04: mantém os stamps MAIS RECENTES enquanto o total de bytes do /backup
// exceder BACKUP_MAX_BYTES; remove sempre o stamp mais antigo primeiro
void BufferManager::enforceBackupBudget(std::vector<String> stamps)
{
    if (!LittleFS.exists("/backup")) return;
    std::sort(stamps.begin(), stamps.end()); // mais antigo primeiro

    while (stamps.size() > 1) {
        size_t totalBytes = 0;
        File root = LittleFS.open("/backup");
        if (!root) return;
        File f = root.openNextFile();
        while (f) {
            String name = f.name();
            size_t sz = f.size();
            f.close();
            if (name.endsWith(".bkp")) totalBytes += sz;
            f = root.openNextFile();
        }
        root.close();

        if (totalBytes <= BACKUP_MAX_BYTES) break;

        String oldStamp = stamps[0];
        std::vector<String> doomed;
        File root2 = LittleFS.open("/backup");
        if (!root2) return;
        File f2 = root2.openNextFile();
        while (f2) {
            String name = f2.name();
            f2.close();
            if (name.endsWith(".bkp") && name.startsWith(oldStamp + "_")) {
                doomed.push_back("/backup/" + name);
            }
            f2 = root2.openNextFile();
        }
        root2.close();
        for (const String& p : doomed) {
            LittleFS.remove(p);
        }
        stamps.erase(stamps.begin());
        Serial.printf("🗑️ Backup stamp %s removed (byte budget %d exceeded)\n",
                      oldStamp.c_str(), (int)BACKUP_MAX_BYTES);
    }
}

bool BufferManager::isFull()
{
    return getTotalDataCount() >= MAX_TOTAL_ITEMS;
}

bool BufferManager::hasData()
{
    return getTotalDataCount() > 0;
}

int BufferManager::getDataCount()
{
    return getTotalDataCount();
}

int BufferManager::getTotalDataCountUnlocked()
{
    int totalCount = 0;
    std::vector<String> ids = listBikeIds();
    for (const String& bikeId : ids) {
        forEachItem(getBikeBufferPath(bikeId), [&](const DataItem&, uint32_t) {
            totalCount++;
            return true;
        });
    }
    return totalCount;
}

int BufferManager::getTotalDataCount()
{
    if (!tryLock()) return 0;
    int count = getTotalDataCountUnlocked();
    unlock();
    return count;
}

int BufferManager::getPendingCount()
{
    if (!tryLock()) return 0;
    int pending = 0;
    std::vector<String> ids = listBikeIds();
    for (const String& bikeId : ids) {
        forEachItem(getBikeBufferPath(bikeId), [&](const DataItem& item, uint32_t) {
            if (!item.uploaded) {
                pending++;
            }
            return true;
        });
    }
    unlock();
    return pending;
}

void BufferManager::printStorageInfo()
{
    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes = LittleFS.usedBytes();
    size_t freeBytes = totalBytes - usedBytes;

    Serial.printf("💾 LittleFS Storage:\n");
    Serial.printf("   Total: %d KB\n", totalBytes / 1024);
    Serial.printf("   Used:  %d KB (%.1f%%)\n", usedBytes / 1024, (float)usedBytes / totalBytes * 100);
    Serial.printf("   Free:  %d KB\n", freeBytes / 1024);

    Serial.printf("📄 Main Files:\n");
    printFileSize(BIKE_REGISTRY_FILE);
    printFileSize(BIKE_STATUS_FILE);
    printFileSize(CONFIG_FILE);

    int bufferCount = 0;
    size_t bufferSize = 0;
    if (LittleFS.exists(BUFFER_DIR)) {
        File root = LittleFS.open(BUFFER_DIR);
        File file = root.openNextFile();
        while (file) {
            String fileName = file.name();
            if (fileName.endsWith(".bin")) {
                bufferCount++;
                bufferSize += file.size();
            }
            file.close();
            file = root.openNextFile();
        }
    }

    Serial.printf("📦 Buffers: %d files, %d KB, %d total items\n",
                  bufferCount, bufferSize / 1024, getTotalDataCount());

    if (freeBytes < 10240)
    {
        Serial.printf("⚠️ LOW STORAGE WARNING: Only %d KB free!\n", freeBytes / 1024);
    }
}

void BufferManager::printFileSize(const String& filePath)
{
    if (LittleFS.exists(filePath))
    {
        File file = LittleFS.open(filePath, "r");
        if (file)
        {
            Serial.printf("   %s: %d bytes\n", filePath.c_str(), file.size());
            file.close();
        }
    }
    else
    {
        Serial.printf("   %s: not found\n", filePath.c_str());
    }
}

bool BufferManager::hasEnoughSpace()
{
    size_t freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
    return freeBytes > MINIMAL_FREE_FS_SPACE;
}
