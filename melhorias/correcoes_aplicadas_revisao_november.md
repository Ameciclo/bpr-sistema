# Correções aplicadas da revisão técnica — fixes/november (R01–R23, A01–A09, O01–O08)

**Data:** 7 de novembro de 2026.
**Base:** `revisao_bpr_fixes_november.md` (revisão do commit `09dc9b1`, branch `fixes/november`).
**Escopo desta entrega:** verificação crítica dos pedidos + correções de código + verificação nativa + builds.
**Nota:** as menções a `Ameciclo/bpr-embarcado` na revisão foram desconsideradas, conforme instrução — é outro repositório, sem relação com este sistema.

---

## 1. Verificação crítica dos pedidos

Todos os achados R01–R23, A01–A09 e O01–O08 foram confrontados com o código antes de qualquer alteração. **Nenhum pedido foi refutado**: os 23 achados de firmware, os 9 do aplicativo e as 8 observações se confirmaram na leitura do código (vários também por reprodução, ver §4). Dois detalhes merecem registro:

- **R11 (callback estático)**: confirmado apenas para o *server callbacks* — na NimBLE-Arduino 1.4.3, `NimBLEServer::setCallbacks(p, deleteCallbacks=true)` destrói o objeto em `deinit(true)` (o `delete` sobre a instância estática é o UB reproduzido com ASan pela revisão). Os callbacks de *característica* não têm esse parâmetro e não são destruídos — o defeito real era só no servidor.
- **R13/proceed**: a revisão afirmou não existir emissor ativo de `proceed`; correto — `UploadQueue::confirmUpload` era o único emissor e é código morto (nenhum chamador). A rota foi removida dos dois lados junto com o código morto.

## 2. O que foi corrigido

### Firmware bici (`firmware/bici/`)

| Achado | Correção |
|---|---|
| **R01 (P0)** | `scanning.cpp`: scan assíncrono iniciado **uma única vez**; enquanto em andamento apenas `scanComplete()` é consultado; `WIFI_SCAN_RUNNING` não é tratado como contagem; prazo global = `scan_timeout_ms`; falha (`WIFI_SCAN_FAILED`) gera diagnóstico e o ciclo seguinte tenta de novo. |
| **R04** | `buffer_manager.cpp`: gravação da sessão em `/session.tmp` + rename; falha de escrita preserva o arquivo anterior. Checkpoint pós-scan e o save antes do deep sleep propagam a falha (log explícito). |
| **R07** | `config_manager.cpp`: política de versão explícita — remota maior aplica e assume a revisão remota; igual+mesmo conteúdo = ACK idempotente; igual+conteúdo divergente = conflito rejeitado; menor = rejeitada. `applyUpdate` só considera "mudança" quando o **valor** difere (antes marcava mudança pela mera presença do campo). |
| **R09** | `save()` retorna `bool`; `processUpdate` aplica em candidata, persiste e só então troca a config ativa e confirma — falha restaura o estado anterior e o ACK sai com erro. |
| **R13** | `at_base.cpp`: contador de retries só zera com **ACK da aplicação** (sucesso de escrita GATT não zera); 3 timeouts sem ACK encerra a tentativa e desconecta; `busy` é respeitado com espera real de `retry_after_sec` antes de nova tentativa; handler legado `proceed` removido. |
| **R16** | `SessionData` ganha `boot_id` (por energização, preservado na sessão recuperada); fragmentos referenciam o scan por **índice** (não por timestamp) — retomada de boot não mistura origens nem depende de unicidade de `millis()`. `SESSION_FILE_VERSION` 2→3 (formato v2 foi criado nesta mesma branch, sem placas em produção — ver R21). |
| **R17** | Política de saturação declarada e diagnosticável: buffer cheio ⇒ scan não executa (economia de rádio), log único de saturação, `isFull()` satisfeito e a busca por base continua; retomada logada quando o buffer é liberado. Rotação multi-sessão permanece como backlog (ver §3). |
| **R20** | `load()` lê em candidata e restaura **padrões reais** em falha (não deixa lixo parcial ser salvo pelo setup). |
| **R22** | Header da sessão ganha `payload_crc`; `load()` valida CRC do payload, terminadores de SSID/BSSID de todas as redes e contagens; arquivo suspeito é **quarentenado** em `/session.corrupt` (não apagado). |
| **A04** | `addBattery()` passou a ser chamado a cada scan coletado (produtor ativo de telemetria na sessão). |
| **O01** | Estimador de bytes conta escapes JSON (aspas/barra/controle) no SSID. |
| **O05** | `dev_mode` padrão **false** (proteção de bateria ativa por padrão; override continua possível por config). |
| **O06** | Estado `LOST` inalcançável removido (código, enum e transições). |

