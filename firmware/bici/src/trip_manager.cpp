#include "trip_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "batch_proto.h"

static const char* kTripPath = "/trip.bin";
static const char* kTripTmpPath = "/trip.tmp";
static const char* kTripBakPath = "/trip.bak";

TripManager::TripManager() {
    memset(&trip, 0, sizeof(trip));
}

bool TripManager::validPayload() const {
    if (trip.bike_id[sizeof(trip.bike_id) - 1] != '\0') return false;
    if (trip.ref_count > TRIP_MAX_REFS) return false;
    if (trip.session_count > TRIP_MAX_SESSIONS) return false;
    if (trip.state > TRIP_STATE_ARRIVED) return false;
    return true;
}

bool TripManager::load() {
    // Padrão N03: recuperação de cópias de troca
    if (!LittleFS.exists(kTripPath)) {
        if (LittleFS.exists(kTripTmpPath) && LittleFS.rename(kTripTmpPath, kTripPath)) {
            Serial.println("🔁 Trip recovered from staging copy");
        } else if (LittleFS.exists(kTripBakPath) && LittleFS.rename(kTripBakPath, kTripPath)) {
            Serial.println("🔁 Trip recovered from backup copy");
        }
    }
    if (!LittleFS.exists(kTripPath)) return false;

    File f = LittleFS.open(kTripPath, "r");
    if (!f) return false;
    TripFileHeader h;
    if (f.readBytes((char*)&h, sizeof(h)) != sizeof(h) ||
        h.magic != TRIP_FILE_MAGIC || h.version != TRIP_FILE_VERSION ||
        h.payload_size != sizeof(TripStateData)) {
        f.close();
        Serial.println("❌ Trip file invalid - ignored");
        return false;
    }
    bool ok = f.readBytes((char*)&trip, sizeof(trip)) == sizeof(trip);
    f.close();
    if (!ok || batch_crc32((const uint8_t*)&trip, sizeof(trip)) != h.payload_crc ||
        !validPayload()) {
        memset(&trip, 0, sizeof(trip));
        Serial.println("❌ Trip payload invalid - ignored");
        return false;
    }
    return true;
}

void TripManager::setBikeId(const char* bikeId) {
    if (strncmp(trip.bike_id, bikeId, sizeof(trip.bike_id) - 1) != 0) {
        strncpy(trip.bike_id, bikeId, sizeof(trip.bike_id) - 1);
        trip.bike_id[sizeof(trip.bike_id) - 1] = '\0';
        save();
    }
}

bool TripManager::save() {    File f = LittleFS.open(kTripTmpPath, "w");
    if (!f) return false;
    TripFileHeader h;
    h.magic = TRIP_FILE_MAGIC;
    h.version = TRIP_FILE_VERSION;
    h.payload_size = sizeof(TripStateData);
    h.payload_crc = batch_crc32((const uint8_t*)&trip, sizeof(trip));
    bool ok = f.write((uint8_t*)&h, sizeof(h)) == sizeof(h);
    if (ok) ok = f.write((uint8_t*)&trip, sizeof(trip)) == sizeof(trip);
    f.close();
    if (!ok) {
        LittleFS.remove(kTripTmpPath);
        return false;
    }
    if (!LittleFS.rename(kTripTmpPath, kTripPath)) {
        LittleFS.remove(kTripBakPath);
        if (LittleFS.exists(kTripPath) && !LittleFS.rename(kTripPath, kTripBakPath)) {
            LittleFS.remove(kTripTmpPath);
            return false;
        }
        if (!LittleFS.rename(kTripTmpPath, kTripPath)) {
            LittleFS.rename(kTripBakPath, kTripPath);
            LittleFS.remove(kTripTmpPath);
            return false;
        }
        LittleFS.remove(kTripBakPath);
    }
    return true;
}

