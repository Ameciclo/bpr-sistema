# Correções aplicadas do PRD de transferência de lotes, integridade e recibos

**Data:** 7 de novembro de 2026.
**Base:** `melhorias/prd_bpr_transferencia_lotes_e_recibos.md` (desenho funcional aprovado) sobre `48b0915`, branch `fixes/november`.
**Escopo desta entrega:** implementação completa do protocolo BTP1 (contrato de bytes, lote congelado na bike, spool/recibos/estados na central, conclusão canônica na nuvem, consumidores) + verificação nativa (matriz de aceite) + builds. **Nenhum teste em placa, deploy ou gravação em placa foi realizado.** O contrato está registrado em `docs/BATCH_PROTOCOL.md`.

## 1. Verificação crítica do pedido

Os problemas do PRD foram confirmados no código base: `can_clear_buffer` era decidido pela completude do **spool local** (a bike apagava com "completo localmente", antes da nuvem); a retomada dependia das partes ainda presentes no buffer (a central que já drenou fazia a bike reenviar para sempre); não havia hash global nem recibo desacoplado do payload; e dois lotes densos excediam o teto global de 800 itens.

## 2. O que foi implementado (plano do PRD §8, em ordem)

| Passo | Implementação |
|---|---|
| **1. Contrato de bytes** | `firmware/common/batch_proto.h` (header-only portátil: SHA-256 FIPS 180-4 streaming, CRC-32/ISO-HDLC, hash input canônico `BTP1` little-endian, índices 0..total-1, `bike_id` 10 B) e `bot/functions/src/batchProto.ts` (espelho exato). **Vetores compartilhados verificados**: FIPS/CRC nas duas pontas + 3 lotes sintéticos comparados C++↔TS no `run.sh` (etapa T17 — divergência reprova o runner). Contrato documentado com os valores de referência em `docs/BATCH_PROTOCOL.md` §2. |
| **2. Bike: lote e snapshot recuperáveis** | `SessionData` v4 (v3 nunca foi em placa — bump sem perda): `freezeBatch()` congela identidade/total/bytes/SHA-256/**resumed** no início da entrega e persiste ANTES de transmitir; novas coletas ficam fora do lote (chunks/serialização usam o subconjunto congelado — bytes idênticos entre reenvios e reboots); `applyClearAuthorization` valida identidade+total+bytes+hash e **persiste a autorização antes de remover**; `performClearIfAuthorized` faz a limpeza cirúrgica (shift das observações pós-lote) e persiste o estado final; memória `last_cleared_*` responde "apaguei" de novo (inclusive após reboot). `at_base.cpp` reescrito: announce → status paginado → envio **só das pendências** → clear_auth; orçamento de visita X02 e MTU check (T16) preservados; sem resposta ao announce = diagnóstico + dados preservados (T20). |
| **3. Nuvem: armazenamento imutável por origem** | A central publica `bikes/{bike}/batches/{sid}-{salt}/parts/{i}.json` (payload = bytes exatos como string; upload **progressivo** 48 partes/sync, retomável pelo bitmap persistido) e por último o `meta`. O gatilho `onBatchMetaUploaded` valida o conjunto inteiro (CRC por parte + SHA global) e instala `bikes/{bike}/batch_confirmations/{sid}-{salt}` por **criação exclusiva** — repetições/outra estação convergem para o MESMO registro, `sources` acumula proveniência (T10/T18). |
| **4. Central: estados, reserva e recibos** | `batch_spool.{h,cpp}`: arquivo por lote com header de estado + bitmap de partes + registros de tamanho fixo (acesso direto por índice); admissão com reserva (lote inteiro + 64 KB de margem, **2 lotes simultâneos** — T15); `addPart` valida índice/comprimento, persiste registro ANTES do bitmap e **releitura confere** a gravação (T06); duplicata idêntica idempotente, conteúdo diferente = conflito (T03); `checkComplete` confere o conjunto exato + hash global — divergência vira `hash_mismatch` (pendência, nunca confirma — T05); **recibo persistente** sobrevive à remoção do payload e repete a autorização (T11). Upload/consulta no `cloud_sync` com falha remota que não derruba a sync (T09). |
| **5. Consumidores** | `assembleFromFragments` extraída e reusada: o lote confirmado produz a MESMA projeção `sessions_v2/{sid}-{salt}` do fluxo legado (identidade preservada, sem cópias); normalização/geolocalização/agregados seguem posteriores e independentes (T19). Caminho legado intacto para firmware antigo (T20). |

## 3. Matriz de aceite — status

| ID | Status | Evidência |
|---|---|---|
| T01 | ✅ host | 56 partes fora de ordem + duplicatas idênticas → completo; duplicata conta uma vez |
| T02 | ✅ host | Parte ausente/truncada → pendente, sem recibo (central + validador TS) |
| T03 | ✅ host | Mesmo índice com conteúdo diferente → `-1` conflito; idêntica → idempotente |
| T04 | ✅ host | Índice fora do intervalo rejeitado; total 513 não admitido; bike_id fora do formato rejeitado |
| T05 | ✅ host | CRCs ok + hash divergente → `hash_mismatch`, sem recibo (central + TS) |
| T06 | ✅ host | Falha de gravação → sem confirmação (bitmap intocado); reenvio aceito |
| T07 | ✅ host | Estado/progresso recuperados do arquivo; announce repetido = retomada (não recria); bike: lote congelado e partes idênticas após "reboot" |
| T08/T18 | ✅ host | Recibo responde sem payload; conclusão consultada por bike (independente de estação) |
| T09 | ✅ design+host | Falha remota não derruba a sync; lote fica `awaiting_cloud`; bike mantém a cópia (check de estado no host; internet real pendente) |
| T10 | ✅ host | Conclusão por criação exclusiva — mesma identidade/hash reutiliza o registro (adaptador transacional) |
| T11 | ✅ host | Autorização repetida pelo recibo após remoção do payload |
| T12 | ✅ host | Memória do lote limpo responde "apaguei" de novo, inclusive após reboot |
| T13 | ✅ host | Ordem das gravações (registro→bitmap, recibo→remoção, autorização→limpeza→estado final) recuperável em cada corte simulado |
| T14 | ✅ host | Coleta pós-freeze fora do lote; partes congeladas não mudam; limpeza preserva o remanescente |
| T15 | ✅ host | 3º lote recusado (`busy`) com 2 em recepção; retomada não consome vaga |
| T16 | ✅ host | `batch_status` paginado (151 B/página) e `clear_auth` (240 B) ≤ 460; bike exige MTU ≥ 471 (recusa com dados preservados) |
| T17 | ✅ host | **C++ ↔ TS concordam** nos vetores (runner reprova em divergência) + FIPS/CRC ancorados |
| T19 | ✅ design | Processamento posterior separado (normalização do lote confirmado; geo/agregados independentes) |
| T20 | ✅ host/build | `v` não suportada → busy; `can_clear_buffer` sempre false no fluxo de lotes; caminho legado intacto |

**Pendente de homologação (PRD §7):** SDK/emulador real para gravação/concorrência/conclusão remota (os adaptadores locais implementam a semântica de transação do SDK, mas não substituem o serviço); placa para quedas de alimentação em cada ponto de gravação, BLE interrompido, recuperação de FS, espaço baixo, MTU real e indisponibilidade de internet prolongada.

## 4. Validação executada

| Verificação | Resultado |
|---|---|
| Build PlatformIO bici | **SUCCESS** — RAM 29,5% (96.660 B); Flash 39,7% (1.247.325 / 3.145.728 B) |
| Build PlatformIO central | **SUCCESS** — RAM 14,0% (45.772 B); Flash 62,7% (1.973.473 / 3.145.728 B) |
| `run.sh` completo (fontes reais + ASan/UBSan + runner estrito) | **Sem `no(BUG)` e sem achado de sanitizador** — incluindo as novas etapas `batch_checks` (central), `bt_*` (bike), `batch backend checks` (TS) e a comparação **T17 C++↔TS** |
| Functions `tsc --noEmit` (build e dev) + `npm run build` | 0 diagnósticos; build OK |
| Functions lint | 0 erros (10 avisos `any` preexistentes em `local-bot.ts`) |
| Sintaxe JS do bot (`node --check`) | OK |

## 5. Contratos novos/alterados (resumo)

- **BTP1** (contrato de bytes completo em `docs/BATCH_PROTOCOL.md`): identidade `bike_id(10 B)+sid+salt`, índices 0-based, SHA-256 global sobre representação little-endian canônica, CRC-32/ISO-HDLC por parte.
- BLE: `batch_announce`, `batch_status` (paginado, estados `receiving/complete_local/awaiting_cloud/hash_mismatch/conflict`), `data_ack.conflict`, `batch_clear_auth`, `batch_cleared`; `can_clear_buffer` sempre false no fluxo de lotes.
- Bike: `SessionData` **v4** (campos de lote + memória de limpeza); lote congelado persistido antes de transmitir.
- Central: `/buffer/batches/` (spool por lote com estados/bitmaps) e `/buffer/receipts/` (recibos duráveis); admissão 2 lotes + margem 64 KB; upload progressivo 48 partes/sync.
- Nuvem: `bikes/{bike}/batches/{sid}-{salt}/{parts/{i}, meta}` (imutável, idempotente) e `bikes/{bike}/batch_confirmations/{sid}-{salt}` (conclusão canônica com `sources`).

## 6. Pendências recomendadas

1. Homologação em placa e com SDK/emulador real (matriz §3 "pendente de homologação").
2. Dimensionar o tempo de upload do lote denso (501 partes × ~2 PUTs/s) contra o ciclo de sync — o progressivo (48/sync) está correto, mas a cadência pode ser ajustada em bancada.
3. R21 segue decidido: nenhuma placa em produção tem formato v3 — o bump v4 não perde dados reais; reflash das placas de protótipo exige o inventário já documentado.
