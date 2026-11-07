#include <ArduinoJson.h>
#include <CRC32.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include "bike_manager.h"
#include "binary_structs.h"
#include "batch_proto.h"
#include "batch_spool.h"
#include "ble_server.h"
#include "bpr_json_helper.h"
#include "buffer_manager.h"
#include "cloud_sync.h"
#include "config_credentials.h"
#include "config_manager.h"
#include "constants.h"
#include "endpoints.h"
#include "time_sync.h"

extern ConfigManager configManager;
extern ConfigCredentials configCredentials;
extern BufferManager bufferManager;

// Static members
bool CloudSync::syncInProgress = false;
SyncResult CloudSync::currentResult = SyncResult::SUCCESS;
static uint32_t syncStartTime = 0;

// O02: nunca loga a URL com credencial — só o caminho
static String redactUrl(const String& url)
{
    int cut = url.indexOf("?auth=");
    return (cut > 0) ? url.substring(0, cut) : url;
}

// PRD lotes/recibos (definida ao final): upload dos lotes BTP1 + conferência
// das confirmações remotas; falha remota não derruba a sync (T09)
static void uploadBatchSpools();

// Patch de horários (definida ao final): publica os registros de viagem
static void uploadTripRecords();

SyncResult CloudSync::enter()
{
    Serial.println("📡 Entering CLOUD_SYNC mode");
    syncStartTime = millis();

    syncInProgress = true;
    currentResult = SyncResult::IN_PROGRESS;

    return currentResult;
}

// F14: reinicia a operação de sync explicitamente (changeState no-op não reinicia)
void CloudSync::retry()
{
    if (!syncInProgress)
    {
        Serial.println("🔄 CloudSync retry armed");
        syncInProgress = true;
        currentResult = SyncResult::IN_PROGRESS;
    }
}

SyncResult CloudSync::update()
{
    if (!syncInProgress)
    {
        return currentResult;
    }

    // WiFi connection PRIMEIRO
    if (!connectWiFi())
    {
        Serial.println("❌ WiFi connection failed");
        WiFi.disconnect(true);
        syncInProgress = false;
        currentResult = SyncResult::FAILURE;
        return currentResult;
    }

    // F20: resultado do NTP é verificado; sem relógio os timestamps saem como 0/flag
    bool timeOk = TimeSync::init();
    if (!timeOk)
    {
        Serial.println("⚠️ NTP indisponível - dados serão carimbados com flag timestamp_valid=false");
    }

    // UPLOADS (dados locais -> Firebase)
    if (!uploadBufferData())
    {
        WiFi.disconnect(true);
        syncInProgress = false;
        currentResult = SyncResult::FAILURE;
        Serial.println("❌ Sync failed");
        return currentResult;
    }

    // PRD lotes/recibos: upload dos lotes BTP1 (progressivo) e conferência
    // das confirmações remotas — falha remota NÃO falha a sync inteira (os
    // lotes ficam aguardando nuvem; a bike mantém a cópia — T09)
    uploadBatchSpools();

    // Patch de horários: publica os registros de viagem (eventos/ref/sessões)
    // — fatos imutáveis por event_id; o PUT é idempotente e o local sai só
    // após confirmação remota
    uploadTripRecords();

    // X10: heartbeat é OBSERVABILIDADE — falha dele não pode bloquear os
    // downloads de configuração/registry que corrigiriam o próprio sistema
    uploadHeartbeat();

    // R15: descobertas locais (pending) publicadas para aprovação remota
    uploadPendingBikes();

    // DOWNLOADS (Firebase -> local)
    if (!downloadCentralConfig())
    {
        WiFi.disconnect(true);
        syncInProgress = false;
        currentResult = SyncResult::FAILURE;
        Serial.println("❌ Sync failed");
        return currentResult;
    }

    if (!downloadBikeRegistryData())
    {
        WiFi.disconnect(true);
        syncInProgress = false;
        currentResult = SyncResult::FAILURE;
        Serial.println("❌ Sync failed");
        return currentResult;
    }

    if (!downloadBikeConfigs())
    {
        WiFi.disconnect(true);
        syncInProgress = false;
        currentResult = SyncResult::FAILURE;
        Serial.println("❌ Sync failed");
        return currentResult;
    }

    // Marcar que o primeiro sync foi concluído
    if (configCredentials.isFirstSync())
    {
        configCredentials.setFirstSyncCompleted();
        configCredentials.saveCredentials();
        Serial.println("✅ First sync completed - flag updated");
    }

    syncInProgress = false;
    currentResult = SyncResult::SUCCESS;
    Serial.println("✅ Sync complete");
    return currentResult;
}

