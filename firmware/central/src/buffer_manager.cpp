#include <ArduinoJson.h>
#include <CRC32.h>
#include <LittleFS.h>
#include "binary_structs.h"
#include "bpr_json_helper.h"
#include "buffer_manager.h"
#include "config_manager.h"
#include "config_credentials.h"
#include "constants.h"

extern ConfigManager configManager;
extern ConfigCredentials configCredentials;

BufferManager::BufferManager() : maxCapacity(0), currentUsage(0), initialized(false), lastSync(0) {}

bool BufferManager::beginWithAvailableHeap() {
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t systemReserve = 30000;  // 30KB para sistema/BLE/etc
    
    if (freeHeap < systemReserve + 10000) {
        Serial.printf("❌ Heap insuficiente: %d bytes\n", freeHeap);
        return false;
    }
    
    maxCapacity = calculateCapacity();
    currentUsage = 0;
    initialized = true;
    
    Serial.printf("📊 Buffer dinâmico: %d bytes disponíveis\n", maxCapacity);
    Serial.printf("   Capacidade: ~%d items\n", maxCapacity / 128);
    
    return true;
}

uint32_t BufferManager::calculateCapacity() {
    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t systemReserve = 30000;  // BLE + sistema
    uint32_t available = freeHeap - systemReserve;
    
    // 70% da memória disponível para buffer (margem de segurança)
    return available * 0.7;
}

void BufferManager::cleanup() {
    currentUsage = 0;
    Serial.printf("🧹 Buffer limpo - %d bytes liberados\n", maxCapacity);
}

void BufferManager::begin()
{
    // Diretórios já criados no self-check
    cleanupOldBackups();
    loadAllBuffers();
    Serial.printf("📥 DataBuffer initialized: %d total items\n", getTotalDataCount());
}

String BufferManager::getBikeBufferPath(const String &bikeId) {
    // Encontrar próximo número sequencial
    int nextNum = 1;
    File root = LittleFS.open(BUFFER_DIR);
    File file = root.openNextFile();
    
    while (file) {
        String fileName = file.name();
        if (fileName.startsWith("scan") && fileName.endsWith(".bin")) {
            int num = fileName.substring(4, fileName.length() - 4).toInt();
            if (num >= nextNum) {
                nextNum = num + 1;
            }
        }
        file = root.openNextFile();
    }
    
    char filename[32];
    snprintf(filename, sizeof(filename), "/buffer/scan%03d.bin", nextNum);
    return String(filename);
}

bool BufferManager::addConfigData(const String &configType, const String &jsonData)
{
    DynamicJsonDocument doc(BIKE_DATA_BUFFER);
    BPRJsonHelper::addConfigFields(doc, configType, jsonData);

    String modifiedJson;
    serializeJson(doc, modifiedJson);

    return addData(configType, (uint8_t *)modifiedJson.c_str(), modifiedJson.length());
}

bool BufferManager::addBikeData(const String &bikeId, const String &jsonData)
{
    // Parse JSON recebido
    DynamicJsonDocument doc(BIKE_DATA_BUFFER);
    DeserializationError error = deserializeJson(doc, jsonData);

    if (error)
    {
        Serial.printf("❌ JSON parse error in addBikeData: %s\n", error.c_str());
        return false;
    }

    // Adicionar timestamp da central
    BPRJsonHelper::addTimestamp(doc, "central_receive");

    // Serializar JSON modificado
    String modifiedJson;
    serializeJson(doc, modifiedJson);

    // Chamar método original
    return addData(bikeId, (uint8_t *)modifiedJson.c_str(), modifiedJson.length());
}