void TripManager::onBaseContact(const char* baseId, uint32_t civilMs,
                                uint8_t civilValid, uint32_t counterMs,
                                uint16_t bootId) {
    if (trip.state == TRIP_STATE_DEPARTURE_PENDING) {
        // §2.1/H11: recontato dentro da tolerância — a bike nunca saiu de
        // fato; a saída pendente é CANCELADA (não existe viagem espúria)
        Serial.println("ℹ️ Recontato antes da tolerância - saída pendente cancelada");
    }
    if (trip.state != TRIP_STATE_ACTIVE) {
        if (trip.state == TRIP_STATE_NONE || trip.state == TRIP_STATE_DEPARTURE_PENDING ||
            trip.state == TRIP_STATE_IN_BASE) {
            trip.state = TRIP_STATE_IN_BASE; // heartbeats não abrem viagem (H05)
        }
        // Atualiza a referência candidata de saída (último contato válido),
        // inclusive em ARRIVED — a próxima saída usa a referência desta visita
        strncpy(trip.pending_base_id, baseId, sizeof(trip.pending_base_id) - 1);
        trip.pending_base_id[sizeof(trip.pending_base_id) - 1] = '\0';
        trip.pending_civil_s = civilMs;
        trip.pending_civil_valid = civilValid;
        trip.pending_counter_ms = counterMs;
        trip.pending_boot_id = bootId;
    }
    // Em viagem ativa o contato é CHEGADA (tratado em onTimeRef, que carrega
    // a hora civil do evento); em ARRIVED o contato apenas atualiza
    // sincronização (a chegada é imutável — §2.2)
    trip.last_contact_ms = millis();
    save();
}

void TripManager::noteLeftBase() {
    if (trip.state == TRIP_STATE_IN_BASE) {
        trip.state = TRIP_STATE_DEPARTURE_PENDING;
        Serial.println("🚪 Ausência da base em observação (saída pendente)");
        save();
    } else if (trip.state == TRIP_STATE_ARRIVED) {
        // §2.3/H13: novo ciclo — o anterior permanece identificado; a
        // viagem antiga só é arquivada localmente quando seus eventos foram
        // entregues (a evidência segue na central/nuvem). Pendente de
        // entrega: o ciclo fica BLOQUEADO (nada é sobrescrito)
        if (trip.departure_sent && (!trip.arrival.valid || trip.arrival_sent)) {
            advanceTrip();
            // A ausência já começou: o novo ciclo abre em observação com a
            // referência do último contato desta visita
            trip.state = TRIP_STATE_DEPARTURE_PENDING;
            Serial.printf("🆕 Novo ciclo de viagem: trip %lu\n",
                          (unsigned long)trip.trip_seq);
            save();
        } else {
            Serial.println("⚠️ Saída com eventos da viagem anterior ainda não " +
                           String("entregues - novo ciclo bloqueado (dados preservados)"));
        }
    }
}

void TripManager::advanceTrip() {
    trip.trip_seq++;
    trip.state = TRIP_STATE_NONE;
    memset(&trip.departure, 0, sizeof(trip.departure));
    memset(&trip.arrival, 0, sizeof(trip.arrival));
    trip.ref_count = 0;
    trip.session_count = 0;
    trip.departure_sent = 0;
    trip.arrival_sent = 0;
    trip.sleep_recorded = 0;
    save();
}

void TripManager::poll(uint32_t nowMs) {
    if (trip.state != TRIP_STATE_DEPARTURE_PENDING) return;
    if (nowMs - trip.last_contact_ms < TRIP_DEPARTURE_CONFIRM_MS) return;
    confirmDeparture();
}

void TripManager::confirmDeparture() {
    if (!trip.pending_civil_valid || trip.pending_boot_id == 0) {
        // Sem referência civil/trecho não há evento de saída registrável —
        // pendência explícita (§2.1: qualidade sem inventar medição)
        Serial.println("⚠️ Saída sem referência civil válida - viagem sem hora de saída");
    }
    trip.departure.valid = 1;
    trip.departure.kind = TRIP_KIND_DEPARTURE;
    trip.departure.event_id = (trip.trip_seq << 8) | TRIP_KIND_DEPARTURE;
    strncpy(trip.departure.base_id, trip.pending_base_id,
            sizeof(trip.departure.base_id) - 1);
    trip.departure.base_id[sizeof(trip.departure.base_id) - 1] = '\0';
    trip.departure.civil_s = trip.pending_civil_s;
    trip.departure.civil_valid = trip.pending_civil_valid;
    trip.departure.counter_ms = trip.pending_counter_ms; // origem explícita (H03)
    trip.departure.boot_id = trip.pending_boot_id;
    trip.departure.method = TRIP_METHOD_LAST_BASE_CONTACT;
    trip.state = TRIP_STATE_ACTIVE;
    trip.departure_sent = 0;

    Serial.printf("🚪 Saída confirmada: trip %lu estação %s (civil %lu, contador %lu, trecho %04x)\n",
                  (unsigned long)trip.trip_seq, trip.departure.base_id,
                  (unsigned long)trip.departure.civil_s,
                  (unsigned long)trip.departure.counter_ms, trip.departure.boot_id);
    save();
}

