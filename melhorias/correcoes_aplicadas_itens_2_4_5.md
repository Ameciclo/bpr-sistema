# Correções aplicadas dos itens 2, 4 e 5 (M03, M04, M06)

**Data:** 7 de novembro de 2026.
**Base:** `melhorias/correcoes_bpr_itens_2_4_5.md` (especificação autorizada) sobre `7a3533b`, branch `fixes/november`.
**Escopo desta entrega:** implementação dos itens 2, 4 e 5 + migração + verificação nativa (matriz C245) + builds/checks. **Nenhum teste em placa, deploy ou gravação em placa foi realizado. Nenhum firmware foi alterado** (a especificação não exige novo firmware para os agregados do backend).

## 1. Verificação crítica dos pedidos

Os três achados foram confirmados no código antes de alterar:

- **Item 2 (M03):** `rideId = ride_{startMs}` usa apenas o tempo relativo de início. O histórico é por bike, mas os tokens públicos (`public_stats/contributed_rides/{rideId}`) são globais — duas bikes com início relativo igual produziam dois históricos válidos e **uma** contribuição pública; a mesma bike repetia o número após reiniciar (millis do boot) e a viagem nova colidia no histórico.
- **Item 4 (M04):** o ramo de reparo do `finishRide` usava km/CO₂/`finished_at` da **tentativa atual** — viagem de janeiro reprocessada em fevereiro contribuía nos dois meses (jan pelo token mensal do bucket antigo, fev pelo bucket novo), embora só exista uma viagem.
- **Item 5 (M06):** o caso sequencial foi corrigido em `7a3533b` (merge com guarda de conteúdo), mas `assembleSessionIfComplete` ainda fazia leitura→montagem→`update({scans})` — coordenadas gravadas pelo conversor **entre** a leitura e a gravação eram sobrescritas pela cópia antiga.

## 2. Item 2 — Identidade estável da viagem e da contribuição

| ID | Implementação |
|---|---|
| ID-01 | A identidade usa o `bikeId` canônico do cadastro (`bpr-XXXXXX`) — horário, nome ou estação não substituem. |
| ID-02 | `startRide(bikeId, origin)` recebe a origem estável `{session_id, session_salt}` (extraída da sessão montada em `bikeMonitor`) junto com `bike_id`; o timestamp relativo é só dado, nunca identidade. |
| ID-03 | `rideId = ride_{ride_rule}_{bikeId}_{sessionId}-{sessionSalt}` com `ride_rule = 'session_v1'` (versão da regra gravada na viagem). A regra atual deriva **uma** viagem por sessão; quando uma regra futura dividir uma origem em várias viagens, a versão sobe para `session_v2` e a divisão estável entra na identidade (C245-15 fica coberto pelo versionamento — não é o caso da v1). |
| ID-04 | Derivação **determinística** da origem (atende o requisito sem ID aleatório a persistir): reenvio, reinício do consumidor e reprocessamento produzem o MESMO rideId. |
| ID-05 | A mesma identidade percorre histórico (`rides/{bike}/{rideId}`), tokens por bike (`bikes/{bike}/metrics/contributed_rides`), tokens públicos (`public_stats/contributed_rides` e `monthly/{mês}`), reparo e consultas (`/status`, notificações, link de rota usa `rideId`). |
| ID-06 | A estação **não** entra na identidade; entra como proveniência: `delivery_bases/{stationId}: true` na viagem (na criação e acrescentada no reparo por outra estação). |
| ID-07 | Origem ausente/inválida → log de pendência explícito + **null** (viagem não derivada; notificação de pendência ao admin). Uptime nunca substitui identidade. |

**Chamadores atualizados:** `bikeMonitor.handleNewSession` extrai `session_id/session_salt` da sessão montada e propaga; `handleBikeArrival` passa a estação como proveniência. `verify-bot.js` migrado para o novo contrato.

## 3. Item 4 — Período e conteúdo autoritativos no reprocessamento

