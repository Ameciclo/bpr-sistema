#include "buffer_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <CRC32.h>
#include "constants.h"
#include "config_manager.h"

extern ConfigManager configManager;

BufferManager::BufferManager() : lastSync(0) {}

void BufferManager::begin()
{
    // Criar diretório buffer se não existir
    if (!LittleFS.exists(BUFFER_DIR)) {
        LittleFS.mkdir(BUFFER_DIR);
        Serial.printf("📁 Created buffer directory: %s\n", BUFFER_DIR);
    }
    
    cleanupOldBackups();
    loadAllBuffers();
    Serial.printf("📥 DataBuffer initialized: %d total items\n", getTotalDataCount());
}

String BufferManager::getBikeBufferPath(const String &bikeId) {
    return String(BUFFER_DIR) + "/" + bikeId + ".json";
}

bool BufferManager::addConfigData(const String &configType, const String &jsonData)
{
    // Adicionar timestamp da central
    time_t now = time(nullptr);
    struct tm timeinfo;
    getLocalTime(&timeinfo);

    char dateStr[64];
    strftime(dateStr, sizeof(dateStr), "%Y-%m-%d %H:%M:%S UTC-3", &timeinfo);

    DynamicJsonDocument doc(BIKE_DATA_BUFFER);
    doc["config_type"] = configType;
    doc["data"] = jsonData;
    doc["central_receive_timestamp"] = now;
    doc["central_receive_timestamp_human"] = dateStr;

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
    time_t now = time(nullptr);
    struct tm timeinfo;
    getLocalTime(&timeinfo);

    char dateStr[64];
    strftime(dateStr, sizeof(dateStr), "%Y-%m-%d %H:%M:%S UTC-3", &timeinfo);

    doc["central_receive_timestamp"] = now;
    doc["central_receive_timestamp_human"] = dateStr;

    // Serializar JSON modificado
    String modifiedJson;
    serializeJson(doc, modifiedJson);

    // Chamar método original
    return addData(bikeId, (uint8_t *)modifiedJson.c_str(), modifiedJson.length());
}

