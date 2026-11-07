#include "buffer_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_system.h>

// N03: caminhos canônicos da sessão — tmp (cópia nova), bak (cópía anterior
// íntegra durante a troca), corrupt (quarentena)
static const char* kSessionPath = "/session.bin";
static const char* kSessionTmpPath = "/session.tmp";
static const char* kSessionBakPath = "/session.bak";
static const char* kSessionCorruptPath = "/session.corrupt";

// R22: CRC-32 (refletido, poly 0xEDB88320) para integridade do payload da sessão
static uint32_t crc32Bytes(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(int32_t)(crc & 1)));
        }
    }
    return crc ^ 0xFFFFFFFF;
}

BufferManager::BufferManager() : sessionActive(false), bootId(0),
                                 batterySaturationLogged(false) {
    memset(&currentSession, 0, sizeof(SessionData));
}

void BufferManager::startSession(const char* bikeId) {
    memset(&currentSession, 0, sizeof(SessionData));
    strncpy(currentSession.bike_id, bikeId, sizeof(currentSession.bike_id) - 1);
    currentSession.bike_id[sizeof(currentSession.bike_id) - 1] = '\0';
    currentSession.session_start_millis = millis();
    currentSession.session_salt = (uint16_t)(esp_random() & 0xFFFF);
    currentSession.boot_id = bootId;
    currentSession.scan_count = 0;
    currentSession.battery_count = 0;
    sessionActive = true;
    batterySaturationLogged = false;

    Serial.printf("📊 Session started: %s at %lu (salt %04x, boot %04x)\n",
                  bikeId, currentSession.session_start_millis,
                  currentSession.session_salt, currentSession.boot_id);
}

void BufferManager::endSession() {
    if (sessionActive) {
        sessionActive = false;
        Serial.printf("📊 Session ended: %d scans, %d battery readings\n",
                      currentSession.scan_count, currentSession.battery_count);
    }
}

void BufferManager::beginBoot() {
    // R16: identidade de boot único por energização; sessões retomadas
    // preservam o boot_id gravado no arquivo
    bootId = (uint16_t)(esp_random() & 0xFFFF);
    if (bootId == 0) bootId = 1;
}

bool BufferManager::addScan(uint32_t timestamp, const NetworkData* networks, uint8_t count) {
    if (!sessionActive || currentSession.scan_count >= MAX_SCANS) {
        // R17: saturação é explícita e diagnosticável, não silenciosa
        Serial.println("⚠️ Scan dropped: session inactive or buffer full (saturation)");
        return false;
    }

    ScanData& scan = currentSession.scans[currentSession.scan_count];
    scan.timestamp_millis = timestamp;
    scan.network_count = min(count, (uint8_t)MAX_NETWORKS_PER_SCAN);

    for (int i = 0; i < scan.network_count; i++) {
        memcpy(&scan.networks[i], &networks[i], sizeof(NetworkData));
    }

    currentSession.scan_count++;
    Serial.printf("📡 Added scan %d with %d networks\n", currentSession.scan_count, scan.network_count);
    return true;
}

void BufferManager::addBattery(uint32_t timestamp, uint8_t percent) {
    if (!sessionActive || currentSession.battery_count >= MAX_BATTERY) {
        // N02: telemetria de bateria satura explicitamente — NUNCA encerra a
        // capacidade de coleta de scans da sessão
        if (sessionActive && !batterySaturationLogged) {
            batterySaturationLogged = true;
            Serial.printf("⚠️ Battery telemetry saturated at %d readings (collection continues)\n",
                          MAX_BATTERY);
        }
        return;
    }

    BatteryData& battery = currentSession.battery[currentSession.battery_count];
    battery.timestamp_millis = timestamp;
    battery.percent = percent;

    currentSession.battery_count++;
}