bool BufferManager::addData(const String &bikeId, const uint8_t *data, size_t length)
{
    if (length > 128) {
        Serial.printf("❌ Data too large: %d bytes\n", length);
        return false;
    }

    // Carregar buffer da bike específica
    BikeBuffer bikeBuffer;
    loadBikeBuffer(bikeId, bikeBuffer);
    
    if (bikeBuffer.dataCount >= 10) {
        Serial.printf("❌ Buffer full for bike %s\n", bikeId.c_str());
        return false;
    }

    // Calcular CRC32
    CRC32 crc;
    crc.update(data, length);
    uint32_t checksum = crc.finalize();

    // Armazenar dados
    int index = bikeBuffer.dataCount;
    bikeBuffer.buffer[index].bikeId = bikeId;
    bikeBuffer.buffer[index].timestamp = time(nullptr);
    bikeBuffer.buffer[index].size = length;
    bikeBuffer.buffer[index].crc32 = checksum;
    bikeBuffer.buffer[index].uploaded = false;
    bikeBuffer.buffer[index].confirmed = false;
    memcpy(bikeBuffer.buffer[index].data, data, length);
    bikeBuffer.dataCount++;

    Serial.printf("📦 Data added: %s [%d bytes, CRC:%08X]\n", bikeId.c_str(), length, checksum);

    // Salvar buffer da bike
    saveBikeBuffer(bikeId, bikeBuffer);
    
    return true;
}

