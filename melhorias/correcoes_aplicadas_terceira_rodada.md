# Correções aplicadas da terceira rodada (N01–N11, X01–X10)

**Data:** 7 de novembro de 2026.
**Base:** `revisao_bpr_fixes_november.md` (revisão 2, commit `23b13aa`) + `comparacao_revisoes_bpr_23b13aa.md` (conferência dos 12 críticos e adendos X01–X10).
**Escopo desta entrega:** correções de código firmware (bike + central), normalizador (Functions) e bot; builds completos; checks de tipo/lint/sintaxe.
**Não incluído:** testes em placa, bancada de bateria/rádio, deploy. As pendências físicas estão listadas em §5.

---

## 1. Ordem de aplicação (seguiu a recomendada pela comparação)

1. N01 (bloqueio de boot) → 2. X06+N02+N06 (coleta/serialização/transporte em conjunto) → 3. X01+X02 (energia) → 4. N03/N07/N08/X05 (persistência/recuperação) → 5. X03/X04/X07/X08/X09/X10 + N04/N05/N09/N10/N11 (consumidores, cadastro, configuração, prazos) → 6. build/check.

## 2. Firmware bici (`firmware/bici/`)

| Achado | Correção aplicada |
|---|---|
| **N01 (P0)** | `buffer_manager.cpp`: a candidata automática de 54.328 B **deixou de existir**. O header (16 B, pilha) é validado primeiro; o payload é lido **direto em `currentSession`** (BSS global). Falha de validação em qualquer ponto quarentena o arquivo — comportamento idêntico ao anterior, sem o frame de 54 KB na pilha de 8 KB. |
| **N02** | `isFull()` passou a ser regido **apenas por scans** (`scan_count >= MAX_SCANS`); telemetria de bateria satura em separado com log único (`addBattery`). A coleta volta a correr até os 50 scans declarados (parava em 10). Heartbeat reporta carga útil real: novo `getPendingDataBytes()` (soma redes+baterias armazenadas) em vez do `getSize()` constante de 54.328 B. |
| **X06** | Arena JSON do fragmento redimensionada para `FRAGMENT_JSON_ARENA = 1280` (era `MAX_BLE_FRAGMENT_BYTES + 192 = 652`). Cobre o pior caso medido no alvo: chunk de 7 redes = 48 slots × 16 B + strings ≤ payload (460) ≈ 1.225 B; final com 10 baterias = 41 slots = 656 B. `doc.overflowed()` continua como guarda. |
| **N04 (bike)** | Cada fragmento de dados transmite **`idx`** (índice do scan na sessão) — identidade estável da observação para o consumidor remontar chunks; fragmento final transmite **`resumed`** quando `currentSession.boot_id != bootId` (sessão retomada entre boots, dado misturado sinalizado). `load()` **não sobrescreve mais** o `bootId` do membro (permanece o da energização atual). Sessão recuperada porém vazia é substituída por sessão nova no boot (não carimba a coleta com `session_start` de boot antigo). `HEADER_BYTES` 190→200 (cobre o campo `idx`). |
| **X01** | `power_manager.cpp` reescrito: primeira consulta **lê o ADC** (cache começa vazio — não existe mais o default fictício de 4,0 V); média de 4 amostras; `adc < 100` = leitura anômala → tensão 0 (desconhecido), **não** "USB 100%"; percentual contínuo 3,2–4,2 V **sem o salto 69%→100% em 3,9 V**; `isBatteryCharging()` só acima de 4,25 V (acima do máximo físico de uma Li-ion — evidência compatível com fonte externa); decisões de energia (`checkBatteryState`/`isLowBattery`/`isCriticalBattery`) ignoram leitura desconhecida (não dorme nem alerta por falha de medição). `enterDeepSleep` valida duração, confere o retorno de `esp_sleep_enable_timer_wakeup` e retorna `bool` — **não dorme sem wake-up armado** (era caminho de brick); `handleSleep` persiste uma única vez por tentativa (sem regravar 54 KB a cada passada do loop). |
| **X02** | `at_base.cpp`: orçamento **global** de visita `AT_BASE_MAX_DURATION_MS` (300 s) — idle sem dados, busy persistente ou rejeição contínua encerram a visita e desconectam; a suspensão da proteção de bateria durante transferência fica limitada a esse prazo. O check de `busy` foi movido para **antes** do `(re)start` da transferência: durante a espera `transferActive` permanece false e a proteção de bateria do loop segue ativa. Espera de `busy` limitada a 3.600 s. Transferência **retomável**: mesma sessão (id/salt inalterados) continua do primeiro fragmento sem ACK após reconexão/busy (dedupe da central por `session_id+salt+seq+CRC`). Log "AT_BASE - Syncing data" throttled para 5 s (era a cada 100 ms). |
| **X08** | `requestConfig()` com retry: resultado da escrita verificado; sem `config_push` dentro de `CONFIG_REQUEST_TIMEOUT_MS` (10 s) repete até `MAX_CONFIG_REQUEST_RETRIES` (3); ao esgotar, segue a conexão com a config atual (reconexão pede de novo). `config_push` marca `configReceived`. |
| **X09** | `ClientCallbacks::onDisconnect` (task NimBLE) **apenas sinaliza** (`disconnectSignaled`); ponteiros/flags compartilhados são zerados por `onBLEDisconnected()` **no contexto do loop**. Fim da corrida com `sendFragment()` usando `pDataChar`. |
| **X07 + N09.2** | `isValidConfig()` agora é **integral** (terminadores, `version>0`, `scan_interval≥5`, `scan_timeout≥100`, `max_networks∈[1,20]`, `base_name` terminado, `ble.scan_time_sec≥1`, `connection_timeout≥1000`, `deep_sleep≥60`, tensões em (3,0; 4,5) com `critical<low`) e vale **igualmente para o load** — arquivo persistido com campo inválido (ex.: `ble.scan_time_sec=0` → scan BLE infinito) restaura defaults seguros **preservando a identidade** (`bike_id` válida é mantida e o arquivo é curado). No push, campo presente com valor fora da faixa **rejeita a operação inteira** (`applyUpdate(..., bool& rejected)`) — nada de aplicar metade e confirmar a revisão. |
| **N09.1** | Revisão **maior** com conteúdo equivalente agora é adotada (versão remota persistida + ACK ok) — antes era recusada por "sem mudanças" e a central reenviava para sempre. |
| **N03** | Gravação de sessão e config: instalação por **rename direto** (substituição atômica); fallback `.bak` preserva o íntegro durante a troca (a janela `remove`+`rename` perdia o arquivo se faltasse energia entre as duas). `load()` **recupera de `.tmp`/`.bak`** quando o canônico sumiu (tmp = cópia mais nova; bak = anterior íntegra), com quarentena de candidato inválido. `clear()` remove também as cópias de troca. |
| **X05** | Mount do LittleFS **sem formatação silenciosa**: marca de custódia em NVS (`Preferences`). Instalação virgem (sem marca) → formatar é permitido. **Com custódia** → não formata; LED pisca em erro, log CRITICAL e deep sleep de 1 h para nova tentativa (dados preservados para recuperação via USB); se o armamento do sleep falhar, permanece parado e visível em vez de prosseguir sem FS (que regeneraria identidade a cada boot). |

