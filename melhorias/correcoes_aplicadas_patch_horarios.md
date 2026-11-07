# Correções aplicadas do patch de horários (saída, chegada e reinícios)

**Data:** 7 de novembro de 2026.
**Base:** `melhorias/patch_bpr_horarios_viagens.md` (contrato funcional aprovado) sobre `a5a819e`, branch `fixes/november`.
**Escopo desta entrega:** implementação do contrato de horários nas quatro camadas (bike, central, nuvem/functions, bot) + verificação nativa (matriz H) + builds. **Nenhum teste em placa, deploy ou gravação em placa foi realizado.** Contrato registrado em `docs/TRIP_TIMES.md`.

## 1. Verificação crítica do pedido

Os problemas do patch foram confirmados no código base: não existiam eventos de saída/chegada (a "viagem" era derivada da sessão de coleta e finalizada pela hora de processamento — `finished_at = Date.now()` do bot); a bike nunca recebeu hora civil (o `time_sync` legado da central não era consumido pelo fluxo); observações não carregavam o domínio do contador (o `resumed` era o único sinal de trecho misturado); e o reboot só era reconhecido por convenções frágeis.

## 2. O que foi implementado

| Camada | Implementação |
|---|---|
| **Bike** | `trip_manager.{h,cpp}` novo: viagem persistente (`/trip.bin`, TripStateData v1) com `trip_seq`, estados (`in_base`/`departure_pending`/`active`/`arrived`), referência candidata de saída por último contato, tolerância de ausência (5 min) com cancelamento por recontato, chegada no primeiro `time_ref` (contador atual, imutável), referências de hora por trecho (trechos antigos preservados — H09), sessões ligadas, sleep planejado como evidência "solicitada", eventos `trip_event`/`trip_meta` imutáveis por `event_id`. Integração: heartbeat com `boot_id`/`counter_ms`/`last_ref_id` (§5); `main.cpp` com `noteLeftBase` na transição de estado e `poll` da tolerância; `at_base` envia eventos/metadados fora do lote congelado (BTP1 intacto). Formato de sessão **v5**: cada scan/bateria carrega o `boot_id` do seu contador (§6/H07). |
| **Central** | Responde CADA heartbeat com `time_ref` (hora CORRENTE do NTP, validade, idade do sync, `base_id`, `ref_id`) — o legado `time_sync` não consumido permanece intacto. Recebe `trip_event`/`trip_meta`, acumula em `/trips/{bike}_{seq}.json` com merge imutável por `event_id` e publica na nuvem no ciclo de sync (`uploadTripRecords`). |
| **Nuvem (Functions)** | `tripReconstruction.ts` (puro, testável): `reconstructTrip` (duração = chegada − saída em horas civis; pendências explícitas sem invenção) e `buildTimeline` (hora por observação pela referência DO PRÓPRIO trecho; a chegada como referência efetiva; `unknown` com método `none` sem referência). Gatilho `onTripUploaded` publica `trips/{seq}/reconstruction` + `sessions_v2/{sid}-{salt}/timeline` — **sempre separado dos fatos** (§7). `assembly.ts` propaga `boot_id` por observação; bateria `[boot, ts, percent]` com detecção de legado por comprimento. |
| **Bot** | `listenToTripArrivals` (cursor por bike): a chegada REAL do trip finaliza a viagem ativa com `finished_at = arrival.utc_ms` e `finished_basis = "arrival_event_clock"` — o upload/replay nunca a substitui (H04/§7); o período canônico (itens 2/4/5) usa essa âncora. Sem evento, âncora de registro declarada. O fluxo anterior (sessão seguinte) permanece como fallback (H16). |

## 3. Matriz de aceite — status