bool TripManager::registerArrival(uint32_t civilMs, uint8_t civilValid,
                                  const char* baseId, uint32_t counterMs) {
    if (trip.state != TRIP_STATE_ACTIVE || trip.arrival.valid) return false;
    trip.arrival.valid = 1;
    trip.arrival.kind = TRIP_KIND_ARRIVAL;
    trip.arrival.event_id = (trip.trip_seq << 8) | TRIP_KIND_ARRIVAL;
    strncpy(trip.arrival.base_id, baseId, sizeof(trip.arrival.base_id) - 1);
    trip.arrival.base_id[sizeof(trip.arrival.base_id) - 1] = '\0';
    trip.arrival.civil_s = civilMs;
    trip.arrival.civil_valid = civilValid;
    trip.arrival.counter_ms = counterMs; // leitura atual, mesmo evento (H02)
    trip.arrival.boot_id = currentBoot;
    trip.arrival.method = TRIP_METHOD_FIRST_TIME_REF;
    trip.arrival_sent = 0;
    trip.state = TRIP_STATE_ARRIVED;

    Serial.printf("🏠 Chegada registrada: trip %lu estação %s (civil %lu, contador %lu, trecho %04x)\n",
                  (unsigned long)trip.trip_seq, trip.arrival.base_id,
                  (unsigned long)trip.arrival.civil_s,
                  (unsigned long)trip.arrival.counter_ms, trip.arrival.boot_id);
    save();
    return true;
}

void TripManager::storeRef(uint32_t refId, uint32_t civilMs, uint8_t civilValid) {
    // Uma referência por trecho: a do boot atual substitui a anterior do
    // MESMO trecho; trechos antigos permanecem (H09) enquanto houver lugar
    for (uint8_t i = 0; i < trip.ref_count; i++) {
        if (trip.refs[i].boot_id == currentBoot) {
            trip.refs[i].ref_id = refId;
            trip.refs[i].civil_s = civilMs;
            trip.refs[i].civil_valid = civilValid;
            trip.refs[i].counter_ms = millis();
            trip.refs[i].received_ms = millis();
            return;
        }
    }
    if (trip.ref_count >= TRIP_MAX_REFS) {
        // Preserva as mais recentes: descarta a mais antiga
        uint8_t oldest = 0;
        for (uint8_t i = 1; i < trip.ref_count; i++) {
            if (trip.refs[i].received_ms < trip.refs[oldest].received_ms) oldest = i;
        }
        for (uint8_t i = oldest; i < trip.ref_count - 1; i++) {
            trip.refs[i] = trip.refs[i + 1];
        }
        trip.ref_count--;
    }
    TimeRefRecord& r = trip.refs[trip.ref_count++];
    r.ref_id = refId;
    r.civil_s = civilMs;
    r.civil_valid = civilValid;
    r.counter_ms = millis();   // §5 passo 3: leitura local ao receber
    r.boot_id = currentBoot;
    r.received_ms = millis();
}

void TripManager::onTimeRef(uint32_t refId, uint32_t civilMs, uint8_t civilValid,
                            const char* baseId) {
    // §5: guarda a associação (contador local + hora civil + trecho)
    storeRef(refId, civilMs, civilValid);

    if (trip.state == TRIP_STATE_ACTIVE) {
        // §2.2: primeiro contato válido do retorno = chegada (hora da
        // estação + leitura atual, no MESMO evento); imutável depois
        registerArrival(civilMs, civilValid, baseId, millis());
    } else if (trip.state == TRIP_STATE_DEPARTURE_PENDING) {
        // Recontato: cancela a saída pendente (a bike estava na base)
        onBaseContact(baseId, civilMs, civilValid, millis(), currentBoot);
    } else {
        // Na base/arrived: apenas sincronização (chegada não é empurrada)
        trip.last_contact_ms = millis();
        save();
    }
}

void TripManager::onSessionStarted(uint32_t sid, uint16_t salt) {
    if (trip.session_count >= TRIP_MAX_SESSIONS) return; // diagnóstico por logStatus
    trip.session_ids[trip.session_count] = sid;
    trip.session_salts[trip.session_count] = salt;
    trip.session_count++;
    save();
}