## 3. Firmware central (`firmware/central/`)

| Achado | Correção aplicada |
|---|---|
| **N06** | `MAX_SESSION_FRAGMENTS` 256→**512** — cobre o pior caso legítimo medido (50 scans × 20 redes com SSID de 32 aspas = 501 fragmentos). `MAX_TOTAL_ITEMS` permanece 800 (limite de flash: 800 × 536 B ≈ 429 KB em 960 KB). |
| **N08** | `forEachItem()` **propaga false** em item truncado/tamanho inválido/CRC errado (era `break` + `return true`). Efeito direto: `rewriteBikeBufferExcluding` aborta e **preserva o original** — não substitui mais o spool por cópia parcial apresentada como completa (cenário A/B/C em que C válido era descartado junto com B corrompido). **Modo reparo:** com o contador `corruptRecords`, a iteração **pula o registro corrompido e continua** (registros de tamanho fixo continuam endereçáveis depois do ponto de corrupção); a reescrita usa esse modo — mantém os registros **válidos posteriores** à corrupção no novo spool e **quarentena o original** (`.corrupt`). A recuperação pós-corrupção deixou de ser operação manual via USB. |
| **X05/C9** | Header inválido no spool de uma bike: arquivo é **quarentenado** (`{bike}.bin.corrupt`) e um spool limpo é recriado — a bike volta a poder entregar (antes: `busy` 30 s para sempre neste arquivo). Evidência preservada para recuperação via USB. |
| **N07** | `writeManifest` valida `overflowed()` + bytes escritos + **releitura do gravado** (manifesto parcial/ilegível é descartado, nunca tratado como durável; gravação via tmp+rename). Falha de `setItemUploadedFlag` é **propagada**: o lote volta a pendente via `rollbackUpload` **fora do mutex** (nada fica "meio preparado"). `rollbackUpload` mantém o manifesto se falhar ao restaurar qualquer flag (o reconcile do boot tenta de novo). | 
| **Índices do manifesto (seção 6)** | Item e índice passam a viajar **pareados** (`ManifestEntry`) na seleção, ordenação e marcação de flags — ordenar só os itens dissociava o par e podia marcar o registro de outra bike/posição quando a enumeração não coincidia com a seleção. |
| **N03** | `rewriteBikeBufferExcluding` instala por rename direto com fallback `.bak` (mesmo padrão da bike). Novo `recoverStagedBuffers()` no `begin()`: varre `/buffer` por `.bin.tmp`/`.bin.bak` órfãos — canônico sumiu → recupera (tmp = mais novo; bak = anterior); canônico presente → staging obsoleto removido (estado converge via reconcile de lotes). Config da central: mesmo padrão de instalação atômica + recuperação no load. |
| **O04** | `createBackup` verifica `hasEnoughSpace()` antes de copiar — backup é **adiado** (logado) quando o espaço está baixo; gravações críticas (spool/manifesto) têm prioridade sobre a cópia. |
| **X03** | `processDataFromBike` libera a vaga (**`finishCurrentBike`**) quando o fragmento final é confirmável (`is_final && canClear`) — a próxima bike não espera os 30 s de timeout. `processBikeDisconnected` libera a vaga se a bike atual desconectou e **descarta os itens enfileirados** dela com reconciliação definida (itens não ACKados: a bike mantém os dados e reenvia na próxima conexão). |
| **X04** | `getPendingBikesForUpload` publica **também** pendentes descobertas sem relógio (`created_at=0`): `first_seen` com qualidade explícita (`first_seen_valid`) e reaproveitamento do contato confiável posterior — o filtro `created_at>0` escondia a bike da aprovação remota para sempre, mesmo com NTP válido depois. |
| **X10** | `uploadHeartbeat` virou **não-fatal** (observabilidade não bloqueia os downloads de config/registry que corrigiriam o próprio sistema). Timeouts HTTP explícitos em **todas** as 8 chamadas (`Endpoints::setupHttp`: conexão 8 s, leitura 15 s — o default de 5 s da biblioteca não é deadline do produto). `yield()` entre lotes do upload (loop longo em C3 single-core). |
| **X09/C12** | `ConfigCallbacks::onWrite` **não consulta mais** `BikeManager::canConnect` na task do NimBLE — a autorização (leitura de `bikeStatus`, alterado pelo loop via merge/heartbeats) passou para `processConfigRequest` no contexto do loop. Verificação de identidade (mapas com mutex) permanece no callback. |
| **N09.3** | `loadConfig` da central: conteúdo inválido (tamanho certo, valores errados) **não é mais atribuído** ao estado ativo — restaura defaults (antes: aplicava a candidata inválida e só retornava false). |
| **N09.4** | Portal `/save` e `/save-json`: falha de `saveCredentials` **restaura a RAM anterior** (antes: respondia erro com a credencial nova já aplicada em memória). Timeout do AP **unificado** em `fallback.config_ap_timeout_sec` nos três pontos (update, `/status`, aviso HTML) — o par divergente exibia 15 min enquanto a transição real ocorria em 5 min. |
| **X05 (central)** | Mount sem formatação silenciosa nem restart-loop: marca de custódia em NVS; virgem → formatar; com custódia → **modo degradado distinguível** (`fsFault` global, log CRITICAL, exposto como `fs_fault` no `/status` do AP) preservando buffer/credenciais para recuperação via USB. |
| **Seção 6 — cadastro cheio** | Retorno de `mergeFromRegistry()` é **verificado** no sync (no download **e** no caminho "já atualizado"): lista cheia sem pendente evictável **falha a sync visivelmente** e reconfigura — antes o `✅ downloaded` escondia a não-aplicação (troca de frota sem efeito). |
| **Seção 6 — fila de dados BLE** | `priorityQueue` com capacidade **definida** (`MAX_BLE_QUEUE_ITEMS=12`): acima do teto o item é recusado com `busy` (a bike mantém o dado até ACK e reenvia) — a fila não cresce sem limite na heap do C3. |
| **Seção 6 — backup entre lotes** | `/backup` limitado por **orçamento de bytes** (`BACKUP_MAX_BYTES=200 KB`) aplicado **na criação** (`enforceBackupBudget`, além da retenção por contagem) — a limpeza não esperava o próximo boot e 3 cópias integrais do spool cheio não caberiam na partição. |
| **N03 (credenciais)** | `saveCredentials` grava em **tmp+rename** com fallback `.bak` (fim da regravação direta destrutiva — queda de energia no meio deixava a central sem credenciais para sempre); `loadCredentials` recupera de `.tmp`/`.bak`. |

