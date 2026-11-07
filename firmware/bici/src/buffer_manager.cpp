#include "buffer_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_system.h>

BufferManager::BufferManager() : sessionActive(false) {
    memset(&currentSession, 0, sizeof(SessionData));
}

void BufferManager::startSession(const char* bikeId) {
    memset(&currentSession, 0, sizeof(SessionData));
    strncpy(currentSession.bike_id, bikeId, sizeof(currentSession.bike_id) - 1);
    currentSession.bike_id[sizeof(currentSession.bike_id) - 1] = '\0';
    currentSession.session_start_millis = millis();
    currentSession.session_salt = (uint16_t)(esp_random() & 0xFFFF);
    currentSession.scan_count = 0;
    currentSession.battery_count = 0;
    sessionActive = true;

    Serial.printf("📊 Session started: %s at %lu (salt %04x)\n",
                  bikeId, currentSession.session_start_millis, currentSession.session_salt);
}

void BufferManager::endSession() {
    if (sessionActive) {
        sessionActive = false;
        Serial.printf("📊 Session ended: %d scans, %d battery readings\n",
                      currentSession.scan_count, currentSession.battery_count);
    }
}

void BufferManager::addScan(uint32_t timestamp, const NetworkData* networks, uint8_t count) {
    if (!sessionActive || currentSession.scan_count >= MAX_SCANS) {
        Serial.println("⚠️ Cannot add scan: session inactive or buffer full");
        return;
    }

    ScanData& scan = currentSession.scans[currentSession.scan_count];
    scan.timestamp_millis = timestamp;
    scan.network_count = min(count, (uint8_t)MAX_NETWORKS_PER_SCAN);

    for (int i = 0; i < scan.network_count; i++) {
        memcpy(&scan.networks[i], &networks[i], sizeof(NetworkData));
    }

    currentSession.scan_count++;
    Serial.printf("📡 Added scan %d with %d networks\n", currentSession.scan_count, scan.network_count);
}

void BufferManager::addBattery(uint32_t timestamp, uint8_t percent) {
    if (!sessionActive || currentSession.battery_count >= MAX_BATTERY) {
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

    File file = LittleFS.open("/session.bin", "w");
    if (!file) {
        Serial.println("❌ Failed to open session file");
        return false;
    }

    SessionFileHeader header;
    header.magic = SESSION_FILE_MAGIC;
    header.version = SESSION_FILE_VERSION;
    header.payload_size = sizeof(SessionData);

    size_t written = file.write((uint8_t*)&header, sizeof(header));
    if (written == sizeof(header)) {
        written = file.write((uint8_t*)&currentSession, sizeof(SessionData));
    }
    file.close();

    if (written != sizeof(SessionData)) {
        Serial.printf("❌ Session save failed: %d/%d bytes\n", written, sizeof(SessionData));
        LittleFS.remove("/session.bin");
        return false;
    }

    Serial.printf("✅ Session saved (%d scans, %d bytes)\n",
                  currentSession.scan_count, sizeof(SessionData));
    return true;
}

bool BufferManager::load() {
    if (!LittleFS.exists("/session.bin")) {
        Serial.println("📦 No saved session found");
        return false;
    }

    File file = LittleFS.open("/session.bin", "r");
    if (!file) {
        return false;
    }

    // F17: valida magic, versão e tamanho exato antes de tocar na struct
    SessionFileHeader header;
    size_t bytesRead = file.readBytes((char*)&header, sizeof(header));
    if (bytesRead != sizeof(header) ||
        header.magic != SESSION_FILE_MAGIC ||
        header.version != SESSION_FILE_VERSION ||
        header.payload_size != sizeof(SessionData)) {
        Serial.println("❌ Session file invalid (header mismatch) - discarding");
        file.close();
        LittleFS.remove("/session.bin");
        return false;
    }

    bytesRead = file.readBytes((char*)&currentSession, sizeof(SessionData));
    file.close();

    if (bytesRead != sizeof(SessionData) ||
        currentSession.scan_count > MAX_SCANS ||
        currentSession.battery_count > MAX_BATTERY ||
        currentSession.bike_id[sizeof(currentSession.bike_id) - 1] != '\0') {
        Serial.println("❌ Session load failed (payload invalid) - discarding");
        memset(&currentSession, 0, sizeof(SessionData));
        sessionActive = false;
        LittleFS.remove("/session.bin");
        return false;
    }

    for (uint16_t i = 0; i < currentSession.scan_count; i++) {
        if (currentSession.scans[i].network_count > MAX_NETWORKS_PER_SCAN) {
            Serial.println("❌ Session load failed (network_count out of range)");
            memset(&currentSession, 0, sizeof(SessionData));
            sessionActive = false;
            LittleFS.remove("/session.bin");
            return false;
        }
    }

    sessionActive = true;
    Serial.printf("✅ Session recovered: %s with %d scans\n",
                  currentSession.bike_id, currentSession.scan_count);
    return true;
}

void BufferManager::clear() {
    memset(&currentSession, 0, sizeof(SessionData));
    sessionActive = false;
    LittleFS.remove("/session.bin");
    Serial.println("🗑️ Session buffer cleared (only after central ACK)");
}

uint32_t BufferManager::getSessionId() const {
    return currentSession.session_start_millis;
}

uint16_t BufferManager::getSessionSalt() const {
    return currentSession.session_salt;
}

// --- Fragmentação (F04) -------------------------------------------------
// Chunk = subconjunto de redes de um scan que cabe numa mensagem.
// Cabeçalho estimado: envelope (~140) + objeto do scan (~40) + margem (10)

size_t BufferManager::measureNetworkBytes(const NetworkData& net) const {
    // ["ssid","aa:bb:cc:dd:ee:ff",-100,255],
    return strlen(net.ssid) + 34;
}

void BufferManager::buildChunks(std::vector<FragChunk>& chunks) const {
    static const size_t HEADER_BYTES = 190;

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
            chunks.push_back({scan.timestamp_millis, start, count});
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

    DynamicJsonDocument doc(MAX_BLE_FRAGMENT_BYTES + 192);
    doc["type"] = "data";
    doc["bike_id"] = currentSession.bike_id;
    doc["session_id"] = currentSession.session_start_millis;
    doc["session_salt"] = currentSession.session_salt;
    doc["seq"] = index;
    doc["total"] = total;

    if (index == total - 1) {
        // Fragmento final: bateria + resumo da sessão
        doc["is_final"] = true;
        doc["session_start_millis"] = currentSession.session_start_millis;
        doc["scan_count"] = currentSession.scan_count;

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

        // localiza o scan dono do timestamp
        const ScanData* scan = nullptr;
        for (uint16_t s = 0; s < currentSession.scan_count; s++) {
            if (currentSession.scans[s].timestamp_millis == chunk.scan_ts) {
                scan = &currentSession.scans[s];
                break;
            }
        }
        if (!scan) return false;

        JsonArray scans = doc.createNestedArray("scans");
        JsonObject scanObj = scans.createNestedObject();
        scanObj["ts"] = chunk.scan_ts;
        scanObj["net_offset"] = chunk.net_start;

        JsonArray networks = scanObj.createNestedArray("nets");
        for (uint8_t i = 0; i < chunk.net_count; i++) {
            const NetworkData& net = scan->networks[chunk.net_start + i];
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
    return sessionActive && (currentSession.scan_count >= MAX_SCANS || currentSession.battery_count >= MAX_BATTERY);
}

size_t BufferManager::getSize() const {
    return sizeof(SessionData);
}