void CloudSync::exit()
{
    WiFi.disconnect(true);

    // Voltar BLE para status READY
    BPRBLEServer::setBusyStatus(false);

    Serial.println("🔚 Exiting CLOUD_SYNC mode - BLE back to READY");
}

void CloudSync::printStatus()
{
    Serial.printf("😲 Bikes conectadas: %d | 💾 Heap: %d bytes\n", BikeManager::getConnectedCount(), ESP.getFreeHeap());
}

bool CloudSync::connectWiFi()
{
    const CredentialsConfig &creds = configCredentials.getCredentials();
    const CentralConfig &config = configManager.getConfig();

    WiFi.mode(WIFI_STA);
    WiFi.begin(creds.wifi_ssid, creds.wifi_password);

    uint32_t startTime = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        if (millis() - startTime > config.timeouts.wifi_sec * 1000)
        {
            return false;
        }
        delay(500);
        Serial.print(".");
    }

    Serial.printf("\n📶 WiFi connected: %s\n", WiFi.localIP().toString().c_str());
    return true;
}

// F20/R20: retorna a versão remota para o chamador guardar (não a hora local).
// Corpo "null" (chave inexistente no RTDB) NÃO é versão zero — é ausência.
static bool fetchRemoteVersion(const String &url, uint32_t &remoteVersion)
{
    HTTPClient http;
    http.begin(url);
    Endpoints::setupHttp(http); // X10
    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK)
    {
        http.end();
        return false;
    }

    String response = http.getString();
    http.end();

    response.trim();
    if (response.length() == 0 || response.equalsIgnoreCase("null"))
    {
        return false; // versão ausente, não zero
    }

    char* endPtr = nullptr;
    unsigned long parsed = strtoul(response.c_str(), &endPtr, 10);
    if (endPtr == response.c_str() || *endPtr != '\0')
    {
        return false; // corpo não-numérico não é versão
    }

    remoteVersion = (uint32_t)parsed;
    return true;
}

bool CloudSync::checkLastUpdateTime(const String &url, uint32_t localLastUpdate,
                                    const String &componentName, uint32_t *remoteOut)
{
    uint32_t remoteLastUpdate = 0;
    if (!fetchRemoteVersion(url, remoteLastUpdate))
    {
        // R20: versão ausente/inconsultável — política: baixa por segurança,
        // exceto quando não há config local (primeiro provisionamento)
        Serial.printf("⚠️ %s version unavailable - downloading for safety\n", componentName.c_str());
        if (remoteOut) *remoteOut = 0;
        return true;
    }

    // R20: sem config local, versão remota qualquer é mais nova (provisionamento)
    bool needsUpdate = (remoteLastUpdate > localLastUpdate) || (localLastUpdate == 0);

    Serial.printf("📋 %s version check: local=%lu, remote=%lu -> %s\n",
                  componentName.c_str(), (unsigned long)localLastUpdate,
                  (unsigned long)remoteLastUpdate, needsUpdate ? "UPDATE NEEDED" : "UP TO DATE");

    if (remoteOut) *remoteOut = remoteLastUpdate;
    return needsUpdate;
}

bool CloudSync::needsConfigUpdate(uint32_t *remoteVersion)
{
    const CentralConfig &config = configManager.getConfig();
    return checkLastUpdateTime(Endpoints::getConfigVersion(), config.last_update, "Config", remoteVersion);
}