bool BufferManager::getDataForUpload(DynamicJsonDocument &doc)
{
    int totalCount = getTotalDataCount();
    if (totalCount == 0) {
        return false;
    }

    BPRJsonHelper::addTimestamp(doc);
    doc["base_id"] = configCredentials.getBaseId();
    doc["data_count"] = totalCount;

    JsonArray dataArray = doc.createNestedArray("data");

    // Iterar por todos os arquivos de buffer
    File root = LittleFS.open(BUFFER_DIR);
    if (!root) return false;
    
    DynamicJsonDocument testDoc(BIKE_DATA_BUFFER); // Move outside loop
    File file = root.openNextFile();
    
    while (file) {
        String fileName = file.name();
        if (fileName.endsWith(".bin")) {
            // Extrair bikeId do formato: bikeId-timestamp.bin
            int dashPos = fileName.lastIndexOf('-');
            String bikeId = (dashPos > 0) ? fileName.substring(0, dashPos) : fileName.substring(0, fileName.length() - 4);
            
            BikeBuffer bikeBuffer;
            if (loadBikeBuffer(bikeId, bikeBuffer)) {
                for (int i = 0; i < bikeBuffer.dataCount; i++) {
                    JsonObject item = dataArray.createNestedObject();
                    item["bike_id"] = bikeBuffer.buffer[i].bikeId;
                    item["ts"] = bikeBuffer.buffer[i].timestamp;
                    item["size"] = bikeBuffer.buffer[i].size;
                    item["crc32"] = String(bikeBuffer.buffer[i].crc32, HEX);

                    // Convert data back to JSON
                    String decodedData = "";
                    for (size_t j = 0; j < bikeBuffer.buffer[i].size; j++) {
                        decodedData += (char)bikeBuffer.buffer[i].data[j];
                    }

                    // Test if decoded data is valid JSON
                    testDoc.clear();
                    if (deserializeJson(testDoc, decodedData) == DeserializationError::Ok) {
                        item["data_decoded"] = testDoc;
                    } else {
                        item["data"] = decodedData;
                    }

                    // Marcar como enviado
                    bikeBuffer.buffer[i].uploaded = true;
                }
                
                // Salvar buffer atualizado
                saveBikeBuffer(bikeId, bikeBuffer);
            }
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();

    return true;
}

void BufferManager::markAsConfirmed()
{
    // Criar backup antes de limpar
    createBackup();

    // Limpar todos os buffers de bikes
    File root = LittleFS.open(BUFFER_DIR);
    File file = root.openNextFile();
    
    while (file) {
        String fileName = file.name();
        if (fileName.endsWith(".bin")) {
            String fullPath = String(BUFFER_DIR) + "/" + fileName;
            LittleFS.remove(fullPath);
            Serial.printf("🗑️ Removed buffer: %s\n", fullPath.c_str());
        }
        file = root.openNextFile();
    }

    lastSync = millis();
    Serial.println("✅ All buffers cleared after confirmed upload");
}

void BufferManager::rollbackUpload()
{
    // Marcar dados como não enviados em todos os buffers
    File root = LittleFS.open(BUFFER_DIR);
    File file = root.openNextFile();
    
    while (file) {
        String fileName = file.name();
        if (fileName.endsWith(".bin")) {
            String bikeId = fileName.substring(0, fileName.length() - 4);
            
            BikeBuffer bikeBuffer;
            if (loadBikeBuffer(bikeId, bikeBuffer)) {
                for (int i = 0; i < bikeBuffer.dataCount; i++) {
                    bikeBuffer.buffer[i].uploaded = false;
                }
                saveBikeBuffer(bikeId, bikeBuffer);
            }
        }
        file = root.openNextFile();
    }
    
    Serial.println("⚠️ Upload failed - all data marked as pending");
}

bool BufferManager::loadBikeBuffer(const String &bikeId, BikeBuffer &bikeBuffer) {
    bikeBuffer.dataCount = 0;
    
    // Find the most recent file for this bikeId
    File root = LittleFS.open(BUFFER_DIR);
    if (!root) return false;
    
    String targetFile = "";
    File file = root.openNextFile();
    while (file) {
        String fileName = file.name();
        if (fileName.endsWith(".bin") && fileName.indexOf(bikeId) >= 0) {
            targetFile = String(BUFFER_DIR) + "/" + fileName;
            break;
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
    
    if (targetFile.isEmpty()) return true; // No file found, empty buffer is OK
    
    File bufferFile = LittleFS.open(targetFile, "r");
    if (!bufferFile) return false;
    
    // Read header
    BufferFileHeader header;
    if (bufferFile.readBytes((char*)&header, sizeof(header)) != sizeof(header)) {
        bufferFile.close();
        return false;
    }
    
    if (header.magic != BUFFER_MAGIC || header.item_count > MAX_BUFFER_SIZE) {
        bufferFile.close();
        return false;
    }
    
    // Read items
    for (uint32_t i = 0; i < header.item_count && i < MAX_BUFFER_SIZE; i++) {
        BufferItemBin item;
        if (bufferFile.readBytes((char*)&item, sizeof(item)) != sizeof(item)) break;
        
        bikeBuffer.buffer[i].bikeId = intToBikeId(item.bikeId);
        bikeBuffer.buffer[i].timestamp = item.timestamp;
        bikeBuffer.buffer[i].size = item.size;
        bikeBuffer.buffer[i].crc32 = item.crc32;
        bikeBuffer.buffer[i].uploaded = item.uploaded;
        bikeBuffer.buffer[i].confirmed = item.confirmed;
        memcpy(bikeBuffer.buffer[i].data, item.data, min(item.size, (uint16_t)128));
        bikeBuffer.dataCount++;
    }
    
    bufferFile.close();
    return true;
}

bool BufferManager::saveBikeBuffer(const String &bikeId, const BikeBuffer &bikeBuffer) {
    String filePath = getBikeBufferPath(bikeId);
    
    if (bikeBuffer.dataCount == 0) {
        // Se não há dados, remover arquivo
        if (LittleFS.exists(filePath)) {
            LittleFS.remove(filePath);
        }
        return true;
    }
    
    File file = LittleFS.open(filePath, "w");
    if (!file) {
        return false;
    }
    
    // Escrever header
    BufferFileHeader header;
    header.magic = BUFFER_MAGIC;
    header.version = BUFFER_VERSION;
    header.item_count = bikeBuffer.dataCount;
    header.last_update = time(nullptr);
    
    file.write((uint8_t*)&header, sizeof(header));
    
    // Escrever items
    for (int i = 0; i < bikeBuffer.dataCount; i++) {
        BufferItemBin item;
        item.bikeId = bikeIdToInt(bikeBuffer.buffer[i].bikeId);
        item.timestamp = bikeBuffer.buffer[i].timestamp;
        item.size = bikeBuffer.buffer[i].size;
        item.crc32 = bikeBuffer.buffer[i].crc32;
        item.uploaded = bikeBuffer.buffer[i].uploaded;
        item.confirmed = bikeBuffer.buffer[i].confirmed;
        memcpy(item.data, bikeBuffer.buffer[i].data, bikeBuffer.buffer[i].size);
        
        file.write((uint8_t*)&item, sizeof(item));
    }
    
    file.close();
    return true;
}

void BufferManager::loadAllBuffers() {
    // Método para compatibilidade - não precisa fazer nada
    // Os buffers são carregados sob demanda
}

void BufferManager::saveBuffer() {
    // Método para compatibilidade - não precisa fazer nada
    // Os buffers são salvos individualmente
}

void BufferManager::createBackup()
{
    int totalCount = getTotalDataCount();
    if (totalCount == 0) return;

    // Criar diretório backup se não existir
    if (!LittleFS.exists("/backup")) {
        LittleFS.mkdir("/backup");
    }
    
    char backupFile[64];
    sprintf(backupFile, "/backup/data.bkp");

    // Criar backup consolidado de todos os buffers
    DynamicJsonDocument backupDoc(BUFFER_PERSISTENCE_BUFFER);
    backupDoc["timestamp"] = time(nullptr);
    backupDoc["total_items"] = totalCount;
    
    JsonArray bikesArray = backupDoc.createNestedArray("bikes");
    
    File root = LittleFS.open(BUFFER_DIR);
    File file = root.openNextFile();
    
    while (file) {
        String fileName = file.name();
        if (fileName.endsWith(".bin")) {
            String bikeId = fileName.substring(0, fileName.length() - 4);
            
            BikeBuffer bikeBuffer;
            if (loadBikeBuffer(bikeId, bikeBuffer) && bikeBuffer.dataCount > 0) {
                JsonObject bikeObj = bikesArray.createNestedObject();
                bikeObj["bike_id"] = bikeId;
                bikeObj["data_count"] = bikeBuffer.dataCount;
                
                JsonArray dataArray = bikeObj.createNestedArray("data");
                for (int i = 0; i < bikeBuffer.dataCount; i++) {
                    JsonObject item = dataArray.createNestedObject();
                    item["ts"] = bikeBuffer.buffer[i].timestamp;
                    item["size"] = bikeBuffer.buffer[i].size;
                    
                    String dataStr = "";
                    for (size_t j = 0; j < bikeBuffer.buffer[i].size; j++) {
                        dataStr += (char)bikeBuffer.buffer[i].data[j];
                    }
                    item["data"] = dataStr;
                }
            }
        }
        file = root.openNextFile();
    }

    File backup = LittleFS.open(backupFile, "w");
    if (backup) {
        serializeJson(backupDoc, backup);
        backup.close();
        Serial.printf("💾 Backup created: %s\n", backupFile);
    }
}

void BufferManager::cleanupOldBackups()
{
    uint32_t retentionHours = configManager.getBackupRetentionHours();
    uint32_t cutoffTime = time(nullptr) - (retentionHours * 3600);

    // Se pouco espaço, ser mais agressivo na limpeza
    if (!hasEnoughSpace())
    {
        Serial.println("⚠️ Low storage - aggressive cleanup mode");
        cutoffTime = time(nullptr) - (retentionHours * 1800); // Metade do tempo
    }

    File root = LittleFS.open("/");
    File file = root.openNextFile();

    while (file)
    {
        String fileName = file.name();
        if (fileName.startsWith("backup/") && fileName.endsWith(".bkp"))
        {
            // Extrair timestamp do conteúdo do arquivo para limpeza
            String fullPath = "/" + fileName;
            File backupFile = LittleFS.open(fullPath, "r");
            if (backupFile) {
                DynamicJsonDocument doc(256);
                if (deserializeJson(doc, backupFile) == DeserializationError::Ok) {
                    uint32_t fileTime = doc["timestamp"] | 0;
                    if (fileTime > 0 && fileTime < cutoffTime) {
                        LittleFS.remove(fullPath);
                        Serial.printf("🗑️ Old backup removed: %s\n", fileName.c_str());
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
    int totalCount = getTotalDataCount();
    int threshold = (MAX_BUFFER_SIZE * BUFFER_SYNC_THRESHOLD_PERCENT) / 100;
    return totalCount >= threshold;
}

bool BufferManager::hasData()
{
    return getTotalDataCount() > 0;
}

int BufferManager::getDataCount()
{
    return getTotalDataCount();
}

int BufferManager::getTotalDataCount()
{
    int totalCount = 0;
    
    if (!LittleFS.exists(BUFFER_DIR)) {
        return 0;
    }
    
    File root = LittleFS.open(BUFFER_DIR);
    File file = root.openNextFile();
    
    while (file) {
        String fileName = file.name();
        if (fileName.endsWith(".bin")) {
            String bikeId = fileName.substring(0, fileName.length() - 4);
            
            BikeBuffer bikeBuffer;
            if (loadBikeBuffer(bikeId, bikeBuffer)) {
                totalCount += bikeBuffer.dataCount;
            }
        }
        file = root.openNextFile();
    }
    
    return totalCount;
}

int BufferManager::getPendingCount()
{
    int pending = 0;
    
    if (!LittleFS.exists(BUFFER_DIR)) {
        return 0;
    }
    
    File root = LittleFS.open(BUFFER_DIR);
    File file = root.openNextFile();
    
    while (file) {
        String fileName = file.name();
        if (fileName.endsWith(".bin")) {
            String bikeId = fileName.substring(0, fileName.length() - 4);
            
            BikeBuffer bikeBuffer;
            if (loadBikeBuffer(bikeId, bikeBuffer)) {
                for (int i = 0; i < bikeBuffer.dataCount; i++) {
                    if (!bikeBuffer.buffer[i].uploaded) {
                        pending++;
                    }
                }
            }
        }
        file = root.openNextFile();
    }
    
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

    // Listar arquivos principais
    Serial.printf("📄 Main Files:\n");
    printFileSize(BIKE_REGISTRY_FILE);
    printFileSize(BIKE_STATUS_FILE);
    printFileSize("/central_config.json");

    // Contar buffers individuais
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
            file = root.openNextFile();
        }
    }
    
    Serial.printf("📦 Buffers: %d files, %d KB, %d total items\n", 
                  bufferCount, bufferSize / 1024, getTotalDataCount());

    // Contar backups
    int backupCount = 0;
    size_t backupSize = 0;
    if (LittleFS.exists("/backup")) {
        File backupRoot = LittleFS.open("/backup");
        File backupFile = backupRoot.openNextFile();
        
        while (backupFile) {
            String fileName = backupFile.name();
            if (fileName.endsWith(".bkp")) {
                backupCount++;
                backupSize += backupFile.size();
            }
            backupFile = backupRoot.openNextFile();
        }
    }

    Serial.printf("💾 Backups: %d files, %d KB\n", backupCount, backupSize / 1024);

    // Alerta se pouco espaço
    if (freeBytes < 10240)
    { // < 10KB
        Serial.printf("⚠️ LOW STORAGE WARNING: Only %d KB free!\n", freeBytes / 1024);
    }
}

void BufferManager::printFileSize(const String &filePath)
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
    return freeBytes > MINIMAL_FREE_FS_SPACE; // Mínimo 20KB livres
}