bool BufferManager::save() {
    // F08: persistir sempre que houver dados, mesmo com sessão já encerrada
    if (currentSession.scan_count == 0 && currentSession.battery_count == 0) {
        return true;
    }

    // R04: gravação recuperável — escreve em temporário, valida e só então
    // substitui o arquivo canônico. Falha preserva a última versão íntegra.
    const char* tmpPath = "/session.tmp";
    File file = LittleFS.open(tmpPath, "w");
    if (!file) {
        Serial.println("❌ Failed to open session temp file");
        return false;
    }

    SessionFileHeader header;
    header.magic = SESSION_FILE_MAGIC;
    header.version = SESSION_FILE_VERSION;
    header.payload_size = sizeof(SessionData);
    header.payload_crc = crc32Bytes((uint8_t*)&currentSession, sizeof(SessionData));

    bool ok = file.write((uint8_t*)&header, sizeof(header)) == sizeof(header);
    if (ok) {
        ok = file.write((uint8_t*)&currentSession, sizeof(SessionData)) == sizeof(SessionData);
    }
    file.close();

    if (!ok) {
        Serial.println("❌ Session save failed (short write) - previous file kept");
        LittleFS.remove(tmpPath);
        return false;
    }

    // N03: instalação por rename DIRETO — substitui o canônico atomicamente.
    // Se a semântica do FS exigir destino inexistente, o fallback preserva o
    // íntegro como .bak ANTES de instalar a nova cópia (a janela antiga
    // remove-then-rename perdia o arquivo se faltasse energia entre as duas).
    if (!LittleFS.rename(tmpPath, kSessionPath)) {
        LittleFS.remove(kSessionBakPath);
        if (LittleFS.exists(kSessionPath) &&
            !LittleFS.rename(kSessionPath, kSessionBakPath)) {
            Serial.println("❌ Session backup move failed - previous file kept");
            LittleFS.remove(tmpPath);
            return false;
        }
        if (!LittleFS.rename(tmpPath, kSessionPath)) {
            Serial.println("❌ Session rename failed - restoring previous file");
            LittleFS.rename(kSessionBakPath, kSessionPath); // restaura o íntegro
            LittleFS.remove(tmpPath);
            return false;
        }
        LittleFS.remove(kSessionBakPath);
    }

    Serial.printf("✅ Session saved (%d scans, %d bytes, crc %08x)\n",
                  currentSession.scan_count, sizeof(SessionData), header.payload_crc);
    return true;
}

bool BufferManager::load() {
    // N03: se o canônico sumiu na janela de troca, recupera do temporário
    // (cópia mais nova) ou do backup (cópía anterior íntegra) antes de desistir
    if (!LittleFS.exists(kSessionPath)) {
        if (LittleFS.exists(kSessionTmpPath) &&
            LittleFS.rename(kSessionTmpPath, kSessionPath)) {
            Serial.println("🔁 Session recovered from staging copy");
        } else if (LittleFS.exists(kSessionBakPath) &&
                   LittleFS.rename(kSessionBakPath, kSessionPath)) {
            Serial.println("🔁 Session recovered from backup copy");
        }
    }

    if (!LittleFS.exists(kSessionPath) &&
        !LittleFS.exists(kSessionTmpPath) &&
        !LittleFS.exists(kSessionBakPath)) {
        Serial.println("📦 No saved session found");
        return false;
    }

    // N01: nenhuma candidata automática de 54 KB — a leitura vai direto para
    // currentSession (BSS global); falha de validação quarentena o arquivo
    if (tryLoadFrom(kSessionPath)) return true;
    if (LittleFS.exists(kSessionTmpPath) && tryLoadFrom(kSessionTmpPath)) return true;
    if (LittleFS.exists(kSessionBakPath) && tryLoadFrom(kSessionBakPath)) return true;

    Serial.println("📦 No valid session recovered");
    return false;
}

bool BufferManager::tryLoadFrom(const char* path) {
    File file = LittleFS.open(path, "r");
    if (!file) {
        return false;
    }

    // F17/R22: valida magic, versão, tamanho e CRC do payload antes de usar
    SessionFileHeader header;
    size_t bytesRead = file.readBytes((char*)&header, sizeof(header));
    if (bytesRead != sizeof(header) ||
        header.magic != SESSION_FILE_MAGIC ||
        header.version != SESSION_FILE_VERSION ||
        header.payload_size != sizeof(SessionData)) {
        Serial.printf("❌ Session file invalid (header mismatch): %s - quarantined\n", path);
        file.close();
        quarantineFile(path);
        return false;
    }

    bytesRead = file.readBytes((char*)&currentSession, sizeof(SessionData));
    file.close();

    if (bytesRead != sizeof(SessionData) ||
        crc32Bytes((uint8_t*)&currentSession, sizeof(SessionData)) != header.payload_crc) {
        Serial.printf("❌ Session load failed (payload CRC mismatch): %s - quarantined\n", path);
        quarantineFile(path);
        return false;
    }

    if (currentSession.scan_count > MAX_SCANS ||
        currentSession.battery_count > MAX_BATTERY ||
        currentSession.bike_id[sizeof(currentSession.bike_id) - 1] != '\0') {
        Serial.printf("❌ Session load failed (payload invalid): %s - quarantined\n", path);
        quarantineFile(path);
        return false;
    }

    // R22: validação estrutural completa — terminadores de strings e contagens
    for (uint16_t i = 0; i < currentSession.scan_count; i++) {
        const ScanData& scan = currentSession.scans[i];
        if (scan.network_count > MAX_NETWORKS_PER_SCAN) {
            Serial.printf("❌ Session load failed (network_count out of range): %s\n", path);
            quarantineFile(path);
            return false;
        }
        for (uint8_t n = 0; n < scan.network_count; n++) {
            const NetworkData& net = scan.networks[n];
            if (net.ssid[sizeof(net.ssid) - 1] != '\0' ||
                net.bssid[sizeof(net.bssid) - 1] != '\0') {
                Serial.printf("❌ Session load failed (unterminated ssid/bssid): %s\n", path);
                quarantineFile(path);
                return false;
            }
        }
    }

    // N04: bootId do MEMBRO permanece o da energização atual (beginBoot);
    // currentSession.boot_id registra a origem da sessão — serializeFragment
    // publica "resumed" quando diferem, sinalizando sessão retomada entre boots
    sessionActive = true;
    Serial.printf("✅ Session recovered: %s with %d scans (boot %04x, current boot %04x)\n",
                  currentSession.bike_id, currentSession.scan_count,
                  currentSession.boot_id, bootId);
    return true;
}

