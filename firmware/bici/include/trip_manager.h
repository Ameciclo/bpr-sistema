#ifndef TRIP_MANAGER_H
#define TRIP_MANAGER_H

#include <Arduino.h>
#include "constants.h"

// ---- Patch de horários: registros de viagem (saída/chegada/referências) --
#define TRIP_FILE_MAGIC 0x42505254   // "BPRT"
#define TRIP_FILE_VERSION 1

#define TRIP_STATE_NONE 0            // sem viagem (boot sem contato)
#define TRIP_STATE_IN_BASE 1         // na base: heartbeats não abrem viagem
#define TRIP_STATE_DEPARTURE_PENDING 2 // ausência em observação (tolerância)
#define TRIP_STATE_ACTIVE 3          // viagem em curso (saída congelada)
#define TRIP_STATE_ARRIVED 4         // chegada registrada (aguarda novo ciclo)

#define TRIP_KIND_DEPARTURE 1
#define TRIP_KIND_ARRIVAL 2

// Método/qualidade da hora civil do evento (§2.1/§2.2 do patch)
#define TRIP_METHOD_LAST_BASE_CONTACT 1   // saída: último contato válido
#define TRIP_METHOD_FIRST_TIME_REF 2      // chegada: primeiro contato válido

struct TimeRefRecord {
    uint32_t ref_id;        // ID da referência (central)
    uint32_t civil_s;       // hora civil UTC (SEGUNDOS) — ms no backend
    uint8_t civil_valid;    // validade declarada pela central (NTP)
    uint32_t counter_ms;    // leitura LOCAL no instante do recebimento
    uint16_t boot_id;       // trecho/domínio do contador
    uint32_t received_ms;   // millis local do recebimento (diagnóstico)
} __attribute__((packed));

struct TripEventRecord {
    uint8_t valid;
    uint8_t kind;
    uint32_t event_id;      // estável: (trip_seq << 8) | kind
    char base_id[24];       // estação de origem/chegada (proveniência)
    uint32_t civil_s;       // hora civil UTC (SEGUNDOS) do evento
    uint8_t civil_valid;
    uint32_t counter_ms;    // leitura local correspondente ao mesmo evento
    uint16_t boot_id;       // trecho do contador
    uint8_t method;
} __attribute__((packed));

struct TripStateData {
    char bike_id[16];
    uint32_t trip_seq;      // identidade persistente da viagem (por bike)
    uint8_t state;

    // Referência CANDIDATA de saída: último contato válido com a base
    // (heartbeats atualizam; ausência confirmada congela — §2.1)
    char pending_base_id[24];
    uint32_t pending_civil_s;
    uint8_t pending_civil_valid;
    uint32_t pending_counter_ms;
    uint16_t pending_boot_id;
    uint32_t last_contact_ms;   // millis local do último contato (limiar)

    TripEventRecord departure; // congelada na confirmação da saída
    TripEventRecord arrival;   // congelada no primeiro contato do retorno

    TimeRefRecord refs[TRIP_MAX_REFS]; // por trecho (substitui a do mesmo boot)
    uint8_t ref_count;

    uint32_t session_ids[TRIP_MAX_SESSIONS];
    uint16_t session_salts[TRIP_MAX_SESSIONS];
    uint8_t session_count;

    uint32_t last_planned_sleep_sec; // evidência "solicitada" (§6), não medida
    uint8_t sleep_recorded;

    uint8_t departure_sent;  // evento já entregue à central
    uint8_t arrival_sent;
} __attribute__((packed));

struct TripFileHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t payload_size;
    uint32_t payload_crc;
} __attribute__((packed));

class TripManager {
public:
    TripManager();

    bool load();
    bool save();

    // Trecho/domínio de contador atual (chamado a cada energização)
    void beginBoot(uint16_t bootId) { currentBoot = bootId; }
    uint16_t currentBootId() const { return currentBoot; }

    // Identidade canônica da bike (persistida com a viagem)
    void setBikeId(const char* bikeId);

    // Ciclo operacional (chamado pelo loop principal e pelos estados)
    void onBaseContact(const char* baseId, uint32_t civilMs, uint8_t civilValid,
                       uint32_t counterMs, uint16_t bootId);
    void noteLeftBase();      // transição AT_BASE -> SCANNING (tolerância começa)
    void poll(uint32_t nowMs);           // confirma saída por ausência persistente
    void onTimeRef(uint32_t refId, uint32_t civilMs, uint8_t civilValid,
                   const char* baseId);  // guarda referência + registra chegada
    void onSessionStarted(uint32_t sid, uint16_t salt);
    void notePlannedSleep(uint32_t seconds);

    // Consulta/entrega
    uint8_t state() const { return trip.state; }
    uint32_t tripSeq() const { return trip.trip_seq; }
    bool hasUnsentEvents() const;
    bool departureUnsent() const {
        return trip.departure.valid && !trip.departure_sent;
    }
    bool arrivalUnsent() const {
        return trip.arrival.valid && !trip.arrival_sent;
    }
    void markEventSent(uint8_t kind);
    bool serializeEventJson(uint8_t kind, String& out) const; // trip_event
    bool serializeMetaJson(String& out) const;                // trip_meta
    uint32_t lastRefId() const;
    bool arrivalCivil(uint32_t& outMs) const;

    // Diagnóstico
    void logStatus() const;

private:
    TripStateData trip;
    uint16_t currentBoot = 0;

    void confirmDeparture();
    bool registerArrival(uint32_t civilMs, uint8_t civilValid, const char* baseId,
                         uint32_t counterMs);
    void advanceTrip();
    void storeRef(uint32_t refId, uint32_t civilMs, uint8_t civilValid);
    bool validPayload() const;
};

#endif
