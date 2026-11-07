#include "buffer_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include "batch_proto.h"

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
    // T12 (PRD lotes/recibos): a memória do último lote limpo sobrevive à
    // abertura de sessão nova — "recebido e apaguei" pode ser repetido
    uint32_t clearedSid = currentSession.last_cleared_sid;
    uint16_t clearedSalt = currentSession.last_cleared_salt;
    uint16_t clearedTotal = currentSession.last_cleared_total;
    uint8_t clearedValid = currentSession.last_cleared_valid;
    uint8_t clearedSha[32];
    memcpy(clearedSha, currentSession.last_cleared_sha, 32);

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

    currentSession.last_cleared_sid = clearedSid;
    currentSession.last_cleared_salt = clearedSalt;
    currentSession.last_cleared_total = clearedTotal;
    currentSession.last_cleared_valid = clearedValid;
    memcpy(currentSession.last_cleared_sha, clearedSha, 32);

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
    // F08: persistir sempre que houver dados, mesmo com sessão já encerrada.
    // T12 (PRD lotes): sessão vazia com memória de lote limpo TAMBÉM persiste
    // — o "recebido e apaguei" precisa poder ser repetido após reboot.
    if (currentSession.scan_count == 0 && currentSession.battery_count == 0 &&
        !currentSession.last_cleared_valid) {
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
    // T12: a memória do último lote limpo sobrevive à limpeza total
    uint32_t clearedSid = currentSession.last_cleared_sid;
    uint16_t clearedSalt = currentSession.last_cleared_salt;
    uint16_t clearedTotal = currentSession.last_cleared_total;
    uint8_t clearedValid = currentSession.last_cleared_valid;
    uint8_t clearedSha[32];
    memcpy(clearedSha, currentSession.last_cleared_sha, 32);

    memset(&currentSession, 0, sizeof(SessionData));
    sessionActive = false;
    batterySaturationLogged = false;

    currentSession.last_cleared_sid = clearedSid;
    currentSession.last_cleared_salt = clearedSalt;
    currentSession.last_cleared_total = clearedTotal;
    currentSession.last_cleared_valid = clearedValid;
    memcpy(currentSession.last_cleared_sha, clearedSha, 32);

    LittleFS.remove(kSessionPath);
    // N03: cópias de troca de uma sessão confirmada não têm valor
    LittleFS.remove(kSessionTmpPath);
    LittleFS.remove(kSessionBakPath);
    Serial.println("🗑️ Session buffer cleared (only after central ACK)");
}

void BufferManager::resetBatchFields() {
    currentSession.batch_frozen = 0;
    currentSession.batch_scan_count = 0;
    currentSession.batch_battery_count = 0;
    currentSession.batch_total_parts = 0;
    currentSession.batch_total_bytes = 0;
    memset(currentSession.batch_sha256, 0, 32);
    currentSession.batch_authorized = 0;
    currentSession.batch_resumed = 0;
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
    // PRD lotes: quando há lote congelado, os chunks são do SUBCONJUNTO
    // congelado — novas coletas ficam fora e não alteram as partes
    buildChunksForRange(chunks, currentSession.batch_frozen
        ? currentSession.batch_scan_count
        : currentSession.scan_count);
}