bool CloudSync::needsBikeRegistryUpdate(uint32_t *remoteVersion)
{
    if (!LittleFS.exists(BIKE_REGISTRY_FILE))
    {
        Serial.println("📋 No local bike registry - needs download");
        if (remoteVersion) *remoteVersion = 0;
        return true;
    }

    File file = LittleFS.open(BIKE_REGISTRY_FILE, "r");
    if (!file)
    {
        return true;
    }

    BikeRegistryData registry;
    size_t bytesRead = file.readBytes((char*)&registry, sizeof(registry));
    file.close();

    if (bytesRead != sizeof(registry))
    {
        return true;
    }

    return checkLastUpdateTime(Endpoints::getBikeRegistryVersion(), registry.last_update, "Bike registry", remoteVersion);
}

void CloudSync::updateConfigFromFirebase(const String &csvData, uint32_t remoteVersion)
{
    Serial.println("🔄 Updating config from Firebase CSV...");

    if (configManager.updateFromCSV(csvData, remoteVersion))
    {
        Serial.println("✅ Config updated from Firebase CSV and saved locally");
    }
    else
    {
        // R20: aplicação falhou (CSV inválido, sem versão ou sem persistência) —
        // o sync NÃO reporta sucesso de configuração
        Serial.println("❌ Failed to update config from Firebase CSV");
    }
}

bool CloudSync::downloadCentralConfig()
{
    uint32_t remoteVersion = 0;

    // Verificar se precisa atualizar antes de baixar
    if (!needsConfigUpdate(&remoteVersion))
    {
        Serial.println("📋 Config already up to date - skipping download");
        return true;
    }

    HTTPClient http;
    String url = Endpoints::getCentralConfig();

    Serial.println("🔄 Downloading central config from Firebase...");

    http.begin(url);
    Endpoints::setupHttp(http); // X10
    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK)
    {
        Serial.printf("🚨 Central config download failed: HTTP %d (%s)\n",
                      httpCode, redactUrl(url).c_str());
        http.end();
        return false;
    }

    String jsonArray = http.getString();
    http.end();

    // R20: corpo "null" = config inexistente na nuvem — erro de contrato,
    // não "sucesso com valores vazios"
    jsonArray.trim();
    if (jsonArray.length() == 0 || jsonArray.equalsIgnoreCase("null"))
    {
        Serial.println("🚨 Central config absent in cloud (null) - contract error");
        return false;
    }

    // R20: sem versão remota válida a configuração não é aplicada
    // (não existe mais fallback para millis())
    if (remoteVersion == 0)
    {
        Serial.println("🚨 Central config without remote version - not applying");
        return false;
    }

    // Converter JSON array para CSV: [90,60,15,...] -> 90,60,15,...
    jsonArray.replace("[", "");
    jsonArray.replace("]", "");

    updateConfigFromFirebase(jsonArray, remoteVersion);

    // R20: downloadCentralConfig só sucede se a configuração foi aplicada E salva
    if (configManager.getConfig().last_update != remoteVersion)
    {
        return false;
    }

    Serial.printf("✅ Central config downloaded (version %lu)\n", (unsigned long)remoteVersion);
    Serial.printf("   Sync interval: %d seconds\n", configManager.getConfig().intervals.sync_sec);
    return true;
}