void BufferManager::quarantineFile(const char* path) {
    // R22/X05: preserva a cópia suspeita para análise em vez de apagar
    memset(&currentSession, 0, sizeof(SessionData));
    sessionActive = false;
    LittleFS.remove(kSessionCorruptPath);
    if (LittleFS.exists(path)) {
        LittleFS.rename(path, kSessionCorruptPath);
    }
}

void BufferManager::clear() {
    memset(&currentSession, 0, sizeof(SessionData));
    sessionActive = false;
    batterySaturationLogged = false;
    LittleFS.remove(kSessionPath);
    // N03: cópias de troca de uma sessão confirmada não têm valor
    LittleFS.remove(kSessionTmpPath);
    LittleFS.remove(kSessionBakPath);
    Serial.println("🗑️ Session buffer cleared (only after central ACK)");
}

uint32_t BufferManager::getSessionId() const {
    return currentSession.session_start_millis;
}

uint16_t BufferManager::getSessionSalt() const {
    return currentSession.session_salt;
}

uint16_t BufferManager::getBootId() const {
    return currentSession.boot_id;
}

// --- Fragmentação (F04) -------------------------------------------------
// Chunk = subconjunto de redes de um scan que cabe numa mensagem.
// R16: identidade do fragmento é o ÍNDICE do scan na sessão, não o timestamp
// (timestamps de boots diferentes podem colidir/recuar).

size_t BufferManager::measureNetworkBytes(const NetworkData& net) const {
    // ["ssid","aa:bb:cc:dd:ee:ff",-100,255],
    // O01: conta escapes JSON (aspas, barra e caracteres de controle)
    size_t ssidBytes = 0;
    for (size_t i = 0; net.ssid[i] != '\0' && i < sizeof(net.ssid); i++) {
        char c = net.ssid[i];
        if (c == '"' || c == '\\' || (unsigned char)c < 0x20) ssidBytes += 2;
        else ssidBytes += 1;
    }
    return ssidBytes + 34;
}

void BufferManager::buildChunks(std::vector<FragChunk>& chunks) const {
    // N04: 200 B cobre o envelope real com pior caso de dígitos + o campo
    // "idx" (identidade do scan) transmitido em cada fragmento de dados
    static const size_t HEADER_BYTES = 200;

    for (uint16_t s = 0; s < currentSession.scan_count; s++) {
        const ScanData& scan = currentSession.scans[s];
        uint8_t start = 0;
        while (start < scan.network_count) {
            size_t used = HEADER_BYTES;
            uint8_t count = 0;
            while (start + count < scan.network_count) {
                size_t netBytes = measureNetworkBytes(scan.networks[start + count]);
                if (used + netBytes > MAX_BLE_FRAGMENT_BYTES && count > 0) break;
                if (netBytes > MAX_BLE_FRAGMENT_BYTES - HEADER_BYTES) {
                    count = 1; // rede anômala sozinha; guardado pelo limite final
                    break;
                }
                used += netBytes;
                count++;
            }
            if (count == 0) count = 1; // segurança
            chunks.push_back({s, start, count});
            start += count;
        }
    }
}

