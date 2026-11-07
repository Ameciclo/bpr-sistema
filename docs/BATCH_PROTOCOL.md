# BPR — Protocolo de transferência de lotes, integridade e recibos (BTP1)

**Versão do protocolo:** 1 (`BTP1`) — implementado conforme
`melhorias/prd_bpr_transferencia_lotes_e_recibos.md`.
**Código de referência:** `firmware/common/batch_proto.h` (contrato, firmware),
`bot/functions/src/batchProto.ts` (contrato, backend), `firmware/central/src/batch_spool.{h,cpp}`
(spool/recibos da central), `firmware/bici/src/{at_base,buffer_manager}.cpp` (bike).

Este documento registra o **contrato de bytes** e as decisões da versão 1.
As duas pontas (firmware C++ e backend TS) concordam por **vetores
compartilhados** verificados no harness (`run.sh`, etapa T17).

## 1. Identidade e unidades

| Entidade | Definição |
|---|---|
| Viagem/ciclo de coleta | Decisão de produto (fora deste protocolo). |
| **Lote de entrega** | Conjunto **imutável** fechado pela bike no início da entrega. v1: **uma sessão = um lote** (identidade = `bike_id + session_id + session_salt`). |
| **Parte** | Uma mensagem de aplicação (o fragmento JSON atual, ≤ 460 B) com índice 0..total-1. |
| Estação | **Proveniência** da entrega (`uploaded_by`, `delivery_bases`), nunca identidade. |

- **Índices: 0..total-1** (convenção declarada nesta versão; os exemplos
  humanos do PRD usavam 1..N — a implementação é 0-based e valida limites).
- Repetir a entrega **conserva** identidade e conteúdo; conteúdo diferente
  com a mesma identidade é **conflito** (nunca substituição silenciosa).
- `bike_id` canônico tem **10 bytes ASCII** (`bpr-XXXXXX`, sem terminador).

## 2. Contrato de bytes do hash do lote

Todos os inteiros em **little-endian**:

```
"BTP1"                4 bytes    versão do formato de lote
bike_id              10 bytes    ASCII "bpr-XXXXXX", SEM terminador
session_id           u32 LE
session_salt         u16 LE
total_parts          u16 LE
total_bytes          u32 LE      soma dos comprimentos das partes
para cada parte i em 0..total_parts-1, EM ORDEM DE ÍNDICE:
  part_index         u16 LE
  part_len           u16 LE
  part_bytes         part_len bytes  (conteúdo exato da parte)
```

**SHA-256 (FIPS 180-4)** sobre esses bytes = `batch_sha256` (32 bytes,
transmitido em hex-64 minúsculo). Horários de contato e proveniência
(`uploaded_by`) ficam FORA do conteúdo congelado.

**CRC por parte:** CRC-32/ISO-HDLC (refletido, poly `0xEDB88320`, init
`0xFFFFFFFF`, xorout `0xFFFFFFFF`) sobre os bytes da parte.

**Bytes da parte** = os bytes UTF-8 exatos da serialização JSON produzida
pelo produtor (fragmento atual), **como transmitidos** no write GATT. A
central e a nuvem nunca re-serializam: a parte é persistida e transportada
como string (RTDB preserva bytes UTF-8) e o CRC/SHA são recomputados sobre
os bytes originais. O campo `resumed` do fragmento final é **congelado no
fechamento do lote** — derivá-lo do boot atual mudaria os bytes após reboot
e invalidaria o hash.

**Decisão v1 sobre o checksum por parte:** o CRC é computado **nas bordas de
persistência** (central ao receber — com releitura de verificação — e nuvem
ao validar) em vez de transmitido dentro da própria parte (auto-referência).
A integridade do link BLE é garantida pelo ATT (write com resposta + MIC do
link); qualquer corrupção remante é detectada pelo CRC da borda e, em última
instância, pelo hash global.

### Vetores compartilhados (T17)

O firmware (C++) e o backend (TS) computam o hash dos mesmos lotes
sintéticos; o `run.sh` compara as saídas e reprova em divergência. Valores
de referência da implementação atual:

| Caso | bike | sid | salt | total | SHA-256 do lote |
|---|---|---|---|---|---|
| 0 | bpr-0aa001 | 4242 | 7 | 1 | `c07430a12ab1c654071835867d3dbc96364722de587703203867449b394cea46` |
| 1 | bpr-0bb002 | 100 | 65535 | 3 | `283e56f77e3a3cc6618294b1766e0bc02daf5346c30c9500e82c88db61e84602` |
| 2 | bpr-0cc003 | 4294967295 | 1 | 56 | `0b55a4aa3e0515e6d77fbdce4defe240ab1488693796e08a283dfbf1f0de12ce` |

Ancoragem do SHA-256 (FIPS 180-4): vetores oficiais "abc" e
"abcdbcde…nopq" verificados no `batch_proto_selftest` (C++) e o CRC
clássico `"123456789"` → `cbf43926` nas duas pontas.

## 3. Mensagens (todas ≤ 460 B de orçamento de aplicação)