bool CloudSync::downloadBikeRegistryData()
{
    uint32_t remoteVersion = 0;

    if (!needsBikeRegistryUpdate(&remoteVersion))
    {
        Serial.println("🚲 Bike registry already up to date - skipping download");
        // F03: merge também no caminho "atualizado" (reboot precisa aplicar o
        // arquivo local). Seção 6: falha de aplicação é EXPOSTA — sem isso a
        // autorização local ficava dessincronizada do arquivo sem sinal
        if (!BikeManager::mergeFromRegistry())
        {
            Serial.println("❌ Local registry not applied (list full?) - sync fails, will retry");
            return false;
        }
        return true;
    }

    HTTPClient http;
    String url = Endpoints::getBikeRegistry();

    Serial.println("🔄 Downloading bike registry from Firebase...");

    http.begin(url);
    Endpoints::setupHttp(http); // X10
    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK)
    {
        Serial.printf("❌ Bike registry download failed: HTTP %d\n", httpCode);
        http.end();
        return false;
    }

    String csvData = http.getString();
    http.end();

    BikeRegistryData registry;
    memset(&registry, 0, sizeof(registry));

    // F20: versão remota como last_update (era hora local do download)
    registry.last_update = remoteVersion;

    // Parse JSON array: ["bpr-XXXXXX",2,"AA:BB:CC:DD:EE:01",ts1,ts2, ...]
    csvData.replace("[", "");
    csvData.replace("]", "");
    csvData.replace("\"", "");
    csvData.replace(" ", "");

    int bikeIndex = 0;
    int startPos = 0;
    int fieldIndex = 0;

    while (startPos < (int)csvData.length() && bikeIndex < 10) {
        int commaPos = csvData.indexOf(',', startPos);
        String value = (commaPos == -1) ? csvData.substring(startPos) : csvData.substring(startPos, commaPos);

        switch (fieldIndex % 5) {
            case 0: // bike_id (formato bpr-XXXXXX hex)
                registry.bikes[bikeIndex] = bikeIdToInt(value);
                break;
            case 1: // status
                registry.statuses[bikeIndex] = value.toInt();
                break;
            case 2: // mac_address
                registry.mac_addresses[bikeIndex] = macStringToInt(value);
                break;
            case 3: // created_at
                registry.created_at[bikeIndex] = value.toInt();
                break;
            case 4: // last_seen
                registry.last_seen[bikeIndex] = value.toInt();
                bikeIndex++;
                break;
        }

        if (commaPos == -1) break;
        startPos = commaPos + 1;
        fieldIndex++;
    }

    registry.bike_count = bikeIndex;

    File file = LittleFS.open(BIKE_REGISTRY_FILE, "w");
    if (file)
    {
        size_t written = file.write((uint8_t*)&registry, sizeof(registry));
        file.close();
        if (written != sizeof(registry))
        {
            Serial.println("❌ Bike registry write failed");
            LittleFS.remove(BIKE_REGISTRY_FILE);
            return false;
        }
        Serial.println("💾 Bike registry saved locally");
    }

    // F03: cadastro baixado alimenta a autorização operacional imediatamente.
    // Seção 6 da revalidação: o retorno do merge é VERIFICADO — lista cheia
    // sem pendente evictável não aplica a autorização baixada, e afirmar
    // "downloaded" esconderia a falha (troca de frota ficava sem efeito)
    if (!BikeManager::mergeFromRegistry())
    {
        Serial.println("❌ Registry downloaded but NOT applied (list full?) - sync fails, will retry");
        return false;
    }

    Serial.printf("✅ Bike registry downloaded: %d bikes\n", bikeIndex);
    return true;
}

bool CloudSync::downloadBikeConfigs()
{
    Serial.println("🔄 Downloading bike configs individually...");

    // F03/F13: lista REAL de bikes autorizadas (registry já mesclado acima)
    if (!BikeManager::isLoaded())
    {
        // primeiro sync ocorre antes do pairing: carrega status/configs/registry agora
        BikeManager::init();
    }

    std::vector<String> allowedBikes = BikeManager::getAllowedBikeIds();
    if (allowedBikes.empty())
    {
        Serial.println("📝 No allowed bikes in registry - skipping config download");
        return true;
    }

    int successCount = 0;
    for (const String& bikeId : allowedBikes)
    {
        if (BikeManager::downloadSingleBikeConfig(bikeId)) {
            successCount++;
        }
        delay(100);
    }

    Serial.printf("✅ Downloaded/checked configs for %d/%d allowed bikes\n",
                  successCount, (int)allowedBikes.size());
    return true; // sem configs para uma bike não derruba o sync inteiro
}