## 4. Functions e bot

### Normalizador (`bot/functions/src/index.ts`)

| Achado | Correção |
|---|---|
| **N05 (remontagem)** | `assembleSessionIfComplete` **reúne os chunks de um mesmo scan** em uma única observação: agrupamento por `idx` (identidade transmitida pelo firmware) com fallback em `ts`; redes dos chunks ordenadas por `net_offset` e concatenadas. Um scan denso dividido em N fragmentos vira **1** scan com todas as redes (antes: N scans de 1 rede cada, e `scan_count` divergia de `scans.length`). |
| **N05 (replay)** | A projeção **preserva enriquecimento**: antes de reescrever `scans`, lê a sessão existente e mantém qualquer campo extra por scan (ex.: `coordinates` gravadas pelo conversor geográfico) **quando o conteúdo equivale** (mesma `ts_ms`) — índice sozinho poderia levar extras para o scan errado se a projeção anterior divergiu. Replay do mesmo lote não apaga mais coordenadas já calculadas. |
| **N04** | Sessão montada registra `resumed` (retomada entre boots). Notificações e `/status` **não inventam mais data absoluta** de `session_start_ms` (é millis desde o boot da bike) — rótulo relativo explícito. |
| **N10** | Comparador de chave `{sid}-{salt}` **numérico** (`sessionKeyComparator`) usado na seleção de última sessão — a ordenação lexicográfica escolhia "9999-1" como mais recente que "10000-1". Aplica-se a `/rota` e ao `/status` das Functions (que usava `orderByKey().limitToLast(1)`) e ao `getLastSession`/bateria do bot (âncora `ingest_ts`). |

