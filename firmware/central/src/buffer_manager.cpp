#include <ArduinoJson.h>
#include <CRC32.h>
#include <LittleFS.h>
#include <freertos/semphr.h>
#include "binary_structs.h"
#include "bpr_json_helper.h"
#include "buffer_manager.h"
#include "config_manager.h"
#include "config_credentials.h"
#include "constants.h"

extern ConfigManager configManager;
extern ConfigCredentials configCredentials;

static SemaphoreHandle_t bufferMutex = nullptr;

BufferManager::BufferManager() : maxCapacity(0), currentUsage(0), initialized(false), lastSync(0) {
    bufferMutex = xSemaphoreCreateMutex();
}

void BufferManager::lock() {
    if (bufferMutex && xSemaphoreTake(bufferMutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
        Serial.println("⚠️ BufferManager lock timeout");
    }
}

void BufferManager::unlock() {
    if (bufferMutex) {
        xSemaphoreGive(bufferMutex);
    }
}

bool BufferManager::beginWithAvailableHeap() {
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t systemReserve = 60000;  // BLE + sistema + BikeBuffer operado em heap

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
    // Diretórios já criados no self-check
    cleanupOldBackups();
    Serial.printf("📥 DataBuffer initialized: %d total items\n", getTotalDataCount());
}

// F15: caminho canônico e determinístico por bike
String BufferManager::getBikeBufferPath(const String& bikeId) {
    return String(BUFFER_DIR) + "/" + bikeId + ".bin";
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

bool BufferManager::addConfigData(const String& configType, const String& jsonData)
{
    lock();
    DynamicJsonDocument doc(BIKE_DATA_BUFFER);
    BPRJsonHelper::addConfigFields(doc, configType, jsonData);

    String modifiedJson;
    serializeJson(doc, modifiedJson);

    bool ok = addData("central", (uint8_t *)modifiedJson.c_str(), modifiedJson.length());
    unlock();
    return ok;
}

bool BufferManager::addBikeData(const String& bikeId, const String& jsonData)
{
    lock();
    DynamicJsonDocument doc(BIKE_DATA_BUFFER);
    DeserializationError error = deserializeJson(doc, jsonData);

    if (error)
    {
        Serial.printf("❌ JSON parse error in addBikeData: %s\n", error.c_str());
        unlock();
        return false;
    }

    // F20: só carimba horário se o relógio for válido (helper verifica)
    BPRJsonHelper::addTimestamp(doc, "central_receive");

    String modifiedJson;
    serializeJson(doc, modifiedJson);

    // F04: extrai identificação de sessão/fragmento para dedupe
    uint32_t sessionId = doc["session_id"] | 0;
    uint16_t salt = doc["session_salt"] | 0;
    uint16_t seq = doc["seq"] | 0;

    bool ok = addData(bikeId, (uint8_t *)modifiedJson.c_str(), modifiedJson.length(), sessionId, salt, seq);
    unlock();
    return ok;
}

// Sem lock interno: chamadores (addBikeData/addConfigData) seguram o lock
bool BufferManager::addData(const String& bikeId, const uint8_t* data, size_t length,
                            uint32_t sessionId, uint16_t sessionSalt, uint16_t seq)
{
    // F04: limite coerente com GATT (512) e com o item persistido
    if (length == 0 || length > MAX_ITEM_DATA_SIZE) {
        Serial.printf("❌ Data too large: %d bytes (max %d)\n", length, MAX_ITEM_DATA_SIZE);
        return false;
    }

    // F15/F23: bikeId vira nome de arquivo — só aceita formato canônico ou "central"
    if (bikeId != "central" && !isValidBikeIdFormat(bikeId)) {
        Serial.printf("❌ Invalid bike id for storage: %s\n", bikeId.c_str());
        return false;
    }

    BikeBuffer* bikeBuffer = new BikeBuffer;
    if (!bikeBuffer) return false;
    bool loaded = loadBikeBuffer(bikeId, *bikeBuffer);

    CRC32 crc;
    crc.update(data, length);
    uint32_t checksum = crc.finalize();

    // F05: dedupe idempotente — reenvio de fragmento já persistido não duplica
    if (loaded) {
        for (int i = 0; i < bikeBuffer->dataCount; i++) {
            if (bikeBuffer->buffer[i].session_id == sessionId &&
                bikeBuffer->buffer[i].session_salt == sessionSalt &&
                bikeBuffer->buffer[i].seq == seq &&
                bikeBuffer->buffer[i].crc32 == checksum) {
                Serial.printf("♻️ Duplicate fragment %s sessão %lu seq %d - already stored\n",
                              bikeId.c_str(), (unsigned long)sessionId, seq);
                delete bikeBuffer;
                return true;
            }
        }
    }

    if (!loaded || bikeBuffer->dataCount >= MAX_ITEMS_PER_BIKE) {
        Serial.printf("❌ Buffer full for bike %s\n", bikeId.c_str());
        delete bikeBuffer;
        return false;
    }

    if (getTotalDataCountUnlocked() >= MAX_TOTAL_ITEMS) {
        Serial.println("❌ Total buffer capacity reached");
        delete bikeBuffer;
        return false;
    }

    int index = bikeBuffer->dataCount;
    bikeBuffer->buffer[index].bikeId = bikeId;
    bikeBuffer->buffer[index].timestamp = time(nullptr);
    bikeBuffer->buffer[index].size = length;
    bikeBuffer->buffer[index].crc32 = checksum;
    bikeBuffer->buffer[index].session_id = sessionId;
    bikeBuffer->buffer[index].session_salt = sessionSalt;
    bikeBuffer->buffer[index].seq = seq;
    bikeBuffer->buffer[index].uploaded = false;
    bikeBuffer->buffer[index].confirmed = false;
    memcpy(bikeBuffer->buffer[index].data, data, length);
    bikeBuffer->dataCount++;

    Serial.printf("📦 Data added: %s [%d bytes, CRC:%08X, sessão %lu seq %d]\n",
                  bikeId.c_str(), length, checksum, (unsigned long)sessionId, seq);

    // F16: só retorna sucesso depois de persistir de fato
    bool saved = saveBikeBuffer(bikeId, *bikeBuffer);
    if (!saved) {
        Serial.printf("❌ Failed to persist buffer for %s\n", bikeId.c_str());
    }
    delete bikeBuffer;
    return saved;
}

// F04/F05: sessão completa = todos os fragmentos 0..total-1 persistidos
bool BufferManager::hasCompleteSession(const String& bikeId, uint32_t sessionId, uint16_t salt, uint16_t totalFrags)
{
    if (totalFrags == 0) return false;
    lock();
    BikeBuffer* bikeBuffer = new BikeBuffer;
    bool complete = false;
    if (loadBikeBuffer(bikeId, *bikeBuffer)) {
        bool seen[MAX_ITEMS_PER_BIKE] = {false};
        int found = 0;
        for (int i = 0; i < bikeBuffer->dataCount; i++) {
            const DataItem& item = bikeBuffer->buffer[i];
            if (item.session_id == sessionId && item.session_salt == salt &&
                item.seq < totalFrags && !seen[item.seq]) {
                seen[item.seq] = true;
                found++;
            }
        }
        complete = (found == totalFrags);
    }
    delete bikeBuffer;
    unlock();
    return complete;
}

// F18/F19: monta UM lote por chamada com bytes medidos; marca como enviado
int BufferManager::getDataForUpload(DynamicJsonDocument& doc, int maxItems)
{
    lock();
    if (maxItems <= 0 || maxItems > UPLOAD_BATCH_MAX_ITEMS) {
        maxItems = UPLOAD_BATCH_MAX_ITEMS;
    }

    // Candidatos: itens não enviados
    std::vector<String> ids = listBikeIds();
    BikeBuffer* bikeBuffer = new BikeBuffer;

    struct Candidate {
        String bikeId;
        int index;
        String decoded;
        size_t itemBytes;
    };
    std::vector<Candidate> candidates;

    for (const String& bikeId : ids) {
        if (!loadBikeBuffer(bikeId, *bikeBuffer)) continue;
        for (int i = 0; i < bikeBuffer->dataCount; i++) {
            if (!bikeBuffer->buffer[i].uploaded) {
                Candidate c;
                c.bikeId = bikeId;
                c.index = i;
                String decoded;
                decoded.reserve(bikeBuffer->buffer[i].size);
                for (size_t j = 0; j < bikeBuffer->buffer[i].size; j++) {
                    decoded += (char)bikeBuffer->buffer[i].data[j];
                }
                c.decoded = decoded;
                c.itemBytes = 120; // metadados do envelope por item
                candidates.push_back(c);
            }
        }
    }

    if (candidates.empty()) {
        delete bikeBuffer;
        unlock();
        return 0;
    }

    // Passo 1 (F18): medir bytes de cada item antes de montar o documento
    DynamicJsonDocument testDoc(BIKE_DATA_BUFFER);
    for (Candidate& c : candidates) {
        testDoc.clear();
        if (deserializeJson(testDoc, c.decoded) == DeserializationError::Ok) {
            c.itemBytes += measureJson(testDoc);
        } else {
            c.itemBytes += c.decoded.length() + 16;
        }
    }

    // Passo 2: selecionar itens que cabem no orçamento do lote
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
        delete bikeBuffer;
        unlock();
        return 0;
    }

    // Passo 3: montar o documento do lote
    BPRJsonHelper::addTimestamp(doc);
    doc["base_id"] = configCredentials.getBaseId();

    JsonArray dataArray = doc.createNestedArray("data");
    std::vector<String> includedBikeIds;
    std::vector<int> includedIndexes;

    for (int sel : selected) {
        const Candidate& c = candidates[sel];
        if (!loadBikeBuffer(c.bikeId, *bikeBuffer)) continue;
        if (c.index >= bikeBuffer->dataCount || bikeBuffer->buffer[c.index].uploaded) continue;

        const DataItem& meta = bikeBuffer->buffer[c.index];

        JsonObject item = dataArray.createNestedObject();
        item["bike_id"] = c.bikeId;
        item["ts"] = meta.timestamp;
        item["size"] = meta.size;
        item["crc32"] = String(meta.crc32, HEX);
        item["session_id"] = meta.session_id;
        item["seq"] = meta.seq;

        testDoc.clear();
        if (deserializeJson(testDoc, c.decoded) == DeserializationError::Ok) {
            item["data_decoded"] = testDoc;
        } else {
            item["data"] = c.decoded;
        }

        includedBikeIds.push_back(c.bikeId);
        includedIndexes.push_back(c.index);
    }

    int included = dataArray.size();
    if (included == 0 || doc.overflowed()) {
        Serial.println("❌ Batch build failed (empty or overflow)");
        delete bikeBuffer;
        unlock();
        return 0;
    }

    doc["data_count"] = included;

    // Passo 4: marcar EXATAMENTE os itens incluídos como enviados e persistir
    for (int i = 0; i < included; i++) {
        if (loadBikeBuffer(includedBikeIds[i], *bikeBuffer)) {
            int idx = includedIndexes[i];
            if (idx < bikeBuffer->dataCount) {
                bikeBuffer->buffer[idx].uploaded = true;
                saveBikeBuffer(includedBikeIds[i], *bikeBuffer);
            }
        }
    }

    delete bikeBuffer;
    unlock();
    return included;
}

// F18/F19: confirmar remove SOMENTE itens enviados; pendências ficam intactas
void BufferManager::markAsConfirmed()
{
    lock();
    createBackup();

    std::vector<String> ids = listBikeIds();
    BikeBuffer* bikeBuffer = new BikeBuffer;

    for (const String& bikeId : ids) {
        if (!loadBikeBuffer(bikeId, *bikeBuffer)) continue;

        BikeBuffer remaining;
        remaining.dataCount = 0;
        for (int i = 0; i < bikeBuffer->dataCount; i++) {
            if (!bikeBuffer->buffer[i].uploaded) {
                remaining.buffer[remaining.dataCount++] = bikeBuffer->buffer[i];
            }
        }

        if (remaining.dataCount == 0) {
            LittleFS.remove(getBikeBufferPath(bikeId));
            Serial.printf("🗑️ Buffer fully uploaded and removed: %s\n", bikeId.c_str());
        } else if (remaining.dataCount != bikeBuffer->dataCount) {
            saveBikeBuffer(bikeId, remaining);
            Serial.printf("📦 Partial confirm for %s: %d items remain\n", bikeId.c_str(), remaining.dataCount);
        }
    }

    delete bikeBuffer;
    lastSync = millis();
    Serial.println("✅ Uploaded buffers cleared after confirmed upload");
}

void BufferManager::rollbackUpload()
{
    lock();
    std::vector<String> ids = listBikeIds();
    BikeBuffer* bikeBuffer = new BikeBuffer;

    for (const String& bikeId : ids) {
        if (!loadBikeBuffer(bikeId, *bikeBuffer)) continue;
        bool changed = false;
        for (int i = 0; i < bikeBuffer->dataCount; i++) {
            if (bikeBuffer->buffer[i].uploaded) {
                bikeBuffer->buffer[i].uploaded = false;
                changed = true;
            }
        }
        if (changed) {
            saveBikeBuffer(bikeId, *bikeBuffer);
        }
    }

    delete bikeBuffer;
    unlock();
    Serial.println("⚠️ Upload failed - uploaded items marked as pending again");
}

// F17: validação completa — magic, versão, contagem limitada pelo array real,
// tamanho por item, CRC e leitura de bytes exatos
bool BufferManager::loadBikeBuffer(const String& bikeId, BikeBuffer& bikeBuffer)
{
    bikeBuffer.dataCount = 0;

    String path = getBikeBufferPath(bikeId);
    if (!LittleFS.exists(path)) {
        return true; // sem arquivo = buffer vazio válido
    }

    File bufferFile = LittleFS.open(path, "r");
    if (!bufferFile) return false;

    BufferFileHeader header;
    if (bufferFile.readBytes((char*)&header, sizeof(header)) != sizeof(header)) {
        bufferFile.close();
        Serial.printf("❌ Buffer header read failed: %s\n", path.c_str());
        return false;
    }

    if (header.magic != BUFFER_MAGIC || header.version != BUFFER_VERSION ||
        header.item_count > MAX_ITEMS_PER_BIKE) {
        bufferFile.close();
        Serial.printf("❌ Buffer header invalid: %s\n", path.c_str());
        return false;
    }

    for (uint32_t i = 0; i < header.item_count; i++) {
        BufferItemBin item;
        if (bufferFile.readBytes((char*)&item, sizeof(item)) != sizeof(item)) {
            Serial.printf("⚠️ Truncated buffer item in %s: keeping %d/%d items\n",
                          path.c_str(), bikeBuffer.dataCount, header.item_count);
            break;
        }

        if (item.size > MAX_ITEM_DATA_SIZE) {
            Serial.printf("⚠️ Item %d in %s has invalid size %d - skipping file\n",
                          i, path.c_str(), item.size);
            bikeBuffer.dataCount = 0;
            bufferFile.close();
            return false;
        }

        // F17: CRC verificado no load (era calculado só ao gravar)
        CRC32 crc;
        crc.update(item.data, item.size);
        if (crc.finalize() != item.crc32) {
            Serial.printf("⚠️ CRC mismatch in %s item %d - skipping file\n", path.c_str(), i);
            bikeBuffer.dataCount = 0;
            bufferFile.close();
            return false;
        }

        DataItem& dst = bikeBuffer.buffer[bikeBuffer.dataCount];
        dst.bikeId = intToBikeId(item.bikeId);
        dst.timestamp = item.timestamp;
        dst.size = item.size;
        dst.crc32 = item.crc32;
        dst.session_id = item.session_id;
        dst.session_salt = item.session_salt;
        dst.seq = item.seq;
        dst.uploaded = item.uploaded;
        dst.confirmed = item.confirmed;
        memcpy(dst.data, item.data, item.size);
        bikeBuffer.dataCount++;
    }

    bufferFile.close();
    return true;
}

bool BufferManager::saveBikeBuffer(const String& bikeId, const BikeBuffer& bikeBuffer)
{
    String path = getBikeBufferPath(bikeId);

    if (bikeBuffer.dataCount == 0) {
        if (LittleFS.exists(path)) {
            LittleFS.remove(path);
        }
        return true;
    }

    File file = LittleFS.open(path, "w");
    if (!file) {
        return false;
    }

    BufferFileHeader header;
    header.magic = BUFFER_MAGIC;
    header.version = BUFFER_VERSION;
    header.item_count = bikeBuffer.dataCount;
    header.last_update = time(nullptr);

    bool ok = file.write((uint8_t*)&header, sizeof(header)) == sizeof(header);

    for (int i = 0; ok && i < bikeBuffer.dataCount; i++) {
        BufferItemBin item;
        memset(&item, 0, sizeof(item));
        item.bikeId = bikeIdToInt(bikeBuffer.buffer[i].bikeId);
        item.timestamp = bikeBuffer.buffer[i].timestamp;
        item.size = bikeBuffer.buffer[i].size;
        item.crc32 = bikeBuffer.buffer[i].crc32;
        item.session_id = bikeBuffer.buffer[i].session_id;
        item.session_salt = bikeBuffer.buffer[i].session_salt;
        item.seq = bikeBuffer.buffer[i].seq;
        item.uploaded = bikeBuffer.buffer[i].uploaded;
        item.confirmed = bikeBuffer.buffer[i].confirmed;
        memcpy(item.data, bikeBuffer.buffer[i].data, bikeBuffer.buffer[i].size);

        if (file.write((uint8_t*)&item, sizeof(item)) != sizeof(item)) {
            ok = false;
        }
    }

    file.close();
    if (!ok) {
        Serial.printf("❌ Failed to write buffer file: %s\n", path.c_str());
        LittleFS.remove(path);
    }
    return ok;
}

void BufferManager::loadAllBuffers() {
    // Buffers são carregados sob demanda, por bike
}

void BufferManager::saveBuffer() {
    // Buffers são salvos individualmente
}

void BufferManager::createBackup()
{
    int totalCount = getTotalDataCountUnlocked();
    if (totalCount == 0) return;

    if (!LittleFS.exists("/backup")) {
        LittleFS.mkdir("/backup");
    }

    // Criar backup consolidado de todos os buffers
    DynamicJsonDocument backupDoc(BUFFER_PERSISTENCE_BUFFER);
    backupDoc["timestamp"] = time(nullptr);
    backupDoc["total_items"] = totalCount;

    JsonArray bikesArray = backupDoc.createNestedArray("bikes");

    std::vector<String> ids = listBikeIds();
    BikeBuffer* bikeBuffer = new BikeBuffer;

    for (const String& bikeId : ids) {
        if (!loadBikeBuffer(bikeId, *bikeBuffer) || bikeBuffer->dataCount == 0) continue;

        JsonObject bikeObj = bikesArray.createNestedObject();
        bikeObj["bike_id"] = bikeId;
        bikeObj["data_count"] = bikeBuffer->dataCount;

        JsonArray dataArray = bikeObj.createNestedArray("data");
        for (int i = 0; i < bikeBuffer->dataCount; i++) {
            JsonObject item = dataArray.createNestedObject();
            item["ts"] = bikeBuffer->buffer[i].timestamp;
            item["size"] = bikeBuffer->buffer[i].size;
            item["crc32"] = String(bikeBuffer->buffer[i].crc32, HEX);
            item["session_id"] = bikeBuffer->buffer[i].session_id;
            item["seq"] = bikeBuffer->buffer[i].seq;

            String dataStr;
            dataStr.reserve(bikeBuffer->buffer[i].size);
            for (size_t j = 0; j < bikeBuffer->buffer[i].size; j++) {
                dataStr += (char)bikeBuffer->buffer[i].data[j];
            }
            item["data"] = dataStr;
        }
    }
    delete bikeBuffer;

    File backup = LittleFS.open("/backup/data.bkp", "w");
    if (backup) {
        serializeJson(backupDoc, backup);
        backup.close();
        Serial.println("💾 Backup created: /backup/data.bkp");
    }
}

void BufferManager::cleanupOldBackups()
{
    uint32_t retentionHours = configManager.getBackupRetentionHours();
    uint32_t cutoffTime = time(nullptr) - (retentionHours * 3600);

    if (!hasEnoughSpace())
    {
        Serial.println("⚠️ Low storage - aggressive cleanup mode");
        cutoffTime = time(nullptr) - (retentionHours * 1800);
    }

    if (!LittleFS.exists("/backup")) return;

    File root = LittleFS.open("/backup");
    if (!root) return;
    File file = root.openNextFile();

    while (file)
    {
        String fileName = file.name();
        file.close();
        if (fileName.endsWith(".bkp"))
        {
            String fullPath = String("/backup/") + fileName;
            File backupFile = LittleFS.open(fullPath, "r");
            if (backupFile) {
                DynamicJsonDocument doc(256);
                if (deserializeJson(doc, backupFile) == DeserializationError::Ok) {
                    uint32_t fileTime = doc["timestamp"] | 0;
                    if (fileTime > 1000000000L && fileTime < cutoffTime) {
                        backupFile.close();
                        LittleFS.remove(fullPath);
                        Serial.printf("🗑️ Old backup removed: %s\n", fileName.c_str());
                        file = root.openNextFile();
                        continue;
                    }
                }
                backupFile.close();
            }
        }
        file = root.openNextFile();
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
    BikeBuffer* bikeBuffer = new BikeBuffer;
    for (const String& bikeId : ids) {
        if (loadBikeBuffer(bikeId, *bikeBuffer)) {
            totalCount += bikeBuffer->dataCount;
        }
    }
    delete bikeBuffer;
    return totalCount;
}

int BufferManager::getTotalDataCount()
{
    lock();
    int count = getTotalDataCountUnlocked();
    unlock();
    return count;
}

int BufferManager::getPendingCount()
{
    lock();
    int pending = 0;
    std::vector<String> ids = listBikeIds();
    BikeBuffer* bikeBuffer = new BikeBuffer;
    for (const String& bikeId : ids) {
        if (!loadBikeBuffer(bikeId, *bikeBuffer)) continue;
        for (int i = 0; i < bikeBuffer->dataCount; i++) {
            if (!bikeBuffer->buffer[i].uploaded) {
                pending++;
            }
        }
    }
    delete bikeBuffer;
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