// F19/R06: a identidade do lote é a chave estável calculada pelo BufferManager
// a partir da identidade dos itens — retries produzem a MESMA chave
bool CloudSync::uploadBufferData()
{
    int totalBatches = 0;
    const int maxBatches = MAX_TOTAL_ITEMS + 8; // guard contra loop se persistência falhar

    // F18/R05: sobe em lotes; cada lote é confirmado individualmente pela
    // identidade exata do seu manifesto
    while (totalBatches < maxBatches)
    {
        // X10: cede CPU entre lotes — o loop de upload é longo e o C3 é
        // single-core (WiFi/BLE/timers precisam respirar)
        yield();

        DynamicJsonDocument doc(BUFFER_PERSISTENCE_BUFFER);
        String batchKey;
        int items = bufferManager.prepareUploadBatch(doc, UPLOAD_BATCH_MAX_ITEMS, batchKey);
        if (items == 0)
        {
            break;
        }

        String jsonString;
        serializeJson(doc, jsonString);

        String url = Endpoints::getBufferDataBatch(batchKey);

        HTTPClient http;
        http.begin(url);
        Endpoints::setupHttp(http); // X10
        http.addHeader("Content-Type", "application/json");

        int httpCode = http.PATCH(jsonString);
        http.end();

        if (httpCode != HTTP_CODE_OK)
        {
            Serial.printf("❌ Buffer upload failed: HTTP %d (batch %s)\n", httpCode, batchKey.c_str());
            Serial.printf("   URL: %s\n", redactUrl(url).c_str());
            // R05: devolve exatamente os itens deste lote ao estado pendente
            bufferManager.rollbackUpload(batchKey);
            return false;
        }

        Serial.printf("📤 Batch %s uploaded: %d items, %d bytes\n",
                      batchKey.c_str(), items, jsonString.length());

        // R05: confirmação por lote — remove somente os itens entregues
        if (!bufferManager.markAsConfirmed(batchKey))
        {
            // Confirm falhou (ex.: flash cheio): lote já subiu; manifesto
            // permanece e a reconciliação do boot devolve os itens como
            // pendentes — a nuvem deduplica pela chave estável
            Serial.println("⚠️ Batch delivered but local confirm failed - will reconcile at boot");
            return false;
        }
        totalBatches++;
    }

    if (totalBatches == 0)
    {
        Serial.println("📝 No buffer data to upload");
        return true;
    }

    Serial.printf("📤 Upload complete: %d batches\n", totalBatches);
    return true;
}

// R15: publica bikes pendentes descobertas localmente para gestão remota
bool CloudSync::uploadPendingBikes()
{
    DynamicJsonDocument doc(1024);
    if (!BikeManager::getPendingBikesForUpload(doc))
    {
        return true; // sem pendentes não é erro
    }

    String jsonString;
    serializeJson(doc, jsonString);

    String url = Endpoints::getPendingBikes();
    HTTPClient http;
    http.begin(url);
    Endpoints::setupHttp(http); // X10
    http.addHeader("Content-Type", "application/json");
    int httpCode = http.PATCH(jsonString);
    http.end();

    if (httpCode != HTTP_CODE_OK)
    {
        Serial.printf("⚠️ Pending bikes upload failed: HTTP %d (%s)\n",
                      httpCode, redactUrl(url).c_str());
        return false; // não derruba o sync; próxima rodada reenvia (idempotente)
    }

    Serial.println("📤 Pending bikes published for remote approval");
    return true;
}

bool CloudSync::uploadHeartbeat()
{
    HTTPClient http;

    String url = Endpoints::getHeartbeat();

    time_t now = time(nullptr);
    if (now < 1000000000L) now = 0; // F20: sem NTP, não inventa Unix time

    DynamicJsonDocument doc(HEARTBEAT_BUFFER);
    doc["timestamp"] = now;
    doc["timestamp_valid"] = (now > 0);
    doc["bikes_connected"] = BikeManager::getConnectedCount();
    doc["heap"] = ESP.getFreeHeap();
    doc["uptime"] = millis() / 1000;

    http.begin(url);
    Endpoints::setupHttp(http); // X10
    http.addHeader("Content-Type", "application/json");

    String jsonString;
    serializeJson(doc, jsonString);

    int httpCode = http.PUT(jsonString);
    http.end();

    bool success = (httpCode == HTTP_CODE_OK);

    if (success)
    {
        Serial.printf("💓 Heartbeat: Bikes: %d | Heap: %d\n",
                      BikeManager::getConnectedCount(), ESP.getFreeHeap());
    }
    else
    {
        Serial.printf("❌ Heartbeat falhou: HTTP %d (%s)\n", httpCode, redactUrl(url).c_str());
    }

    return success;
}