### Firmware central (`firmware/central/`)

| Achado | Correção |
|---|---|
| **R02/R12** | Contrato único de capacidade: `MAX_SESSION_FRAGMENTS=256` (pior caso: 50 scans densos × ~5 chunks + final); `MAX_ITEMS_PER_BIKE=256`; `MAX_TOTAL_ITEMS=800` (orçamento de flash ~429 KB de payload na partição de 960 KB). `BufferManager` reescrito em **streaming de registros de tamanho fixo** (nada carrega a sessão inteira em RAM); `hasCompleteSession` limita `seq` pelo array **e** pelo total — o caso seq 10/total 11 roda sob ASan/UBSan sem acesso fora dos limites. |
| **R03** | `addBikeData` não muta mais o conteúdo da bike: o horário de recepção ficou como metadado do item (`timestamp`), fora do CRC — reenvio idêntico produz o mesmo CRC e cai no dedupe idempotente (bike, sessão, salt, seq, CRC). |
| **R04** | Gravação por bike: append de registro + header atualizado **depois** (crash no meio deixa o arquivo consistente no estado anterior); reescritas (confirmação) usam temporário + rename; falha preserva o original. |
| **R05** | Lote com **manifesto durável** (`/buffer/batch-{id}.manifest`) gravado antes de marcar itens; confirmação por lote remove **exatamente** os itens do manifesto; `rollbackUpload(batchId)` devolve só os itens daquele lote a pendente; `begin()` reconcile lotes inconclusos de boots anteriores. |
| **R06** | Chave do lote = CRC32 da identidade ordenada dos itens + base_id — **sem horário**; retry do mesmo conjunto produz a mesma chave (a nuvem deduplica por ela). |
| **R07** | `downloadSingleBikeConfig` passa a versão remota ao aplicar (o bug "baixa 42, grava 0" foi corrigido); versão zero não sobrescreve versão local conhecida; corpo `null` = ausência (não é zero). |
| **R08** | Comparação de status por **conteúdo** (`String == "ok"`, não endereço de literal); `markConfigAcked` só encerra a pendência com a **versão esperada**; ACK divergente/zero mantém a pendência com erro visível. |
| **R10** | `tryLock()` com resultado obrigatório: quem não adquire não executa a região; `markAsConfirmed` (o vazamento reproduzido pela revisão) e todos os caminhos liberam o lock. |
| **R11** | `pServer->setCallbacks(&instância, false)` — a biblioteca não destrói mais o objeto estático em `deinit(true)`. |
| **R13** | `createBusyResponse(bike_id, retry_after_sec)` endereçada e correlacionada; admissão de dados vinculada ao **estado real** (`STATE_BIKE_PAIRING`), não só ao prazo de busy; rota legada `proceed` removida (helper + `UploadQueue` morto + `BPRStringHelper` morto excluídos). |
| **R14** | `isConfigValidFor` valida **todos** os divisores de LED (`ble_ms`, `sync_ms`, `error_ms`, `count_ms`, `count_pause_ms`, `bike_arrived_ms`, `bike_left_ms`) e faixas; `led_controller` tem guarda defensiva contra ciclo zero. |
| **R15** | Pendentes publicados: `getPendingBikesForUpload` sem o filtro que descartava descobertas (agora publica todas) e chamado no sync (`uploadPendingBikes` → `/bases/{id}/pending_bikes.json`); merge do registry **remove pendente mais antigo** para abrir slot a um ALLOWED remoto e **retorna false** quando não consegue aplicar; revogação por ausência: ALLOWED local fora de um snapshot válido e não-vazio vira BLOCKED com log (registry vazio = "sem decisão", não revoga). |
| **R18** | Identidade fixada à conexão: troca de ID na mesma conexão é recusada; segunda conexão reivindicando ID já ocupado é recusada; escritas de configuração exigem handle identificado, ID compatível e **mesma autorização** do caminho de dados (`canConnect`). |
| **R19** | Callbacks BLE só **enfileiram** eventos (fila FreeRTOS limitada = backpressure); todo o processamento (parsing, gestão, persistência, pushes) roda no loop; mapas de conexão protegidos por mutex; busy por prazo não reabre recepção fora do estado de pairing. |
| **R20** | `loadConfig` lê em candidata e restaura padrões reais; `updateFromCSV` exige CSV completo (32 campos), valida candidata (R14) e só aplica após **persistir** (retorno de `saveConfig` verificado); sem versão remota válida a config não é aplicada (fim do fallback para `millis()`); `downloadCentralConfig` falha quando a aplicação falha; corpo `null` tratado como ausência em config, versão de config e versões por bike; portal (`config_ap`) aplica em **cópia candidata** e só commita após validar e salvar. |
| **R23** | Central chama `NimBLEDevice::setMTU(512)` em runtime (a flag de build é sobrescrita pelo sdkconfig do framework para 256 — confirmado no fonte da biblioteca); `sendConfigToHandle` consulta o **MTU negociado** (`getPeerMTU`) e recusa explicitamente envelope maior que MTU−3 em vez de notificar truncado como sucesso. |
| **O02** | URLs com `?auth=` nunca mais são logadas (`redactUrl` em todos os pontos de log de erro HTTP). |
| **O03** | Fluxo morto removido (`addConfigData`/`saveSystemHeartbeat`/`addConfigFields`); `addData` só aceita IDs canônicos `bpr-*`. |
| **O04** | Backup passou a ser **cópia bruta por arquivo** com verificação de bytes (antes consolidava num documento JSON finito sem checagem); retenção pelos 3 mais recentes. |
| **O06** | `getPendingBytes/Sessions/BufferUsage` agora retornam valores reais do BufferManager (streaming). |