### Bot (`bot/src/`)

| Achado | Correção |
|---|---|
| **N10 (seleção/cursor)** | `firebase.js`: seleção de sessão por `ingest_ts` (âncora server-side) com fallback numérico — substitui o `orderByKey` lexicográfico. `listenToNewSessions` usa **cursor** (`notifiedSessions`): só notifica sessões ainda não vistas (o restart do bot não reanuncia histórico como sessão nova). |
| **N10 (listeners)** | `listenToSessionScans` mantém **uma assinatura ativa por bike** — a sessão anterior é desanexada (`ref.off()`) quando a próxima é assinada (listeners acumulavam por sessão sem remoção). |
| **N10 (chegada)** | Listener de connections (schema v1 sem produtor no fluxo v2) **removido**. A chegada deriva do contrato ativo: nova sessão v2 montada = bike entregou dados na base → a viagem ativa da sessão anterior é finalizada (`handleBikeArrival`) e a viagem da sessão nova é aberta. O ciclo partida→coleta→chegada fecha ponta a ponta. |
| **N10 (dedupe)** | Chave de dedupe de scan inclui a sessão (`scan_{bikeId}_{sessionId}`) — scan de sessão nova com timestamp menor (pós-reboot) não era mais recusado pelo da anterior. |
| **N04 (unidades)** | Fim da heurística `toMs` encadeada (multiplicava 10.000 ms por 1000²): `fromV2Ms` (v2 já é ms) e `fromLegacySec` (legado em segundos) — **um único ponto de conversão por origem**. `isHistorical` ancorado no `ingest_ts` do servidor (a comparação anterior misturava boot-ms com unix-ms e marcava todos os scans como históricos). Mensagens exibem rótulos relativos (`boot+N ms`) em vez de datas inventadas de 1970. |
| **N04 (duração)** | `finishRide` calcula duração em **domínio único** (span entre primeiro e último ponto, ms do boot) — `end_ts` no mesmo domínio do `start_ts`, com `finished_at` (âncora absoluta do servidor) e `ts_domain` declarado. Duração anterior misturava boot-ms com Date.now() e produzia ~28 milhões de minutos. |
| **N11 (idempotência)** | Criação da viagem por **transação abort-if-exists** (`rides/{bike}/{rideId}`): duas finalizações concorrentes → uma cria, a outra aborta (o par exists+set observava ausência nas duas e somava o contador 2×). Métricas e estatísticas só quando **esta execução** criou a viagem. |
| **N11 (tokens por viagem)** | Cada agregador (`bikes/{id}/metrics`, `public_stats/monthly/{mês}`, `public_stats`) carrega o **token `contributed_rides/{rideId}` DENTRO da mesma transação do contador** — a contribuição entra exatamente uma vez mesmo com replay/concorrência. **Reparo reconciliável:** crash entre criar a viagem e agregar não perde mais a contribuição — o replay encontra a viagem existente e completa a agregação (o token impede duplicação). |
| **N11 (NaN)** | Todas as transações de agregado (`updateBikeMetrics`, mensal e geral) **coercem campos ausentes** (`Number(x) \|\| 0`) — o primeiro resumo com `public_stats` já existente (ramo monthly criado) mas contadores gerais ausentes gravava NaN como estatística pública definitiva. |
| **N04 (período mensal)** | O bucket mensal usa a **âncora absoluta do servidor** (`finished_at`) — `end_ts` da viagem está no domínio boot-da-bike e produzia o mês "1970-01" como período de toda viagem. |
| **A03 (seção 6)** | `stationMonitor`: heartbeat inválido depois de um estado conhecido **grava `null`** no mapa da estação — o "online" do período anterior não persiste; transições notificadas apenas entre estados conhecidos (null = sem evidência, não é "offline"). |