void TripManager::notePlannedSleep(uint32_t seconds) {
    // §6: evidência de sleep SOLICITADO (não medição do tempo transcorrido)
    trip.last_planned_sleep_sec = seconds;
    trip.sleep_recorded = 1;
    save();
}

bool TripManager::hasUnsentEvents() const {
    return (trip.departure.valid && !trip.departure_sent) ||
           (trip.arrival.valid && !trip.arrival_sent);
}

void TripManager::markEventSent(uint8_t kind) {
    if (kind == TRIP_KIND_DEPARTURE) trip.departure_sent = 1;
    if (kind == TRIP_KIND_ARRIVAL) trip.arrival_sent = 1;
    save();
}

bool TripManager::serializeEventJson(uint8_t kind, String& out) const {
    const TripEventRecord* ev =
        kind == TRIP_KIND_DEPARTURE ? &trip.departure : &trip.arrival;
    if (!ev->valid) return false;

    DynamicJsonDocument doc(384);
    doc["type"] = "trip_event";
    doc["v"] = 1;
    doc["bike_id"] = trip.bike_id;
    doc["trip_seq"] = trip.trip_seq;
    doc["event_id"] = ev->event_id;
    doc["kind"] = kind == TRIP_KIND_DEPARTURE ? "departure" : "arrival";
    doc["base_id"] = ev->base_id;
    doc["civil_s"] = ev->civil_s;
    doc["civil_valid"] = ev->civil_valid != 0;
    doc["counter_ms"] = ev->counter_ms;
    doc["boot_id"] = ev->boot_id;
    doc["method"] = ev->method == TRIP_METHOD_LAST_BASE_CONTACT
        ? "last_base_contact" : "first_contact_time_ref";
    serializeJson(doc, out);
    return out.length() <= 512;
}

bool TripManager::serializeMetaJson(String& out) const {
    DynamicJsonDocument doc(1024);
    doc["type"] = "trip_meta";
    doc["v"] = 1;
    doc["bike_id"] = trip.bike_id;
    doc["trip_seq"] = trip.trip_seq;

    JsonArray sessions = doc.createNestedArray("sessions");
    for (uint8_t i = 0; i < trip.session_count; i++) {
        JsonObject s = sessions.createNestedObject();
        s["session_id"] = trip.session_ids[i];
        s["session_salt"] = trip.session_salts[i];
    }

    JsonArray refs = doc.createNestedArray("time_refs");
    for (uint8_t i = 0; i < trip.ref_count; i++) {
        const TimeRefRecord& r = trip.refs[i];
        JsonObject o = refs.createNestedObject();
        o["ref_id"] = r.ref_id;
        o["civil_s"] = r.civil_s;
        o["civil_valid"] = r.civil_valid != 0;
        o["counter_ms"] = r.counter_ms;
        o["boot_id"] = r.boot_id;
    }

    if (trip.sleep_recorded) {
        // Natureza identificada: solicitado, não medido (§6)
        doc["planned_sleep_sec"] = trip.last_planned_sleep_sec;
        doc["sleep_nature"] = "requested";
    }
    serializeJson(doc, out);
    return out.length() <= 512;
}

uint32_t TripManager::lastRefId() const {
    for (uint8_t i = 0; i < trip.ref_count; i++) {
        if (trip.refs[i].boot_id == currentBoot) return trip.refs[i].ref_id;
    }
    return 0;
}

bool TripManager::arrivalCivil(uint32_t& outMs) const {
    if (!trip.arrival.valid || !trip.arrival.civil_valid) return false;
    outMs = trip.arrival.civil_s;
    return true;
}

void TripManager::logStatus() const {
    const char* stateName =
        trip.state == TRIP_STATE_NONE ? "none" :
        trip.state == TRIP_STATE_IN_BASE ? "in_base" :
        trip.state == TRIP_STATE_DEPARTURE_PENDING ? "departure_pending" :
        trip.state == TRIP_STATE_ACTIVE ? "active" :
        trip.state == TRIP_STATE_ARRIVED ? "arrived" : "?";
    Serial.printf("🧭 Trip %lu: %s | saída %s/enviada %d | chegada %s/enviada %d | refs %d | sessões %d\n",
                  (unsigned long)trip.trip_seq, stateName,
                  trip.departure.valid ? "reg" : "-", trip.departure_sent,
                  trip.arrival.valid ? "reg" : "-", trip.arrival_sent,
                  trip.ref_count, trip.session_count);
}
