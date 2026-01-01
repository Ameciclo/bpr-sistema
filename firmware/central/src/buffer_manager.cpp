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
    if (dataCount >= MAX_BUFFER_SIZE || length > 256)
    {
        return false;
    }

    // Calcular CRC32
    CRC32 crc;
    crc.update(data, length);
    uint32_t checksum = crc.finalize();

    // Armazenar dados
    buffer[dataCount].bikeId = bikeId;
    buffer[dataCount].timestamp = time(nullptr);
    buffer[dataCount].size = length;
    buffer[dataCount].crc32 = checksum;
    buffer[dataCount].uploaded = false;
    buffer[dataCount].confirmed = false;
    memcpy(buffer[dataCount].data, data, length);
    dataCount++;

    Serial.printf("📦 Data added: %s [%d bytes, CRC:%08X]\n", bikeId.c_str(), length, checksum);

    // Auto-save periodicamente
    if (dataCount % 5 == 0)
    {
        saveBuffer();
    }

    return true;
}

bool BufferManager::getDataForUpload(DynamicJsonDocument &doc)
{
    if (dataCount == 0)
    {
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

        // Convert hex data back to readable JSON
        String hexData = "";
        for (size_t j = 0; j < buffer[i].size; j++)
        {
            char hex[3];
            sprintf(hex, "%02X", buffer[i].data[j]);
            hexData += hex;
        }

        // Try to decode hex back to JSON for readability
        String decodedData = "";
        for (size_t j = 0; j < hexData.length(); j += 2)
        {
            String byteString = hexData.substring(j, j + 2);
            char byte = strtol(byteString.c_str(), NULL, 16);
            decodedData += byte;
        }

        // Test if decoded data is valid JSON
        DynamicJsonDocument testDoc(BIKE_HEARTBEAT_BUFFER);
        if (deserializeJson(testDoc, decodedData) == DeserializationError::Ok)
        {
            item["data_decoded"] = testDoc;
            Serial.printf("📋 Decoded data for %s: %s\n", buffer[i].bikeId.c_str(), decodedData.c_str());
        }
        else
        {
            item["data"] = hexData; // Fallback to hex if not JSON
        }

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
    for (int i = 0; i < dataCount; i++)
    {
        buffer[i].uploaded = false;
    }
    Serial.println("⚠️ Upload failed - data marked as pending");
}

void BufferManager::loadBuffer()
{
    if (!LittleFS.exists(BUFFER_FILE))
    {
        dataCount = 0;
        lastSync = 0;
        return;
    }

    File file = LittleFS.open(BUFFER_FILE, "r");
    if (!file)
        return;

    DynamicJsonDocument doc(BUFFER_PERSISTENCE_BUFFER);
    if (deserializeJson(doc, file) != DeserializationError::Ok)
    {
        file.close();
        return;
    }
    file.close();

    dataCount = doc["data_count"] | 0;
    lastSync = doc["last_sync"] | 0;

    JsonArray dataArray = doc["buffer"];
    int loadedCount = 0;

    for (JsonObject item : dataArray)
    {
        if (loadedCount >= MAX_BUFFER_SIZE)
            break;

        buffer[loadedCount].bikeId = item["bike_id"] | "unknown";
        buffer[loadedCount].timestamp = item["ts"];
        buffer[loadedCount].size = item["size"];
        buffer[loadedCount].crc32 = strtoul(item["crc32"] | "0", NULL, 16);
        buffer[loadedCount].uploaded = item["uploaded"] | false;
        buffer[loadedCount].confirmed = item["confirmed"] | false;

        String hexData = item["data"];
        size_t dataSize = hexData.length() / 2;

        for (size_t i = 0; i < dataSize && i < 256; i++)
        {
            String byteString = hexData.substring(i * 2, i * 2 + 2);
            buffer[loadedCount].data[i] = strtol(byteString.c_str(), NULL, 16);
        }

        loadedCount++;
    }

    dataCount = loadedCount;
}

void BufferManager::saveBuffer()
{
    DynamicJsonDocument doc(BUFFER_PERSISTENCE_BUFFER);

    doc["data_count"] = dataCount;
    doc["last_sync"] = lastSync;

    JsonArray dataArray = doc.createNestedArray("buffer");
    for (int i = 0; i < dataCount; i++)
    {
        JsonObject item = dataArray.createNestedObject();
        item["bike_id"] = buffer[i].bikeId;
        item["ts"] = buffer[i].timestamp;
        item["size"] = buffer[i].size;
        item["crc32"] = String(buffer[i].crc32, HEX);
        item["uploaded"] = buffer[i].uploaded;
        item["confirmed"] = buffer[i].confirmed;

        String hexData = "";
        for (size_t j = 0; j < buffer[i].size; j++)
        {
            char hex[3];
            sprintf(hex, "%02X", buffer[i].data[j]);
            hexData += hex;
        }
        item["data"] = hexData;
    }

    File file = LittleFS.open(BUFFER_FILE, "w");
    if (file)
    {
        serializeJson(doc, file);
        file.close();
    }
}

void BufferManager::createBackup()
{
    if (dataCount == 0)
        return;

    char backupFile[64];
    sprintf(backupFile, "/backup_%lu.json", time(nullptr));

    File source = LittleFS.open(BUFFER_FILE, "r");
    File backup = LittleFS.open(backupFile, "w");

    if (source && backup)
    {
        while (source.available())
        {
            backup.write(source.read());
        }
        Serial.printf("💾 Backup created: %s\n", backupFile);
    }

    if (source)
        source.close();
    if (backup)
        backup.close();
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
    int threshold = (MAX_BUFFER_SIZE * BUFFER_SYNC_THRESHOLD_PERCENT) / 100;
    return dataCount >= threshold;
}

bool BufferManager::hasData()
{
    return dataCount > 0;
}

int BufferManager::getDataCount()
{
    return dataCount;
}

int BufferManager::getPendingCount()
{
    int pending = 0;
    for (int i = 0; i < dataCount; i++)
    {
        if (!buffer[i].uploaded)
            pending++;
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