| ID | Implementação |
|---|---|
| MES-01 | A primeira persistência grava `period_month`, `period_basis` (`server_ingest_clock` — **mês de registro/processamento, declarado, não mês comprovado da coleta**), `period_rule` (`registration_month_v1`) e `period_ref_ms` na viagem. |
| MES-02 | O reparo lê a viagem **persistida** e reutiliza km, CO₂ e período dela (`canonicalValues`); a tentativa atual não é autoridade. |
| MES-03 | O relógio da tentativa nunca desloca o mês: o bucket usa o `period_month` persistido (fallback documentado para legado: âncora `finished_at` PERSISTIDA, nunca a da tentativa). |
| MES-04 | Token `contributed_rides/{rideId}` dentro da MESMA transação de cada contador (bike, mensal, geral) — reparo completa só os agregados incompletos, no período original. |
| MES-05 | Canônico mensal = `public_stats/monthly/{YYYY-MM}` (um bucket por mês); totais gerais = campos próprios (`total_rides`, `km_total`, `co2_saved_g`). Os campos legado `total_*_month/current_month` passam a ser **espelho do bucket do período da viagem** (best-effort, compat de leitura) — não acumulam meses diferentes. |
| MES-06 | Divergência de km/CO₂ na tentativa de reparo é **diagnosticada** (log com os dois valores) e a versão canônica prevalece; revisão deliberada de conteúdo/período permanece fluxo auditável à parte (não implementada — não confundir com reprocessamento comum). |

**A escolha do mês fica explícita:** enquanto o patch de tempo não existe, o período é o mês de registro com `basis` declarado; a referência futura (chegada à base com horário válido) já tem lugar reservado (`period_basis/period_ref_ms`) sem mudar a regra agora. Fuso e interpretação de uptime como data civil não foram alterados.

## 4. Item 5 — Não sobrescrever enriquecimento com cópia antiga

Estratégia da especificação adotada: **separar observação recebida de enriquecimento**, vinculados por identidade estável e revisão de conteúdo.

- **Novo módulo puro `bot/functions/src/assembly.ts`** (testável no host): `scanKey`, `hashNetworks` (FNV-1a do conteúdo das redes), `reassembleScans` (remontagem N05 agora emite `scan_key` + `content_hash` por observação), `resolveGeo` (política fresh/stale/legacy/missing), `canInstallGeo`, `mergeLegacyExtras`. DTO de rede extraído para `bot/functions/src/dto.ts` (fonte única).
- **Normalizador** escreve apenas os campos-base em `scans` (com `scan_key`/`content_hash`); o enriquecimento canônico vive em **`geo/{scan_key}`** — escritores disjuntos nunca se sobrescrevem (GEO-02/C245-10/C245-11).
- **Conversor** (`/converter`): pula observações `fresh`; consulta a API **fora de transação**; **re-lê** o scan por `scan_key` e re-confere o `content_hash` antes de instalar (resultado de revisão antiga é descartado com diagnóstico e recalculado — GEO-03/04, C245-12); falha mantém dados + pendência explícita (GEO-05).
- **Leitores** atualizados: `/rota` combina base+geo por identidade (nunca índice de lista), mostra pendentes/desatualizados explicitamente (C245-14). `coordinates` embutido em `scans` vira legado read-only (preservado pelo merge, não mais escrito) — um só caminho canônico (GEO-06). `local-bot.ts` opera no schema v1 legado (path distinto) e não conflita com o contrato v2.

## 5. Migração e reconciliação (item 2 §Migração)

`bot/migrate-ride-identity.js` (npm `migrate` — dry-run; `migrate:apply`):

1. Inventaria `rides/{bike}/` e os tokens atuais.
2. Associação inequívoca legado→canônica por **bike + `session_start_ms == start_ts`** na `sessions_v2` da MESMA bike — duas bikes com `ride_10000` nunca são fundidas (C245-09).
3. Path legado recebe `migrated_to` (alias/proveniência), conteúdo preservado para leitores antigos.
4. Agregados (`bikes/{bike}/metrics` e `public_stats` com `monthly`) **reconstruídos** a partir das viagens únicas recuperáveis — renomear token não corrigiria total que já perdeu contribuição.
5. Ambíguas (sem sessão, múltiplas sessões, destino já existente) ficam de FORA dos agregados e viram pendência em `migrations/pending/ride_identity/` com a evidência (snapshot) — nada inventado ou apagado.

