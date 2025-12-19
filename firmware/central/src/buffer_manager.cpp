#include "buffer_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <CRC32.h>
#include "constants.h"
#include "config_manager.h"

extern ConfigManager configManager;

BufferManager::BufferManager() : dataCount(0), lastSync(0) {}

void BufferManager::begin()
{
    cleanupOldBackups();
    loadBuffer();
    Serial.printf("📥 DataBuffer initialized: %d items\n", dataCount);
}

bool BufferManager::addBikeData(const String& bikeId, const String& jsonData)
{
    // Parse JSON recebido
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, jsonData);
    
    if (error) {
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
    return addData(bikeId, (uint8_t*)modifiedJson.c_str(), modifiedJson.length());
}

bool BufferManager::addData(const String& bikeId, const uint8_t *data, size_t length)
{
    if (dataCount >= MAX_BUFFER_SIZE || length > 256)
    {
        return false;
    }

    // Comprimir se configurado
    uint8_t* finalData = (uint8_t*)data;
    size_t finalSize = length;
    bool compressed = false;
    
    if (configManager.getCompressionEnabled() && length > configManager.getCompressionMinSize()) {
        // TODO: Implementar compressão quando necessário
        // finalData = compress(data, length, &finalSize);
        // compressed = true;
    }

    // Calcular CRC32
    CRC32 crc;
    crc.update(finalData, finalSize);
    uint32_t checksum = crc.finalize();

    // Armazenar dados
    buffer[dataCount].bikeId = bikeId;
    buffer[dataCount].timestamp = time(nullptr);
    buffer[dataCount].size = finalSize;
    buffer[dataCount].crc32 = checksum;
    buffer[dataCount].uploaded = false;
    buffer[dataCount].confirmed = false;
    buffer[dataCount].compressed = compressed;
    memcpy(buffer[dataCount].data, finalData, finalSize);
    dataCount++;

    Serial.printf("📦 Data added: %s [%d bytes, CRC:%08X]\n", bikeId.c_str(), finalSize, checksum);

    // Auto-save periodicamente
    if (dataCount % 5 == 0) {
        saveBuffer();
    }

    return true;
}

bool BufferManager::needsSync()
{
    int threshold = (MAX_BUFFER_SIZE * 80) / 100; // 80% do buffer
    uint32_t syncInterval = configManager.getConfig().intervals.sync_sec * 1000; // sec -> ms
    
    return dataCount >= threshold || 
           (dataCount > 0 && (millis() - lastSync) > syncInterval);
}

bool BufferManager::isCriticallyFull()
{
    int criticalThreshold = (MAX_BUFFER_SIZE * 95) / 100; // 95% do buffer
    return dataCount >= criticalThreshold;
}

bool BufferManager::getDataForUpload(DynamicJsonDocument &doc)
{
    if (dataCount == 0) {
        return false;
    }

    doc["timestamp"] = time(nullptr);
    doc["base_id"] = configManager.getBaseId();
    doc["data_count"] = dataCount;

    JsonArray dataArray = doc.createNestedArray("data");

    for (int i = 0; i < dataCount; i++)
    {
        JsonObject item = dataArray.createNestedObject();
        item["bike_id"] = buffer[i].bikeId;
        item["ts"] = buffer[i].timestamp;
        item["size"] = buffer[i].size;
        item["crc32"] = String(buffer[i].crc32, HEX);
        item["compressed"] = buffer[i].compressed;

        String hexData = "";
        for (size_t j = 0; j < buffer[i].size; j++)
        {
            char hex[3];
            sprintf(hex, "%02X", buffer[i].data[j]);
            hexData += hex;
        }
        item["data"] = hexData;
        
        // Marcar como enviado
        buffer[i].uploaded = true;
    }

    return true;
}

void BufferManager::markAsConfirmed()
{
    // Criar backup antes de limpar
    createBackup();
    
    // Limpar dados confirmados
    dataCount = 0;
    lastSync = millis();
    saveBuffer();
    
    Serial.println("✅ Buffer cleared after confirmed upload");
}

void BufferManager::rollbackUpload()
{
    // Marcar dados como não enviados em caso de falha
    for (int i = 0; i < dataCount; i++) {
        buffer[i].uploaded = false;
    }
    Serial.println("⚠️ Upload failed - data marked as pending");
}

void BufferManager::loadBuffer()
{
    if (!LittleFS.exists(BUFFER_FILE)) {
        dataCount = 0;
        lastSync = 0;
        return;
    }

    File file = LittleFS.open(BUFFER_FILE, "r");
    if (!file) return;

    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, file) != DeserializationError::Ok) {
        file.close();
        return;
    }
    file.close();

    dataCount = doc["data_count"] | 0;
    lastSync = doc["last_sync"] | 0;

    JsonArray dataArray = doc["buffer"];
    int loadedCount = 0;

    for (JsonObject item : dataArray) {
        if (loadedCount >= MAX_BUFFER_SIZE) break;

        buffer[loadedCount].bikeId = item["bike_id"] | "unknown";
        buffer[loadedCount].timestamp = item["ts"];
        buffer[loadedCount].size = item["size"];
        buffer[loadedCount].crc32 = strtoul(item["crc32"] | "0", NULL, 16);
        buffer[loadedCount].uploaded = item["uploaded"] | false;
        buffer[loadedCount].confirmed = item["confirmed"] | false;
        buffer[loadedCount].compressed = item["compressed"] | false;

        String hexData = item["data"];
        size_t dataSize = hexData.length() / 2;

        for (size_t i = 0; i < dataSize && i < 256; i++) {
            String byteString = hexData.substring(i * 2, i * 2 + 2);
            buffer[loadedCount].data[i] = strtol(byteString.c_str(), NULL, 16);
        }

        loadedCount++;
    }

    dataCount = loadedCount;
}