void BufferManager::buildChunksForRange(std::vector<FragChunk>& chunks, uint16_t scanLimit) const {
    // N04: 200 B cobre o envelope real com pior caso de dígitos + o campo
    // "idx" (identidade do scan) transmitido em cada fragmento de dados
    static const size_t HEADER_BYTES = 200;

    for (uint16_t s = 0; s < scanLimit; s++) {
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
    // PRD lotes: total de partes é FIXADO no fechamento do lote
    if (currentSession.batch_frozen) {
        return currentSession.batch_total_parts;
    }
    if (currentSession.scan_count == 0 && currentSession.battery_count == 0) {
        return 0;
    }

    std::vector<FragChunk> chunks;
    buildChunks(chunks);

    // +1: fragmento final (bateria + resumo da sessão) marca a conclusão
    return (uint16_t)chunks.size() + 1;
}

// Serialização com limites explícitos (lote congelado ou sessão atual) —
// o freeze e os reenvios usam ESTA função, garantindo bytes idênticos.
// `resumedFixed`: quando há lote congelado, o valor de 'resumed' é o
// CONGELADO no fechamento — derivar do boot atual mudaria os bytes da parte
// após reboot e invalidaria o hash do lote.
bool BufferManager::serializePartWithLimits(uint16_t index, uint16_t total,
                                            uint16_t scanLimit, uint16_t batteryLimit,
                                            bool resumedFixed, String& out) const {
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
        doc["scan_count"] = scanLimit;
        // N04: sessão retomada entre boots — VALOR CONGELADO no lote
        // (bytes estáveis entre reenvios/reboots — contrato BTP1)
        doc["resumed"] = resumedFixed;

        JsonArray battery = doc.createNestedArray("battery");
        for (int i = 0; i < batteryLimit; i++) {
            JsonArray batArray = battery.createNestedArray();
            batArray.add(currentSession.battery[i].timestamp_millis);
            batArray.add(currentSession.battery[i].percent);
        }
    } else {
        // Fragmento de dados: um subconjunto de redes de um scan
        doc["is_final"] = false;

        std::vector<FragChunk> chunks;
        buildChunksForRange(chunks, scanLimit);
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

bool BufferManager::serializeFragment(uint16_t index, String& out) const {
    if (currentSession.batch_frozen) {
        // Parte do lote congelado: limites, total e 'resumed' FIXADOS no
        // fechamento — bytes idênticos entre reenvios e reboots
        return serializePartWithLimits(index, currentSession.batch_total_parts,
                                       currentSession.batch_scan_count,
                                       currentSession.batch_battery_count,
                                       currentSession.batch_resumed != 0, out);
    }
    return serializePartWithLimits(index, fragmentCount(),
                                   currentSession.scan_count,
                                   currentSession.battery_count,
                                   currentSession.boot_id != bootId, out);
}

// ---- Lote de entrega (PRD lotes, integridade e recibos) ------------------

bool BufferManager::freezeBatch() {
    if (currentSession.batch_frozen) {
        return true; // idempotente: retry não cria outro lote (PRD 4.1)
    }
    if (!sessionActive ||
        (currentSession.scan_count == 0 && currentSession.battery_count == 0)) {
        return false;
    }

    // Identidade canônica exige bike_id de 12 bytes ("bpr-XXXXXX")
    if (strlen(currentSession.bike_id) != BATCH_PROTO_BIKE_ID_LEN) {
        Serial.printf("🚨 freezeBatch: bike_id '%s' fora do formato canônico — lote não fechado\n",
                      currentSession.bike_id);
        return false;
    }

    const uint16_t scanLimit = currentSession.scan_count;
    const uint16_t batteryLimit = currentSession.battery_count;
    // 'resumed' avaliado NO fechamento (estável nos bytes das partes)
    const bool resumedAtFreeze = (currentSession.boot_id != bootId);

    std::vector<FragChunk> chunks;
    buildChunksForRange(chunks, scanLimit);
    const uint16_t total = (uint16_t)chunks.size() + 1; // + final
    if (total == 0 || total > BATCH_PROTO_MAX_PARTS) {
        Serial.printf("🚨 freezeBatch: total %d fora do contrato — lote não fechado\n", total);
        return false;
    }

    // Serializa todas as partes (bytes determinísticos) e computa o hash
    // do lote sobre a representação canônica BTP1
    std::vector<String> parts((size_t)total);
    uint32_t totalBytes = 0;
    for (uint16_t i = 0; i < total; i++) {
        if (!serializePartWithLimits(i, total, scanLimit, batteryLimit,
                                     resumedAtFreeze, parts[i])) {
            Serial.printf("🚨 freezeBatch: parte %d não serializou — lote não fechado\n", i);
            return false;
        }
        totalBytes += parts[i].length();
    }

    batch_hash_input input;
    input.begin(currentSession.bike_id,
                currentSession.session_start_millis, currentSession.session_salt,
                total, totalBytes);
    for (uint16_t i = 0; i < total; i++) {
        input.part(i, (const uint8_t*)parts[i].c_str(),
                   (uint16_t)parts[i].length());
    }
    uint8_t sha[32];
    input.final(sha);

    // Congela o conjunto e persiste ANTES de transmitir (PRD 4.1)
    currentSession.batch_frozen = 1;
    currentSession.batch_scan_count = scanLimit;
    currentSession.batch_battery_count = batteryLimit;
    currentSession.batch_total_parts = total;
    currentSession.batch_total_bytes = totalBytes;
    memcpy(currentSession.batch_sha256, sha, 32);
    currentSession.batch_authorized = 0;
    currentSession.batch_resumed = resumedAtFreeze ? 1 : 0;

    char hex[65];
    batch_sha256_hex(sha, hex);
    Serial.printf("📦 Batch frozen: %d partes, %lu bytes, sha256 %s\n",
                  total, (unsigned long)totalBytes, hex);

    if (!save()) {
        // Sem persistência do freeze, o lote não pode ser anunciado
        resetBatchFields();
        Serial.println("🚨 freezeBatch: persistência falhou — lote não fechado");
        return false;
    }
    return true;
}

bool BufferManager::hasBatchToDeliver() const {
    return sessionActive && currentSession.batch_frozen &&
           currentSession.batch_authorized == 0 &&
           currentSession.batch_total_parts > 0;
}

void BufferManager::getBatchSha256Hex(char out[65]) const {
    batch_sha256_hex(currentSession.batch_sha256, out);
}

bool BufferManager::applyClearAuthorization(const char shaHex[65],
                                            uint16_t totalParts, uint32_t totalBytes) {
    if (!currentSession.batch_frozen || currentSession.batch_authorized) {
        return false;
    }
    if (totalParts != currentSession.batch_total_parts ||
        totalBytes != currentSession.batch_total_bytes) {
        Serial.printf("🚨 Clear auth mismatch: total %d/%d bytes %lu/%lu — ignorada\n",
                      totalParts, currentSession.batch_total_parts,
                      (unsigned long)totalBytes,
                      (unsigned long)currentSession.batch_total_bytes);
        return false;
    }
    char expected[65];
    batch_sha256_hex(currentSession.batch_sha256, expected);
    if (shaHex == nullptr || strlen(shaHex) != 64 ||
        strcasecmp(shaHex, expected) != 0) {
        Serial.println("🚨 Clear auth com hash divergente — ignorada");
        return false;
    }

    // PRD 5.2: a autorização é PERSISTIDA antes de remover qualquer registro
    currentSession.batch_authorized = 1;
    if (!save()) {
        currentSession.batch_authorized = 0;
        Serial.println("🚨 Autorização não persistiu — limpeza não executada");
        return false;
    }
    Serial.println("🔓 Limpeza autorizada e persistida (aguardando execução)");
    return true;
}

bool BufferManager::performClearIfAuthorized() {
    if (!currentSession.batch_frozen || !currentSession.batch_authorized) {
        return false;
    }

    const uint16_t removeScans = currentSession.batch_scan_count;
    const uint16_t removeBattery = currentSession.batch_battery_count;

    // T14: apenas os registros COBERTOS pelo lote são removidos — observações
    // coletadas durante/após a transferência permanecem (shift para o início)
    if (currentSession.scan_count > removeScans) {
        memmove(currentSession.scans, &currentSession.scans[removeScans],
                (size_t)(currentSession.scan_count - removeScans) * sizeof(ScanData));
    }
    currentSession.scan_count -= removeScans;

    if (currentSession.battery_count > removeBattery) {
        memmove(currentSession.battery, &currentSession.battery[removeBattery],
                (size_t)(currentSession.battery_count - removeBattery) * sizeof(BatteryData));
    }
    currentSession.battery_count -= removeBattery;

    // T12: memória do lote limpo — "recebido e apaguei" pode ser repetido
    currentSession.last_cleared_sid = currentSession.session_start_millis;
    currentSession.last_cleared_salt = currentSession.session_salt;
    currentSession.last_cleared_total = currentSession.batch_total_parts;
    memcpy(currentSession.last_cleared_sha, currentSession.batch_sha256, 32);
    currentSession.last_cleared_valid = 1;

    resetBatchFields();

    // Persiste o estado final (PRD 5.2) — vazio com memória também persiste
    if (currentSession.scan_count == 0 && currentSession.battery_count == 0) {
        if (!save()) {
            Serial.println("🚨 Estado final (vazio) não persistiu — memória T12 em risco");
        }
        sessionActive = false;
        // Cópias de troca não têm valor; o CANÔNICO permanece quando carrega
        // a memória do lote limpo (T12: responder "apaguei" após reboot)
        LittleFS.remove(kSessionTmpPath);
        LittleFS.remove(kSessionBakPath);
        if (!currentSession.last_cleared_valid) {
            LittleFS.remove(kSessionPath);
        }
    } else {
        if (!save()) {
            Serial.println("🚨 Estado final não persistiu — próxima entrega repetirá o lote");
        }
    }

    Serial.printf("🗑️ Lote limpo: %d scans, %d baterias removidos; restam %d scans\n",
                  removeScans, removeBattery, currentSession.scan_count);
    return true;
}

bool BufferManager::isBatchCleared(uint32_t sid, uint16_t salt, const char shaHex[65]) const {
    if (!currentSession.last_cleared_valid) return false;
    if (sid != currentSession.last_cleared_sid || salt != currentSession.last_cleared_salt) {
        return false;
    }
    char expected[65];
    batch_sha256_hex(currentSession.last_cleared_sha, expected);
    return shaHex != nullptr && strlen(shaHex) == 64 &&
           strcasecmp(shaHex, expected) == 0;
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