## 5. Verificação

| Check | Resultado |
|---|---|
| Build ESP32 bike (`pio run`) | **SUCCESS** — Flash 39,4% (1.237.979/3.145.728 B), RAM 29,5%. Warnings remanescentes são pré-existentes (formato `%d`/`%x` com `uint32_t`; redefinições de flags NimBLE do sdkconfig). |
| Build ESP32 central (`pio run`) | **SUCCESS** — Flash 61,4% (1.931.385/3.145.728 B), RAM 14,0%. |
| Verificadores nativos (`melhorias/verify-fixes/run.sh`, fontes atuais + stubs + ASan/UBSan) | **116 saídas** (47 bici + 47 central + 22 bot), **zero `no(BUG)`**, zero achado de sanitizador. Destaques: `n02_scans_collected: 12` com `n02_battery_capped_at_max: 10` e `n02_collection_continues_past_battery_cap: yes` (**N02 verificado no fluxo real de coleta** — a revisão 2 reproduzia parada em 10); `n06_worst_case_fragment_total: 501` com todos serializáveis e `n06_complete_worst_case_session: yes` na central (**E02 ponta a ponta**); N03 (recuperação de staging/backup), N07 (flag parcial → manifesto mantido → reconcile), N08 (corrupção no meio → registro posterior válido **preservado** + original quarentenado), N09.1/N09.2/N09.3, X07 e os itens de seção 6 (índices do manifesto, orçamento de backup, cadastro) verificados; R02–R20 da rodada anterior **sem regressão**. |
| **O08 (runner estrito)** | O `run.sh` agora **reprova** se qualquer saída contiver `no(BUG)` ou se os sanitizadores acusarem — os valores impressos viraram critério de bloqueio, não apenas evidência. |
| Verificador do bot (`verify-bot.js` — `rideCalculator` real + adaptador RTDB transacional local) | E07 (duas finalizações concorrentes → **1** viagem, contadores = 1), E08 (primeiro resumo **sem NaN**), N04 (duração em domínio único; bucket mensal pela âncora do servidor; sem conversão dupla) e **reparo** (crash entre criar viagem e agregar → replay completa a contribuição exatamente uma vez). Nenhum Firebase/Telegram real contatado. |
| TypeScript Functions (`tsc`, build e dev) | Zero diagnósticos. |
| Lint Functions | Zero erros (10 warnings pré-existentes em `local-bot.ts`). |
| Sintaxe JS do bot (`node --check`) | 9/9 arquivos OK. |
| Placa, rádio, ADC, MTU negociado, Firebase real | **Não executados** — pendências de bancada/homologação. |