uint16_t BufferManager::fragmentCount() const {
    if (currentSession.scan_count == 0 && currentSession.battery_count == 0) {
        return 0;
    }

    std::vector<FragChunk> chunks;
    buildChunks(chunks);

    // +1: fragmento final (bateria + resumo da sessão) marca a conclusão
    return (uint16_t)chunks.size() + 1;
}

bool BufferManager::serializeFragment(uint16_t index, String& out) const {
    uint16_t total = fragmentCount();
    if (total == 0 || index >= total) {
        return false;
    }

    // X06: a arena da ÁRVORE interna precisa comportar todos os slots do pior
    // caso (7 redes por chunk / 10 baterias no final) — não confundir com o
    // orçamento do payload serializado (MAX_BLE_FRAGMENT_BYTES)
    DynamicJsonDocument doc(FRAGMENT_JSON_ARENA);
    doc["type"] = "data";
    doc["bike_id"] = currentSession.bike_id;
    doc["session_id"] = currentSession.session_start_millis;
    doc["session_salt"] = currentSession.session_salt;
    doc["boot_id"] = currentSession.boot_id;
    doc["seq"] = index;
    doc["total"] = total;

    if (index == total - 1) {
        // Fragmento final: bateria + resumo da sessão
        doc["is_final"] = true;
        doc["session_start_millis"] = currentSession.session_start_millis;
        doc["scan_count"] = currentSession.scan_count;
        // N04: sessão retomada entre boots — dados de origens temporais
        // diferentes sob o mesmo rótulo; consumidor decide como tratar
        doc["resumed"] = (currentSession.boot_id != bootId);

        JsonArray battery = doc.createNestedArray("battery");
        for (int i = 0; i < currentSession.battery_count; i++) {
            JsonArray batArray = battery.createNestedArray();
            batArray.add(currentSession.battery[i].timestamp_millis);
            batArray.add(currentSession.battery[i].percent);
        }
    } else {
        // Fragmento de dados: um subconjunto de redes de um scan
        doc["is_final"] = false;

        std::vector<FragChunk> chunks;
        buildChunks(chunks);
        if (index >= chunks.size()) {
            return false;
        }
        const FragChunk& chunk = chunks[index];

        // R16: acesso direto por índice do scan (sem busca por timestamp)
        const ScanData& scan = currentSession.scans[chunk.scan_idx];

        JsonArray scans = doc.createNestedArray("scans");
        JsonObject scanObj = scans.createNestedObject();
        // N04: identidade da observação transmitida — o consumidor remonta os
        // chunks de um mesmo scan por (idx) em vez de adivinhar por timestamp
        scanObj["idx"] = chunk.scan_idx;
        scanObj["ts"] = scan.timestamp_millis;
        scanObj["net_offset"] = chunk.net_start;

        JsonArray networks = scanObj.createNestedArray("nets");
        for (uint8_t i = 0; i < chunk.net_count; i++) {
            const NetworkData& net = scan.networks[chunk.net_start + i];
            JsonArray netArray = networks.createNestedArray();
            netArray.add(net.ssid);
            netArray.add(net.bssid);
            netArray.add(net.rssi);
            netArray.add(net.channel);
        }
    }

    if (doc.overflowed()) {
        Serial.println("❌ Fragment serialization overflow");
        return false;
    }

    serializeJson(doc, out);
    // O01: 512 é o limite físico GATT; 460 é o orçamento lógico do fragmento
    if (out.length() > 512) {
        Serial.printf("❌ Fragment %d exceeds BLE limit: %d bytes\n", index, out.length());
        return false;
    }
    return true;
}

// ------------------------------------------------------------------------

bool BufferManager::isEmpty() const {
    return !sessionActive || (currentSession.scan_count == 0 && currentSession.battery_count == 0);
}

bool BufferManager::isFull() const {
    // N02: capacidade da sessão é regida pelos SCANS — telemetria de bateria
    // satura em separado (addBattery) e não encerra a coleta prematuramente
    return sessionActive && currentSession.scan_count >= MAX_SCANS;
}

size_t BufferManager::getSize() const {
    return sizeof(SessionData);
}

size_t BufferManager::getPendingDataBytes() const {
    // N02: estimativa da carga útil real pendente (não a capacidade da struct)
    size_t bytes = 60; // envelope do fragmento final
    for (uint16_t s = 0; s < currentSession.scan_count; s++) {
        const ScanData& scan = currentSession.scans[s];
        bytes += 40; // envelope por scan
        for (uint8_t n = 0; n < scan.network_count; n++) {
            bytes += measureNetworkBytes(scan.networks[n]);
        }
    }
    bytes += (size_t)currentSession.battery_count * 16;
    return bytes;
}
