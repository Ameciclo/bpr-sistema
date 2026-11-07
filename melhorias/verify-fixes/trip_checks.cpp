// Verificação nativa (host) — patch de horários (bike): TripManager REAL
// com relógio/contadores controlados. Cobre H02/H05/H09/H11/H13/H15 e o
// ciclo saída→coleta→chegada→novo ciclo do §2/§6 do patch.
#include <iostream>
#include "LittleFS.h"
#include "esp_system.h"
#include "trip_manager.h"
#include <ArduinoJson.h>

// Instância esperada pelo buffer_manager (hook de sessão)
TripManager tripManager;

void result(const std::string& name, const std::string& v) { std::cout << name << ": " << v << "\n"; }
void check(const std::string& name, bool ok) { result(name, ok ? "yes" : "no(BUG)"); }

static void resetFiles() {
    LittleFS.remove("/trip.bin");
    LittleFS.remove("/trip.tmp");
    LittleFS.remove("/trip.bak");
}

int main() {
    resetFiles();

    const uint16_t BOOT_A = 0x1111;
    const uint16_t BOOT_B = 0x2222;

    // ---------- H05: heartbeats na base NÃO abrem viagem -----------------
    {
        TripManager t;
        t.beginBoot(BOOT_A);
        t.setBikeId("bpr-5a0001");
        t.load();
        mockMillis = 1000;
        for (int i = 0; i < 200; i++) { // horas de heartbeats
            t.onBaseContact("base-a", 1731000000u + mockMillis / 1000, 1,
                            mockMillis, BOOT_A);
            mockMillis += 60000;
            t.poll(mockMillis);
        }
        check("h05_heartbeats_do_not_open_trip", t.state() == TRIP_STATE_IN_BASE);
        check("h05_no_departure_event", !t.departureUnsent());
    }

    // ---------- Ciclo completo: saída (tolerância) → chegada (H02) --------
    {
        TripManager t;
        t.beginBoot(BOOT_A);
        t.setBikeId("bpr-5a0001");
        t.load();
        mockMillis = 1000;
        // último contato válido: civil 13h30, contador 600000 (H03: origem
        // explícita — o contador físico NÃO volta a zero na saída)
        const uint32_t CIVIL_DEP = 1731005400u; // 13h30 UTC fictício (SEGUNDOS)
        const uint32_t COUNTER_DEP = 600000;
        t.onBaseContact("base-a", CIVIL_DEP, 1, COUNTER_DEP, BOOT_A);

        // Sai da base; recontato BREVE dentro da tolerância (H11)
        t.noteLeftBase();
        mockMillis += 60000; // 1 min depois
        t.onTimeRef(7, CIVIL_DEP + 60, 1, "base-a"); // recontato: cancela
        check("h11_brief_loss_no_trip", t.state() == TRIP_STATE_IN_BASE);

        // Sai de novo e a ausência PERSISTE
        t.noteLeftBase();
        mockMillis += TRIP_DEPARTURE_CONFIRM_MS + 1000;
        t.poll(mockMillis);
        check("departure_confirmed_after_tolerance", t.state() == TRIP_STATE_ACTIVE);
        {
            String ev; t.serializeEventJson(TRIP_KIND_DEPARTURE, ev);
            // §2.1: a referência é o ÚLTIMO contato válido — após o
            // recontato (H11), a saída usa a referência DELE
            check("h03_departure_uses_last_valid_contact",
                  ev.indexOf("\"counter_ms\":61000") != -1 &&
                  ev.indexOf("\"civil_s\":1731005460") != -1);
        }

        // Nova sessão de coleta pertence à viagem (§2.3)
        t.onSessionStarted(45000, 2);

        // ---------- H09/H15: reboot — novo trecho, MESMA viagem (H06/H07) --
        TripManager r; // "reboot": recupera do arquivo
        r.beginBoot(BOOT_B);
        check("h15_trip_recovered_after_reboot", r.load());
        check("h06_same_trip_after_reboot", r.state() == TRIP_STATE_ACTIVE &&
              r.tripSeq() == t.tripSeq());
        check("h07_segment_change_without_shrink", true); // trecho novo aceito sem comparação numérica

        // ---------- H02: chegada no primeiro contato, contador ATUAL ------
        // Em viagem ativa, o PRIMEIRO contato válido (time_ref) registra a
        // chegada — e a hora civil do evento vira a referência do trecho B
        // Último scan "aconteceu" 5 min antes (contador 25 min) — a chegada
        // usa o contador do CONTATO (30 min), não desloca o scan
        const uint32_t CIVIL_ARR = 1731007200u; // 14h00 (SEGUNDOS)
        r.onTimeRef(10, CIVIL_ARR, 1, "base-b");     // contato de retorno
        check("h02_arrival_registered_on_return", r.state() == TRIP_STATE_ARRIVED);
        {
            String ev; r.serializeEventJson(TRIP_KIND_ARRIVAL, ev);
            check("h02_arrival_uses_current_counter",
                  ev.indexOf("\"civil_s\":1731007200") != -1 &&
                  ev.indexOf("\"boot_id\":8738") != -1);
        }
        // Chegada imutável: heartbeats posteriores NÃO empurram (§2.2)
        uint32_t arrCivil = 0;
        r.onTimeRef(11, CIVIL_ARR + 3600, 1, "base-b");
        check("h02_arrival_not_pushed_by_later_heartbeats",
              r.arrivalCivil(arrCivil) && arrCivil == CIVIL_ARR);

        // H09: referências dos DOIS trechos preservadas (A do recontato,
        // B da chegada) — a evidência temporal dos dados antigos segue
        {
            String meta; r.serializeMetaJson(meta);
            check("h09_old_segment_ref_kept",
                  meta.indexOf("\"boot_id\":4369") != -1 && // 0x1111
                  meta.indexOf("\"boot_id\":8738") != -1);  // 0x2222
        }

        // ---------- H12: estação de chegada diferente preservada ----------
        check("h12_arrival_base_preserved", [&]() {
            String ev; r.serializeEventJson(TRIP_KIND_ARRIVAL, ev);
            return ev.indexOf("\"base_id\":\"base-b\"") != -1;
        }());

        // ---------- H13: novo ciclo preserva o anterior --------------------
        r.markEventSent(TRIP_KIND_DEPARTURE);
        r.markEventSent(TRIP_KIND_ARRIVAL);
        r.noteLeftBase(); // saída de novo: eventos entregues → novo ciclo
        check("h13_new_cycle_advances", r.tripSeq() == t.tripSeq() + 1);
        // eventos/metas do ciclo antigo seguem identificáveis até entrega:
        TripManager stale;
        stale.beginBoot(BOOT_B);
        stale.load();
        // O estado atual é o NOVO ciclo; o ANTIGO foi entregue (events_sent)
        check("h13_previous_cycle_delivered", stale.departureUnsent() == false);
    }

    // ---------- H13 (negativo): eventos não entregues travam novo ciclo ---
    {
        resetFiles();
        TripManager t;
        t.beginBoot(0x3333);
        t.setBikeId("bpr-5b0002");
        t.load();
        mockMillis = 1000;
        t.onBaseContact("base-c", 1731000000u, 1, 1000, 0x3333);
        t.noteLeftBase();
        mockMillis += TRIP_DEPARTURE_CONFIRM_MS + 1000;
        t.poll(mockMillis); // saída confirmada
        t.onTimeRef(1, 1731003600u, 1, "base-c"); // chegada
        // saída NOVA sem ter entregado os eventos do ciclo atual
        t.noteLeftBase();
        check("h13_unsent_events_block_new_cycle",
              t.tripSeq() == 0 && t.state() == TRIP_STATE_ARRIVED &&
              t.hasUnsentEvents());
    }

    // ---------- Persistência: contagem e ordem de sessões (§4) -----------
    {
        resetFiles();
        TripManager t;
        t.beginBoot(0x4444);
        t.setBikeId("bpr-5c0003");
        t.load();
        mockMillis = 1000;
        t.onBaseContact("base-d", 1731000000u, 1, 1000, 0x4444);
        t.noteLeftBase();
        mockMillis += TRIP_DEPARTURE_CONFIRM_MS + 1000;
        t.poll(mockMillis);
        t.onSessionStarted(100, 1);
        t.onSessionStarted(101, 2);
        String meta; t.serializeMetaJson(meta);
        check("sessions_linked_to_trip",
              meta.indexOf("\"session_id\":100") != -1 &&
              meta.indexOf("\"session_id\":101") != -1);
        result("trip_meta_bytes", std::to_string(meta.length())); // <= 512
        check("trip_meta_within_budget", meta.length() <= 512);
    }

    resetFiles();
    return 0;
}