| ID | Status | Evidência |
|---|---|---|
| H01 | ✅ host | Duração 30 min pelas horas civis; scan dos 25 min às 13h55 (não deslocado) |
| H02 | ✅ host | Chegada no primeiro contato com contador ATUAL; imutável a heartbeats posteriores |
| H03 | ✅ host | Contador físico 100→130 min: origem registrada; duração civil 30 min independe do contador |
| H04 | ✅ host | Replay (backend e bot): mesma identidade, mesma chegada; `finished_at` = evento |
| H05 | ✅ host | 200 heartbeats na base: nenhuma viagem; referência candidata apenas atualizada |
| H06 | ✅ host | Reboot A→B: mesmo `trip_seq`, pontos com refs dos próprios trechos, total 30 min |
| H07 | ✅ host | Trecho novo reconhecido pelo `boot_id` (não por redução numérica) |
| H08 | ✅ host | Trecho sem referência: `unknown`/`none` explícito; trecho da chegada reconstruído |
| H09 | ✅ host | Refs de trechos antigos preservadas após receber a do novo (inclusive pós-reboot) |
| H10 | ✅ host (parcial) | Reconstrutor: pendências `no_valid_civil_reference`/`non_monotonic_civil_clock` sem invenção; NTP real indisponível exige placa |
| H11 | ✅ host | Recontato na tolerância cancela a saída pendente (sem viagem espúria) |
| H12 | ✅ host | Saída `base-a` / chegada `base-b` preservadas como proveniência, mesmo conjunto |
| H13 | ✅ host | Novo ciclo só após entrega dos eventos do anterior; bloqueado preserva dados |
| H14 | ✅ host | Scans com mesmo contador distintos por `scan_key`/numeração |
| H15 | ✅ host | Trip recuperado do arquivo após "reboot" (mesma viagem, mesmo trecho novo) |
| H16 | ✅ host | Legado sem boot/trip: reconstrução compatível, sem throw, sem invenção |

## 4. Validação executada

| Verificação | Resultado |
|---|---|
| Build PlatformIO bici | **SUCCESS** — RAM 29,7% (97.188 B); Flash 39,9% (1.254.773 / 3.145.728 B) |
| Build PlatformIO central | **SUCCESS** — RAM 14,0% (45.780 B); Flash 63,9% (2.009.487 / 3.145.728 B) |
| `run.sh` completo (fontes reais + ASan/UBSan + runner estrito) | **Todas as 9 suítes sem `no(BUG)` e sem sanitizador** — incluindo `trip_checks` (bike, TripManager real) e `trip backend checks` (reconstrução compilada + rideCalculator com chegada real); sem regressão das rodadas anteriores (central, batch/BTP1, bici, bot, itens 2/4/5, T17) |
| Functions `tsc --noEmit` (build e dev) + `npm run build` | 0 diagnósticos; build OK |
| Functions lint | 0 erros (10 avisos `any` preexistentes em `local-bot.ts`) |
| Sintaxe JS do bot (`node --check`) | OK |

## 5. Contratos novos/alterados (resumo)

- Bike: `/trip.bin` (TripStateData v1); `SESSION_FILE_VERSION` **v5** (boot por scan/bateria); heartbeat com `boot_id`/`counter_ms`/`last_ref_id`; mensagens `time_ref` (central→bike), `trip_event`/`trip_meta` (bike→central) — metadados FORA do lote congelado (hash BTP1 intacto).
- Hora civil no transporte: **segundos UTC** (`utc_s`/`civil_s`); ms apenas no backend.
- Nuvem: `bikes/{bike}/trips/{seq}/{trip, reconstruction}` + `sessions_v2/{sid}-{salt}/timeline` (reconstrução separada dos fatos); `sessions_v2` scans ganham `boot_id`; bateria `{boot_id, ts_ms, percent}`.
- Bot: viagens concluídas com `finished_at`/`period_*` ancorados na **chegada real** (`finished_basis: arrival_event_clock`) quando o trip a tem.

## 6. Pendências (homologação)

1. **Placa:** tolerância real da saída (5 min default — ajustável), NTP indisponível/antigo na central (H10 completo), alimentação interrompida em cada ponto de gravação, sleep/retomada, contato BLE interrompido e limites reais de mensagem/MTU com os novos campos.
2. **SDK/emulador real** para gravação/concorrência (os adaptadores locais implementam a semântica de transação, mas não substituem o serviço).
3. **Dashboard** consumindo `trips/{seq}/reconstruction` e `timeline` (a reconstrução já está publicada e separada dos fatos).
4. Ajuste de cadência do upload de viagens (junto ao ciclo de sync da central) conforme observado em bancada.