bool BufferManager::addData(const String &bikeId, const uint8_t *data, size_t length)
{
    if (length > 256) {
        Serial.printf("❌ Data too large: %d bytes\n", length);
        return false;
    }

    // Carregar buffer da bike específica
    BikeBuffer bikeBuffer;
    loadBikeBuffer(bikeId, bikeBuffer);
    
    if (bikeBuffer.dataCount >= MAX_BUFFER_SIZE) {
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

    doc["timestamp"] = time(nullptr);
    doc["base_id"] = configManager.getBaseId();
    doc["data_count"] = totalCount;

    JsonArray dataArray = doc.createNestedArray("data");

    // Iterar por todos os arquivos de buffer
    File root = LittleFS.open(BUFFER_DIR);
    File file = root.openNextFile();
    
    while (file) {
        String fileName = file.name();
        if (fileName.endsWith(".json")) {
            String bikeId = fileName.substring(0, fileName.length() - 5); // Remove .json
            
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
                    DynamicJsonDocument testDoc(BIKE_HEARTBEAT_BUFFER);
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
        file = root.openNextFile();
    }

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
        if (fileName.endsWith(".json")) {
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
        if (fileName.endsWith(".json")) {
            String bikeId = fileName.substring(0, fileName.length() - 5);
            
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
    String filePath = getBikeBufferPath(bikeId);
    
    bikeBuffer.dataCount = 0;
    
    if (!LittleFS.exists(filePath)) {
        return true; // Buffer vazio é válido
    }

    File file = LittleFS.open(filePath, "r");
    if (!file) {
        return false;
    }

    DynamicJsonDocument doc(BUFFER_PERSISTENCE_BUFFER);
    if (deserializeJson(doc, file) != DeserializationError::Ok) {
        file.close();
        return false;
    }
    file.close();

    bikeBuffer.dataCount = doc["data_count"] | 0;
    JsonArray dataArray = doc["buffer"];
    int loadedCount = 0;

    for (JsonObject item : dataArray) {
        if (loadedCount >= MAX_BUFFER_SIZE) break;

        bikeBuffer.buffer[loadedCount].bikeId = item["bike_id"] | "unknown";
        bikeBuffer.buffer[loadedCount].timestamp = item["ts"];
        bikeBuffer.buffer[loadedCount].size = item["size"];
        bikeBuffer.buffer[loadedCount].crc32 = strtoul(item["crc32"] | "0", NULL, 16);
        bikeBuffer.buffer[loadedCount].uploaded = item["uploaded"] | false;
        bikeBuffer.buffer[loadedCount].confirmed = item["confirmed"] | false;

        String dataStr = item["data"];
        size_t dataSize = dataStr.length();
        
        for (size_t i = 0; i < dataSize && i < 256; i++) {
            bikeBuffer.buffer[loadedCount].data[i] = dataStr[i];
        }

        loadedCount++;
    }

    bikeBuffer.dataCount = loadedCount;
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
    
    DynamicJsonDocument doc(BUFFER_PERSISTENCE_BUFFER);
    doc["bike_id"] = bikeId;
    doc["data_count"] = bikeBuffer.dataCount;
    doc["last_update"] = time(nullptr);

    JsonArray dataArray = doc.createNestedArray("buffer");
    for (int i = 0; i < bikeBuffer.dataCount; i++) {
        JsonObject item = dataArray.createNestedObject();
        item["bike_id"] = bikeBuffer.buffer[i].bikeId;
        item["ts"] = bikeBuffer.buffer[i].timestamp;
        item["size"] = bikeBuffer.buffer[i].size;
        item["crc32"] = String(bikeBuffer.buffer[i].crc32, HEX);
        item["uploaded"] = bikeBuffer.buffer[i].uploaded;
        item["confirmed"] = bikeBuffer.buffer[i].confirmed;

        String dataStr = "";
        for (size_t j = 0; j < bikeBuffer.buffer[i].size; j++) {
            dataStr += (char)bikeBuffer.buffer[i].data[j];
        }
        item["data"] = dataStr;
    }

    File file = LittleFS.open(filePath, "w");
    if (!file) {
        return false;
    }
    
    serializeJson(doc, file);
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

    char backupFile[64];
    sprintf(backupFile, "/backup_%lu.json", time(nullptr));

    // Criar backup consolidado de todos os buffers
    DynamicJsonDocument backupDoc(BUFFER_PERSISTENCE_BUFFER);
    backupDoc["timestamp"] = time(nullptr);
    backupDoc["total_items"] = totalCount;
    
    JsonArray bikesArray = backupDoc.createNestedArray("bikes");
    
    File root = LittleFS.open(BUFFER_DIR);
    File file = root.openNextFile();
    
    while (file) {
        String fileName = file.name();
        if (fileName.endsWith(".json")) {
            String bikeId = fileName.substring(0, fileName.length() - 5);
            
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
        if (fileName.startsWith("backup_"))
        {
            // Extrair timestamp do nome do arquivo
            int underscorePos = fileName.indexOf('_');
            int dotPos = fileName.indexOf('.');
            if (underscorePos > 0 && dotPos > underscorePos)
            {
                String timestampStr = fileName.substring(underscorePos + 1, dotPos);
                uint32_t fileTime = timestampStr.toInt();

                if (fileTime < cutoffTime)
                {
                    LittleFS.remove("/" + fileName);
                    Serial.printf("🗑️ Old backup removed: %s\n", fileName.c_str());
                }
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
        if (fileName.endsWith(".json")) {
            String bikeId = fileName.substring(0, fileName.length() - 5);
            
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
        if (fileName.endsWith(".json")) {
            String bikeId = fileName.substring(0, fileName.length() - 5);
            
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
            if (fileName.endsWith(".json")) {
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
    File root = LittleFS.open("/");
    File file = root.openNextFile();

    while (file)
    {
        String fileName = file.name();
        if (fileName.startsWith("backup_"))
        {
            backupCount++;
            backupSize += file.size();
        }
        file = root.openNextFile();
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