### Bot, Functions e infra do aplicativo

| Achado | Correção |
|---|---|
| **A01 (parcial)** | **Normalizador implementado** (o consumidor que faltava): trigger `onBatchUploaded` em `/bases/{baseId}/data/{batchId}` (o caminho que o firmware grava) → armazena fragmentos por `seq` em `bikes/{id}/raw_fragments/{sid}-{salt}/{seq}` (idempotente) → quando todos os fragmentos 0..total−1 existem, remonta a sessão (scans ordenados por ts+offset, bateria do fragmento final) e publica a projeção versionada em `bikes/{id}/sessions_v2/{sid}-{salt}` com `source_batches` auditável. `onSessionAssembled` notifica o admin. |
| **A02** | DTO interno único (`NetworkDto`/`normalizeNetwork`/`normalizeScan`) em `geolocation.js`, aceitando array do firmware e objeto v2; `formatWifiData`, `calculateRoute`, `handleNewScan`, `getLastRoute` e o conversor das Functions todos consomem o DTO — o caso "bot entrega objetos e a API lê índices 1/2/3" não existe mais. |
| **A03** | `stationMonitor`: heartbeat em segundos comparado em ms; `timestamp_valid=false` = estado **desconhecido** (não inventa conectividade); ausência de evidência não é "online" (painel mostra "Sem evidência"). |
| **A04** | Contrato de bateria separado: `{ts_ms, percent, voltage, charging}`; consumidor usa **percentual direto** quando existe e só converte tensão como fallback explícito; projetado também nas sessões v2. |
| **A05 (parcial)** | Mensagens distinguem evento de ingestão (marcador "dado histórico" para scans antigos); registro de listeners por sessão (sem acumular); viagem salva por `rideId` com verificação de existência (replay não duplica). Dedupe persistente de notificações entre reboots do bot permanece como backlog (§3). |
| **A06** | Os 9 diagnósticos TypeScript corrigidos (erros `unknown` do axios com `isAxiosError`, casts de `Object.entries`, remoção da referência inexistente a `GeolocationService` e do handler duplicado de `/converter`); `local*.ts` documentados como entrypoints de dev (fora do build, com `tsconfig.dev.json` + `dotenv` devDependency); scripts da raiz que apontavam para `./scripts/*.sh` inexistentes substituídos por comandos existentes (`build:bot`, `check:functions`, `build:firmware:*`). |
| **A07** | `database.rules.json`: escrita em `bikes`/`bases`/`rides`/`public_stats` exige `auth != null`; `subscriptions` só o próprio usuário; leitura pública mantida onde o produto já expunha. O firmware não é afetado (database secret = acesso administrativo). |
| **A08** | Unidades normalizadas (`toMs`: segundos↔millis em um único auxiliar); duração coerente; métricas e estatísticas via **transação** do RTDB; resumo mensal com **chave de período** (`public_stats/monthly/YYYY-MM`) + `current_month` explícito; CO₂ e distância rotulados como **estimativa** (método `co2_method` publicado, hipótese de substituição por carro declarada). |
| **A09 (parcial)** | Webhook valida `X-Telegram-Bot-Api-Secret-Token` quando configurado; `/converter` com resfriamento por usuário (API paga); publicações de movimento em **região aproximada** (3 casas decimais) e sem "está rodando agora" para dados históricos. Matriz completa de acesso por papel permanece como decisão (§3). |