| Mensagem | Direção | Campos | Observações |
|---|---|---|---|
| `batch_announce` | bike→central | `v, bike_id, session_id, session_salt, total, total_bytes, sha256` | Fecha o lote (freeze persistido) ANTES de transmitir. |
| `batch_status` | central→bike | `v, bike_id, session_id, session_salt, state, page, pages, missing` | Pendências em **páginas de 64 partes** (16 hex chars) — cabe em MTU pequeno (T16). `missing`: bit 1 = parte presente. Estados: `receiving`, `complete_local`, `awaiting_cloud`, `hash_mismatch`, `conflict`. |
| `data` (parte) | bike→central | fragmento atual (`seq` = índice) | Roteado ao spool do lote anunciado; ACK sem `can_clear`. |
| `data_ack` | central→bike | `stored, seq, conflict` | `conflict: true` = mesmo índice com conteúdo diferente (T03). `can_clear_buffer` é **sempre false** no fluxo de lotes (T20). |
| `batch_clear_auth` | central→bike | `v, bike_id, session_id, session_salt, total, total_bytes, sha256, confirmed_at` | Autorização ligada à confirmação DURÁVEL na nuvem; identifica exatamente o conteúdo liberável. |
| `batch_cleared` | bike→central | `v, bike_id, session_id, session_salt, sha256` | "Recebido e apaguei" — pode ser repetido pelo estado persistido (T12). |
| `busy` | central→bike | `retry_after_sec` | Capacidade/admissão cheia (T15) ou versão não suportada (T20). |

## 4. Estados e ordem das gravações (PRD §5)

Estados na central (`batch_spool.h`): `receiving` → `complete_local` →
`awaiting_cloud` → **recibo** (`confirmed_cloud` na nuvem); `hash_mismatch`
é pendência de diagnóstico (nunca confirma).

- **Central:** confirmação remota → **persistir recibo** → remover payload →
  autorizar (pelo recibo, quando a bike anunciar de novo).
- **Bike:** autorização validada (identidade+total+bytes+hash) →
  **persistir autorização** → remover apenas a cobertura do lote
  (observações pós-lote permanecem — T14) → persistir estado final →
  responder `batch_cleared`.
- A bike **nunca** limpa por `can_clear_buffer` no fluxo de lotes; a
  ausência de resposta ao announce é diagnóstico de incompatibilidade com
  dados preservados (T20).
- Recibo central (`/buffer/receipts/{bike}_{sid}_{salt}.receipt`) sobrevive
  à remoção do payload e repete a autorização indefinidamente (T11);
  a memória do lote limpo na bike (`last_cleared_*`) responde "apaguei" de
  novo, inclusive após reboot (T12).

## 5. Nuvem e consumidores

- A central publica cada parte em
  `bikes/{bike}/batches/{sid}-{salt}/parts/{i}.json` `{len, crc32, payload}`
  (idempotente; upload **progressivo** — 48 partes por ciclo de sync,
  retomável) e por último o `meta` `{v, total, total_bytes, sha256, uploaded_by}`.
- O gatilho `onBatchMetaUploaded` (Functions) valida o conjunto inteiro
  (CRC por parte + hash global) e instala a **conclusão canônica por origem**
  em `bikes/{bike}/batch_confirmations/{sid}-{salt}` — criação exclusiva:
  uploads repetidos/outra estação convergem para o MESMO registro (T10/T18),
  com `sources` acumulando a proveniência.
- A central consulta essa conclusão (por bike, independente de estação),
  grava o recibo local e libera o payload.
- O processamento (normalização → `sessions_v2/{sid}-{salt`) roda do lote
  CONFIRMADO com a mesma remontagem do fluxo legado — a identidade da
  sessão é preservada; geolocalização e agregados continuam posteriores e
  independentes (T19).

## 6. Capacidade, MTU e compatibilidade

- **Admissão serializada:** até 2 lotes em recepção simultâneos
  (`MAX_ADMITTED_BATCHES`), cada um com espaço reservado para o lote
  INTEIRO + margem de 64 KB (T15); anúncios além disso recebem `busy`.
- Pior caso legítimo (50 scans × 20 redes, SSID de 32 aspas): 501 partes ≤
  512 do contrato; o total de partes foi recalculado com os campos deste
  protocolo (PRD §6).
- **MTU:** a bike exige MTU negociado ≥ 471 (parte 460 + overhead) — MTU
  menor é recusa explícita com dados preservados (T16); `batch_status` é
  paginado para caber em MTU pequeno.
- **Compatibilidade (T20):** o caminho legado (fragmentos sem lote
  anunciado → spool antigo + `can_clear_buffer`) permanece para firmware
  antigo; a central nunca interpreta um ACK antigo como autorização do
  lote inteiro. `SESSION_FILE_VERSION` 3→4 (formato v3 nunca gravado em
  placa — bump sem perda de dados reais, R21).

## 7. Diagnóstico mínimo (PRD §8)

Logs de: partes presentes/esperadas e bytes por lote; estado
(receiving/complete/awaiting/hash_mismatch); conflitos de identidade e hash
divergente (com os dois hashes); reenvios e duplicatas; admissões
recusadas (motivo); recibo gravado/consultado; limpeza executada
(cobertura e remanescente); upload progressivo (partes por ciclo).

## 8. Pendências de homologação (fora do host)

Placa: quedas de alimentação em cada ponto de gravação, BLE interrompido,
recuperação do FS, espaço baixo, MTU real, indisponibilidade de internet
prolongada. SDK/emulador real para gravação/concorrência/conclusão remota
(os adaptadores locais implementam a semântica de transação, mas não
substituem o serviço real — PRD §7).