O script reporta antes/depois e exige `--apply` para gravar. **Não foi executado contra banco real** (não há acesso; execução é operação manual documentada).

## 6. Validação executada

| Verificação | Resultado |
|---|---|
| Matriz C245 (itens 2/4) — `verify-item245.js`: rideCalculator + migrador reais, RTDB transacional local | C245-01..09 + ID-07 **sem BUG** (50 saídas): duas bikes/mesmo início → 2 contribuições; mesma bike/sessões distintas → identidades distintas; outra estação → mesma viagem + proveniência; concorrência → 1 entidade; restart → reparo único; **jan=1/fev=0/geral=1** com espelho=jan; km divergente → canônica; migração dry-run/apply com agregados reconstruídos |
| Matriz C245 (item 5) — `verify-functions-assembly.js`: assembly/dto reais compilados com o tsc do projeto | C245-10..14 + GEO-01..05 + N05 **sem BUG** (18 saídas): remontagem por scan_key; conteúdo divergente não herda extras; resultado stale não instalado; ausência explícita; fora de ordem converge; legado sem idx/ts repetido sem undefined |
| `run.sh` completo (fontes reais + sanitizadores + runner estrito) | **185 saídas** (47 bici + 47 central + 23 bot + 50 item2/4 + 18 item5), **zero `no(BUG)`**, zero achado de ASan/UBSan — sem regressão das rodadas anteriores |
| Functions `tsc --noEmit` (build e dev) + `npm run build` | 0 diagnósticos; build OK |
| Functions lint | 0 erros (10 avisos `any` preexistentes em `local-bot.ts`, utilitário de dev) |
| Sintaxe JS do bot (`node --check`) + migrador | OK (10 arquivos) |
| Firmware | **Não alterado nesta rodada** (builds anteriores seguem válidos) |

C245-15: a regra v1 deriva uma viagem por origem; o requisito de divisão estável só se aplica quando uma regra futura derivar várias — o versionamento (`ride_rule` na identidade e na viagem) reserva esse lugar.

**Limitação declarada:** as transações foram exercitadas com adaptador local que implementa a semântica do SDK (abort = `undefined`, handler pode rodar mais de uma vez) — a especificação pede SDK/emulador real nos pontos de transação; a validação com emulador/Firebase real permanece pendente de homologação, assim como a execução da migração contra o banco de produção.

## 7. Contratos novos/alterados

- `rides/{bike}/{rideId}`: rideId canônico `ride_session_v1_{bike}_{sid}-{salt}`; campos `ride_rule`, `origin`, `period_month/basis/rule/ref_ms`, `delivery_bases`, `finished_at`, `ts_domain`; legado `ride_{startMs}` vira alias com `migrated_to`.
- `sessions_v2/{key}/scans[]`: ganham `scan_key` e `content_hash` (base); `coordinates` embutido é legado read-only.
- `sessions_v2/{key}/geo/{scan_key}`: **novo** enriquecimento canônico `{lat, lng, accuracy, base_hash, source, computed_at}`.
- `public_stats`: totais gerais `total_rides/km_total/co2_saved_g` (canônico) + `monthly/{mês}` (canônico mensal) + espelho legado `total_*_month/current_month`; tokens `contributed_rides` com rideId canônico em todos os agregadores.
- `migrations/pending/ride_identity/`: pendências de migração com evidência.
- `bot/functions/src/assembly.ts` e `dto.ts`: novos módulos puros (lógica de montagem/enriquecimento extraída do index.ts).

## 8. Pendências (fora do escopo desta entrega)

1. **Homologação com Firebase/emulador real** nos pontos de transação (C245 recomenda SDK real além dos adaptadores).
2. **Execução da migração** contra o banco real (`npm run migrate` → revisar relatório → `npm run migrate:apply`), com comparativo antes/depois arquivado.
3. Debates temporais (relógio da bike, reconstrução pós-reboot, retomada de entrega) — seguem no documento de discussão; esta correção não mudou a definição de saída física nem redefiniu sessão=viagem.
4. M05 (sessões que o consumidor deixa de processar), revisão auditável deliberada (MES-06) e dashboard — mantêm seus próprios status.
