# BPR — Patch de horários: saída, chegada e reinícios

**Versão do contrato:** 1 — implementado conforme
`melhorias/patch_bpr_horarios_viagens.md`.
**Código de referência:** `firmware/bici/src/trip_manager.cpp` (bike),
`firmware/central/src/bike_pairing.cpp` (time_ref/eventos),
`bot/functions/src/tripReconstruction.ts` (backend),
`bot/src/services/{bikeMonitor,rideCalculator}.js` (chegada real).

## 1. Modelo

| Registro | Onde vive | Conteúdo |
|---|---|---|
| **Viagem (trip)** | bike (`/trip.bin`, TripStateData v1) + nuvem (`bikes/{bike}/trips/{seq}`) | `trip_seq` persistente por bike, estado (`in_base`/`departure_pending`/`active`/`arrived`), referências de saída/chegada, referências de hora por trecho, sessões ligadas, sleep planejado (natureza "solicitada"). |
| **Evento de saída/chegada** | bike → central → nuvem (`trips/{seq}/trip/events`) | `event_id` estável (`(seq<<8)|kind`), estação, `civil_s` (UTC **segundos**; ms no backend), `civil_valid`, `counter_ms` (leitura local NO evento), `boot_id` (trecho), `method` (`last_base_contact` / `first_contact_time_ref`). Imutável no reenvio. |
| **Referência de hora** | bike (por trecho) → central → nuvem (`trips/{seq}/trip/time_refs`) | `ref_id`, `civil_s`, `civil_valid`, `counter_ms` (leitura local AO RECEBER), `boot_id`. Uma por trecho; trechos antigos preservados (H09). |
| **Observação** | sessões v2 | `scan_key`, `boot_id` (trecho do contador — formato v5), `ts_ms` (contador). A numeração (`idx`) é a autoridade de ordem. |
| **Reconstrução** | nuvem (`trips/{seq}/reconstruction` + `sessions_v2/{sid}-{salt}/timeline`) | Sempre SEPARADA dos fatos: hora atribuída, referência/método (`ref_forward`/`none`), qualidade e versão. |

## 2. Ciclo operacional

- **Saída (§2.1):** heartbeats na base atualizam a referência candidata
  (último contato válido) — NÃO abrem viagem (H05). A transição
  `AT_BASE→SCANNING` inicia a tolerância (`TRIP_DEPARTURE_CONFIRM_MS`, 5 min
  default); recontato dentro dela CANCELA a saída pendente (H11 — perda
  breve de BLE não gera viagem espúria). Confirmada a ausência, o evento de
  saída é congelado com a hora civil e o contador do último contato.
- **Chegada (§2.2):** em viagem ativa, o PRIMEIRO contato válido (a resposta
  `time_ref` do heartbeat) registra a chegada com a hora da estação e a
  leitura ATUAL do contador, no mesmo evento (H02). Imutável: heartbeats
  posteriores apenas atualizam sincronização (não empurram a chegada).
  Estação de chegada ≠ origem é proveniência (H12).
- **Novo ciclo (§2.3/H13):** a chegada encerra o período, NÃO autoriza
  apagar dados (limpeza é do PRD de lotes/recibos). Nova saída avança
  `trip_seq` SOMENTE com os eventos do ciclo anterior entregues — pendente
  de entrega, o novo ciclo fica bloqueado com os dados preservados.
- **Reboot (§6):** o trecho (`boot_id`) viaja com CADA observação e leitura
  de bateria (formato de sessão v5); reboot não é deduzido por redução de
  contador (H07) nem abre/fecha viagem (H06). Sleep planejado é registrado
  como evidência **solicitada** (`sleep_nature:"requested"`), nunca como
  medição do tempo transcorrido.

## 3. Referência de hora pelo heartbeat (§5)

1. Heartbeat da bike carrega `boot_id` (trecho atual), `counter_ms`
   (millis) e `last_ref_id` (a referência guardada — passo 4).
2. A central responde CADA heartbeat com `time_ref`:
   `{v, bike_id, ref_id, utc_s, valid, base_id, sync_age_sec}` — a hora
   CORRENTE da sincronização NTP (não congelada), com validade e idade.
3. A bike lê o contador AGORA e persiste a associação
   (ref ↔ civil ↔ contador ↔ trecho).
4. A referência guardada é devolvida no próximo heartbeat.

Os eventos/metadados acompanham a visita como **metadados vinculados**
(`trip_event`/`trip_meta` na data char): nunca tocam no lote congelado nem
no seu hash (BTP1 intacto).

## 4. Reconstrução no backend (§3/§7)

- **Duração** = `chegada − saída` em horas civis válidas
  (`civil_diff_v1`) — o upload/replay NUNCA a substitui (H04). Sem hora
  válida, relógio não monotônico ou chegada ausente → pendência explícita
  (`no_valid_civil_reference` / `non_monotonic_civil_clock` /
  `awaiting_arrival`) — sem duração negativa, falsa precisão ou descarte
  (H10).
- **Pontos:** hora = `civil_ref + (ts − counter_ref)` pela referência DO
  PRÓPRIO TRECHO (a chegada entra como referência efetiva do seu trecho —
  a "conta para trás" do §3 é algebricamente a mesma). Sem referência do
  trecho → `unknown` com método `none` (H08). NUNCA se aplica referência de
  outro trecho (§6). Scans com o mesmo contador permanecem distintos pela
  identidade (H14).
- **Bot:** a chegada REAL do trip conclui a viagem ativa com
  `finished_at = arrival.utc_ms` e `finished_basis = "arrival_event_clock"`
  (o mês/período segue essa âncora — MES-01 dos itens 2/4/5); sem evento,
  âncora de registro (`server_ingest_clock`) declarada. Legado sem trip
  mantém o fluxo anterior (H16).

## 5. Formato e unidades

- Hora civil no firmware/transporte: **segundos UTC** (`civil_s`/`utc_s`,
  uint32 até 2106); o backend converte para ms (`utc_ms`).
- Contador local: **ms desde o boot (trecho)** — nunca timestamp Unix.
- Bateria por fragmento final: `[boot, ts, percent]` (3 posições; legado de
  2 posições tratado por comprimento). Scans: campo `boot` por observação.
- `SESSION_FILE_VERSION` 5 (v3/v4 nunca gravados em placa — bump sem perda).

## 6. Aceite (matriz H)

Verificada no host (`run.sh`: `trip_checks` bike + `trip backend checks`):
H01–H09, H11–H16 sem BUG. **H10** parcial no host (qualidade/pendência do
reconstrutor; a indisponibilidade real de NTP na central exige placa).
**Pendente de homologação:** bancada (alimentação interrompida, sleep,
retomada, contato BLE, tolerância da saída, limites reais de mensagem) e
SDK/emulador real para gravação/concorrência.