## 3. O que NÃO foi feito (e por quê)

- **R21 (migração)** — decisão/plano, não código: não há inventário das placas instaladas. O plano exigido: (1) levantar partição/IDs/conteúdo de cada placa; (2) exportar pendências pela serial/USB antes de qualquer reflash; (3) tratar formatação como recuperação deliberada. Nota: o formato de sessão v2 desta branch nunca foi gravado em placa em produção (foi criado no commit auditado), por isso o bump 2→3 não perde dados reais.
- **A01 (dashboard)** — o normalizador fecha o ciclo firmware→nuvem→consumidor; o painel Remix (`web/`) permanece estrutura inicial (rotas, autenticação e consulta Firebase continuam por fazer).
- **A05 (replay total do bot)** — notificações entre reboots ainda dependem de dedupe em RAM; a viagem é idempotente, os alertas não.
- **A09 (matriz de acesso)** — webhook e limites implementados; política por papel (operador × visitante × dispositivo) segue como decisão de produto, junto com F21.
- **O05 (energia)** — `dev_mode` padrão corrigido e retry sem fim eliminado (R13), mas ADC/carga/autonomia exigem bancada.
- **O07 (OTA)** — a partição continua com app único (manutenção USB); OTA exigiria dois slots e decisão de capacidade.
- **Testes em placa** — tudo abaixo é host/build; rádio, flash física e FreeRTOS reais seguem pendentes (a própria revisão exige aceite em placa para F24/F25 e cenários §7.4).

## 4. Validação executada

| Verificação | Resultado |
|---|---|
| Build PlatformIO bici (espressif32 8.0.0, ArduinoJson 6.21.6, NimBLE 1.4.3) | **SUCCESS** — RAM 29,5%, Flash 39,2% (1.234.259 / 3.145.728 B) |
| Build PlatformIO central | **SUCCESS** — RAM 14,0%, Flash 60,7% (1.909.015 / 3.145.728 B) |
| Functions `tsc --noEmit` (build e dev) | **0 diagnósticos** (antes: 9 erros) |
| Functions `npm run lint` | **0 erros** (antes: 87 problemas no index.ts original; restam 10 avisos `any` em `local-bot.ts`, utilitário de dev) |
| `npm run build` (Functions) | sucesso |
| Sintaxe de todos os fontes JS do bot (`node --check`) | OK |
| Verificação nativa (ver §5) | **53/53 checagens sem “no(BUG)”**, zero achados de ASan/UBSan |