// ---- PRD lotes/recibos (BTP1): upload dos lotes e conferência remota -----
// Ordem das gravações (PRD 5.2): confirmação remota → persistir RECIBO →
// remover payload local → (a autorização à bike vem pelo recibo quando ela
// anunciar de novo). Falha remota mantém o estado aguardando nuvem (T09) —
// a bike conserva a própria cópia até a autorização durável.
static void uploadBatchSpools()
{
    // 1) Upload progressivo: lotes completos localmente (complete_local) com
    // partes ainda não enviadas — retomável entre syncs/reboots (T07)
    std::vector<BatchSpoolHeader> ready = BatchSpool::listReadyForUpload();
    for (const BatchSpoolHeader &h : ready)
    {
        String bikeId(h.bike_id);
        int sent = 0;
        bool failed = false;

        for (uint16_t i = 0; i < h.total_parts && sent < MAX_PART_UPLOADS_PER_SYNC; i++)
        {
            if (h.uploaded_bitmap[i / 8] & (1u << (i % 8))) continue;

            BatchPartRecord rec;
            if (!BatchSpool::readPartForUpload(h, i, rec))
            {
                Serial.printf("🚨 Falha lendo parte %d do lote %lu-%d\n",
                              i, (unsigned long)h.session_id, h.session_salt);
                failed = true;
                break;
            }

            // A parte vai como STRING: o RTDB preserva os bytes UTF-8 exatos
            // e a validação na nuvem recomputa CRC/SHA do contrato (T17)
            DynamicJsonDocument doc(BIKE_DATA_BUFFER + 64);
            doc["len"] = rec.len;
            char crcHex[9];
            snprintf(crcHex, sizeof(crcHex), "%08x", rec.crc32);
            doc["crc32"] = crcHex;
            doc["payload"] = String((const char *)rec.data, rec.len);

            String body;
            serializeJson(doc, body);

            String url = Endpoints::getBatchPartUrl(bikeId, h.session_id, h.session_salt, i);
            HTTPClient http;
            http.begin(url);
            Endpoints::setupHttp(http); // X10
            http.addHeader("Content-Type", "application/json");
            int code = http.PUT(body);
            http.end();

            if (code == HTTP_CODE_OK)
            {
                BatchSpool::markUploaded(bikeId, h.session_id, h.session_salt, i);
                sent++;
            }
            else
            {
                Serial.printf("⚠️ Upload da parte %d falhou (HTTP %d) - continuará no próximo sync\n",
                              i, code);
                failed = true;
            }
            yield(); // X10: loop longo no C3 single-core
            if (failed) break;
        }

        if (failed || sent > 0) {
            Serial.printf("☁️ Lote %lu-%d: %d partes enviadas neste ciclo\n",
                          (unsigned long)h.session_id, h.session_salt, sent);
        }

        // Todas as partes enviadas? publica o meta — o gatilho na nuvem valida
        // o conjunto inteiro (CRC por parte + hash global) e instala a
        // conclusão canônica; um pedido isolado NÃO representa o todo (PRD §3)
        BatchSpoolHeader now;
        if (!failed && BatchSpool::find(bikeId, h.session_id, h.session_salt, now))
        {
            bool all = true;
            for (uint16_t i = 0; i < now.total_parts && all; i++) {
                if ((now.uploaded_bitmap[i / 8] & (1u << (i % 8))) == 0) all = false;
            }
            if (all)
            {
                char shaHex[65];
                batch_sha256_hex(now.sha256, shaHex);
                DynamicJsonDocument meta(320);
                meta["v"] = BATCH_PROTO_VERSION;
                meta["total"] = now.total_parts;
                meta["total_bytes"] = now.total_bytes;
                meta["sha256"] = shaHex;
                meta["uploaded_by"] = configCredentials.getBaseId(); // proveniência
                String body;
                serializeJson(meta, body);

                String url = Endpoints::getBatchMetaUrl(bikeId, h.session_id, h.session_salt);
                HTTPClient http;
                http.begin(url);
                Endpoints::setupHttp(http);
                http.addHeader("Content-Type", "application/json");
                int code = http.PUT(body);
                http.end();
                if (code == HTTP_CODE_OK)
                {
                    BatchSpool::markAwaitingCloud(bikeId, h.session_id, h.session_salt);
                    Serial.printf("☁️ Meta do lote %lu-%d publicado - aguardando confirmação\n",
                                  (unsigned long)h.session_id, h.session_salt);
                }
                else
                {
                    Serial.printf("⚠️ Meta do lote %lu-%d falhou (HTTP %d) - reenviará\n",
                                  (unsigned long)h.session_id, h.session_salt, code);
                }
            }
        }
    }

    // 2) Conferir confirmações remotas dos lotes aguardando nuvem — o recibo
    // canônico por ORIGEM independe da estação que fez o upload (T10/T18)
    std::vector<BatchSpoolHeader> waiting = BatchSpool::listAwaitingCloud();
    for (const BatchSpoolHeader &h : waiting)
    {
        String bikeId(h.bike_id);
        String url = Endpoints::getBatchConfirmationUrl(bikeId, h.session_id, h.session_salt);
        HTTPClient http;
        http.begin(url);
        Endpoints::setupHttp(http);
        int code = http.GET();
        if (code != HTTP_CODE_OK)
        {
            // T09: sem confirmação acessível = pendência; nada é inferido
            continue;
        }
        String body = http.getString();
        http.end();

        DynamicJsonDocument doc(512);
        if (deserializeJson(doc, body) != DeserializationError::Ok) continue;

        String state = doc["state"] | "";
        String shaHex = doc["sha256"] | "";
        char want[65];
        batch_sha256_hex(h.sha256, want);

        if (state == "confirmed" && shaHex == want)
        {
            // PRD 5.2: recibo ANTES de remover o payload; o recibo sobrevive
            // à remoção e autoriza a limpeza repetidamente (T11)
            uint32_t confirmedAt = doc["confirmed_at"] | 0;
            if (BatchSpool::writeReceipt(h, confirmedAt))
            {
                BatchSpool::removeSpool(bikeId, h.session_id, h.session_salt);
                Serial.printf("🧾 Lote %lu-%d confirmado na nuvem - recibo local, payload liberado\n",
                              (unsigned long)h.session_id, h.session_salt);
            }
        }
        else if (state == "confirmed")
        {
            Serial.printf("🚨 Confirmação com hash divergente para lote %lu-%d - mantido aguardando\n",
                          (unsigned long)h.session_id, h.session_salt);
        }
    }
}