void BufferManager::saveBuffer()
{
    DynamicJsonDocument doc(8192);

    doc["data_count"] = dataCount;
    doc["last_sync"] = lastSync;

    JsonArray dataArray = doc.createNestedArray("buffer");
    for (int i = 0; i < dataCount; i++) {
        JsonObject item = dataArray.createNestedObject();
        item["bike_id"] = buffer[i].bikeId;
        item["ts"] = buffer[i].timestamp;
        item["size"] = buffer[i].size;
        item["crc32"] = String(buffer[i].crc32, HEX);
        item["uploaded"] = buffer[i].uploaded;
        item["confirmed"] = buffer[i].confirmed;
        item["compressed"] = buffer[i].compressed;

        String hexData = "";
        for (size_t j = 0; j < buffer[i].size; j++) {
            char hex[3];
            sprintf(hex, "%02X", buffer[i].data[j]);
            hexData += hex;
        }
        item["data"] = hexData;
    }

    File file = LittleFS.open(BUFFER_FILE, "w");
    if (file) {
        serializeJson(doc, file);
        file.close();
    }
}

void BufferManager::createBackup()
{
    if (dataCount == 0) return;

    char backupFile[64];
    sprintf(backupFile, "/backup_%lu.json", time(nullptr));

    File source = LittleFS.open(BUFFER_FILE, "r");
    File backup = LittleFS.open(backupFile, "w");
    
    if (source && backup) {
        while (source.available()) {
            backup.write(source.read());
        }
        Serial.printf("💾 Backup created: %s\n", backupFile);
    }
    
    if (source) source.close();
    if (backup) backup.close();
}

void BufferManager::cleanupOldBackups()
{
    uint32_t retentionHours = configManager.getBackupRetentionHours();
    uint32_t cutoffTime = time(nullptr) - (retentionHours * 3600);
    
    // Se pouco espaço, ser mais agressivo na limpeza
    if (!hasEnoughSpace()) {
        Serial.println("⚠️ Low storage - aggressive cleanup mode");
        cutoffTime = time(nullptr) - (retentionHours * 1800); // Metade do tempo
    }
    
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    
    while (file) {
        String fileName = file.name();
        if (fileName.startsWith("backup_")) {
            // Extrair timestamp do nome do arquivo
            int underscorePos = fileName.indexOf('_');
            int dotPos = fileName.indexOf('.');
            if (underscorePos > 0 && dotPos > underscorePos) {
                String timestampStr = fileName.substring(underscorePos + 1, dotPos);
                uint32_t fileTime = timestampStr.toInt();
                
                if (fileTime < cutoffTime) {
                    LittleFS.remove("/" + fileName);
                    Serial.printf("🗑️ Old backup removed: %s\n", fileName.c_str());
                }
            }
        }
        file = root.openNextFile();
    }
}

int BufferManager::getDataCount()
{
    return dataCount;
}

int BufferManager::getPendingCount()
{
    int pending = 0;
    for (int i = 0; i < dataCount; i++) {
        if (!buffer[i].uploaded) pending++;
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
    printFileSize(BUFFER_FILE);
    printFileSize(BIKE_REGISTRY_FILE);
    printFileSize("/central_config.json");
    
    // Contar backups
    int backupCount = 0;
    size_t backupSize = 0;
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    
    while (file) {
        String fileName = file.name();
        if (fileName.startsWith("backup_")) {
            backupCount++;
            backupSize += file.size();
        }
        file = root.openNextFile();
    }
    
    Serial.printf("💾 Backups: %d files, %d KB\n", backupCount, backupSize / 1024);
    
    // Alerta se pouco espaço
    if (freeBytes < 10240) { // < 10KB
        Serial.printf("⚠️ LOW STORAGE WARNING: Only %d KB free!\n", freeBytes / 1024);
    }
}

void BufferManager::printFileSize(const String& filePath)
{
    if (LittleFS.exists(filePath)) {
        File file = LittleFS.open(filePath, "r");
        if (file) {
            Serial.printf("   %s: %d bytes\n", filePath.c_str(), file.size());
            file.close();
        }
    } else {
        Serial.printf("   %s: not found\n", filePath.c_str());
    }
}

bool BufferManager::hasEnoughSpace()
{
    size_t freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
    return freeBytes > 20480; // Mínimo 20KB livres
}