## 5. Verificação nativa (evidência reproduzível)

`melhorias/verify-fixes/run.sh` compila os **fontes reais** de firmware com stubs de host e `-fsanitize=address,undefined`. Casos que reproduzem diretamente as falhas registradas na revisão (coluna “antes” = resultado da revisão):

| Caso | Antes (revisão) | Agora |
|---|---|---|
| R01 — scan assíncrono com 3 redes | `scan_async=1 collected=0` | 4 loops → 1 registro de scan com 3 redes |
| R02 — 10 scans/1 rede ⇒ 11 fragmentos | `eleventh_fragment_stored=0 complete=0` | 11/11 armazenados, sessão completa |
| R03 — mesmo fragmento 6 s depois | `persisted_items=2` | 1 item (reenvio idempotente) |
| R04 — escrita curta | `previously_acked_remaining=0` | falha reportada e arquivo anterior íntegro |
| R05 — reboot com lote preparado | `next_boot_upload_candidates=0` | reconciliação no boot → itens voltam a pendente |
| R05 — confirmar | remoção global | remove exatamente os 4 do lote; extra sobrevive; manifesto removido |
| R06 — mesma identidade após retry | `same_batch_key_after_retry=0` | mesma chave |
| R07 — reaplicar mesma versão | `applied_version=43` | idempotente (42); conflito e versão antiga rejeitados |
| R09 — config com falha de escrita | `config_update_success=1 persistence_valid=0` | ACK negado; RAM e arquivo preservam o anterior |
| R10 — confirmar e travar | `mutex_still_taken_after_confirm=1` | lock liberado |
| R12 — seq 10/total 11 | ASan/UBSan: fora de `bool[10]` | rejeitado sem acesso fora dos limites (sob ASan/UBSan) |
| R14 — divisores zero | `zero_led_durations_accepted=1` | rejeitados |
| R20 — CSV incompleto / sem versão | aceito | rejeitados; versão válida aplicada com `last_update=42` |
| R22 — 1 byte corrompido no payload | não detectado | load rejeita por CRC e quarentena em `/session.corrupt` |
| R17 — 60 scans | recusa silenciosa | 50 aceitos + saturação diagnosticada (`isFull`) |

R11, R13, R18, R19 e R23 foram corrigidos por leitura/inspeção da API 1.4.3 (o contrato de `setCallbacks`/`deinit`/`setMTU`/`getPeerMTU` foi conferido no fonte da biblioteca em cache) e cobertos pelo build; exigem placa para aceite final.

## 6. Contratos novos/alterados (resumo para revisão)

- Sessão da bike: `SessionFileHeader` com `payload_crc` (versão 3); `SessionData.boot_id`; fragmentos identificam scan por índice.
- Envelope do fragmento ganha `boot_id`; `data_ack`/`busy`/`config` sempre com `bike_id` e correlação.
- Lote da central: `batch-{crc da identidade}.manifest` + itens com `session_salt`; confirmação por lote.
- Capacidade: 256 fragmentos/bike, 800 itens totais (flash), batches de até 4 itens / ~3,4 KB.
- Nuvem: `/bases/{id}/data/{batchId}` (existente) agora consumido; novo `/bases/{id}/pending_bikes.json`; projeção `bikes/{id}/sessions_v2/{sid}-{salt}` com `schema_version: 2`, `ts_ms`/`session_start_ms` em millis, `battery: [{ts_ms, percent}]`, `networks: [{ssid,bssid,rssi,channel}]`.
- Rules do RTDB: escrita autenticada; leitura pública apenas onde já era pública.

## 7. Pendências recomendadas (ordem)

1. Aceite em placa dos itens §7.4 da revisão (rádio, corte de energia, MTU observado, ADC).
2. R21: inventário + exportação antes de reflash das placas existentes.
3. Dashboard (`web/`) consumindo `sessions_v2` + autenticação.
4. Dedupe persistente de notificações do bot (replay total A05).
5. Decisões de produto: matriz de acesso (A09/F21), OTA × USB (O07), rotação de sessões além de 50 scans (R17), cenário de CO₂ (A08).