// ---- Patch de horários: publica registros de viagem na nuvem ------------
// A central acumulou /trips/{bike}_{seq}.json (eventos imutáveis por
// event_id + referências + sessões). A nuvem é o registro canônico por
// bike+trip; o arquivo local sai somente após o PUT confirmar.
static void uploadTripRecords()
{
    if (!LittleFS.exists("/trips")) return;
    File root = LittleFS.open("/trips");
    if (!root) return;

    std::vector<String> files;
    File f = root.openNextFile();
    while (f) {
        String name = f.name();
        f.close();
        if (name.endsWith(".json") && !name.endsWith(".tmp")) {
            files.push_back(name);
        }
        f = root.openNextFile();
    }
    root.close();

    for (const String& name : files) {
        // nome: {bike}_{trip}.json
        String base = name.substring(0, name.length() - 5);
        int cut = base.lastIndexOf('_');
        if (cut <= 4) continue;
        String bikeId = base.substring(0, cut);
        String tripSeq = base.substring(cut + 1);

        String path = "/trips/" + name;
        File in = LittleFS.open(path, "r");
        if (!in) continue;
        String body = in.readString();
        in.close();

        String url = String(configCredentials.getCredentials().firebase_database_url) +
            "/bikes/" + bikeId + "/trips/" + tripSeq + "/trip.json" +
            "?auth=" + configCredentials.getCredentials().firebase_api_key;
        HTTPClient http;
        http.begin(url);
        Endpoints::setupHttp(http); // X10
        http.addHeader("Content-Type", "application/json");
        int code = http.PUT(body);
        http.end();

        if (code == HTTP_CODE_OK) {
            LittleFS.remove(path);
            Serial.printf("🧭 Viagem %s/%s publicada na nuvem\n",
                          bikeId.c_str(), tripSeq.c_str());
        } else {
            Serial.printf("⚠️ Publicação da viagem %s/%s falhou (HTTP %d)\n",
                          bikeId.c_str(), tripSeq.c_str(), code);
        }
        yield();
    }
}