Notas de verificação específicas:

- **X06 (arena)**: orçamento derivado do layout do alvo (slot de 16 B) e do particionador (máx. 7 redes/chunk por bytes; 10 baterias no final) — 1.280 B ≥ 1.225 B do pior caso. Não mediado em placa.
- **N06**: 512 > 501 (pior caso legítimo com SSID de 32 aspas). Sessão de 501 fragmentos a ~5 frags/s ≈ 100 s — dentro do orçamento de visita de 300 s (X02).
- **N03**: a instalação primária é `rename` direto (substituição atômica no LittleFS); o fallback `.bak` + recuperação no boot cobre a semântica de destino inexistente e a queda de energia no meio da troca. Não simulada queda física de energia.
- **N01**: o frame de `load()` não reserva mais a candidata — **medido no ELF compilado: 32 B** (antes 54.432 B); `tryLoadFrom()` usa **80 B**; a tarefa loop tem 8.192 B. A reserva antecendia a primeira consulta ao arquivo; agora a leitura vai direto ao `currentSession` (BSS global).

## 6. Pendências deliberadamente não encerradas

Estes pontos continuam abertos por exigirem placa, decisão de produto ou outro ciclo — não devem ser tratados como corrigidos:

1. **T45–T54** (PRD): todos os cenários de bancada — bateria real/USB, MTUs 23/185/512, desconexão durante escrita, prazo global medido, backlog alto.
2. **X08 (fragmentação central→bike)**: config que não caiba no MTU negociado continua **recusada com log explícito** (sem fragmentação de envelope). Com MTU 512 (negociado em runtime pelos dois lados) o envelope de ~300 B cabe; o caminho de fallback por MTU pequeno permanece um contrato a fechar.
3. **Rotação multi-sessão** (backlog herdado): a bike ainda carrega UMA sessão por vez; saturação encerra a coleta e prioriza entrega.
4. **NTP para a bike**: timestamps seguem relativos ao boot (agora com `idx`, `resumed` e rótulos honestos); a âncora absoluta por sincronização de tempo central→bike permanece proposta.
5. **Coredump/OTA**: partição continua sem área de coredump e com app factory único (decisão O07 mantida).
6. **A07 (regras Firebase)**: escopo por papel/base permanece pendente.

## 7. Arquivos alterados nesta rodada

- `firmware/bici/`: `constants.h`, `buffer_manager.{h,cpp}`, `config_manager.{h,cpp}`, `power_manager.{h,cpp}`, `at_base.{h,cpp}`, `main.cpp`
- `firmware/central/`: `constants.h`, `buffer_manager.{h,cpp}`, `endpoints.{h,cpp}`, `bike_manager.cpp`, `bike_pairing.cpp`, `ble_server.cpp`, `cloud_sync.cpp`, `config_ap.cpp`, `config_credentials.cpp`, `config_manager.cpp`, `main.cpp`
- `bot/functions/src/index.ts`, `bot/src/config/firebase.js`, `bot/src/services/{bikeMonitor,rideCalculator,stationMonitor}.js`
- Verificação: `melhorias/verify-fixes/{run.sh,bici_checks.cpp,central_checks.cpp,verify-bot.js}` + stubs (`LittleFS.h`, `esp_sleep.h`) — **runner estrito** (reprova em `no(BUG)`/sanitizador)
- Contratos documentados: `docs/PROTOCOL_BLE.md` (`idx`/`resumed`, capacidade 512), `docs/DATA_MODEL.md` (remontagem, domínio temporal, replay)
