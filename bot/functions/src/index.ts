/*
 * BPR Sistema - Bot Telegram Functions
 * Copyright (C) 2024 BPR Sistema Contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

import * as functions from "firebase-functions";
import * as admin from "firebase-admin";
import {Telegraf} from "telegraf";
import axios, {isAxiosError} from "axios";

// Inicialização do Firebase Admin
if (!admin.apps.length) {
  admin.initializeApp();
}

const db = admin.database();

const BOT_TOKEN = functions.config().telegram?.bot_token || "";
const ADMIN_CHAT_ID = functions.config().admin?.chat_id || "";
const GOOGLE_API_KEY = functions.config().google?.api_key || "";

console.log("🔧 Configurações carregadas:", {
  botToken: BOT_TOKEN ? "✅" : "❌",
  adminChatId: ADMIN_CHAT_ID ? "✅" : "❌",
  googleApiKey: GOOGLE_API_KEY ? "✅" : "❌",
});

const bot = new Telegraf(BOT_TOKEN);

// === Contratos de dados (A01/A02/A04) =====================================
// DTO interno de rede WiFi: fonte única em ./dto (compartilhado com assembly)
import {NetworkDto, normalizeNetwork} from "./dto";
export {NetworkDto, normalizeNetwork};

// Itens 2/4/5: montagem/enriquecimento — lógica pura testável em ./assembly
import {
  AssembledScan,
  GeoEntry,
  GeoResolution,
  ScanChunk,
  canInstallGeo,
  hashNetworks,
  mergeLegacyExtras,
  reassembleScans,
  resolveGeo,
  scanKey,
} from "./assembly";

// PRD lotes/recibos: contrato de bytes do protocolo de lotes (BTP1)
import {
  BATCH_PROTO_VERSION,
  BatchMeta,
  BatchPartRecord,
  validateBatch,
} from "./batchProto";

// Sessão tocada por um lote (aguarda completude)
interface TouchedSession {
  bikeId: string;
  sessionId: number;
  salt: number;
  total: number;
}

// Registro de fragmento bruto em /bikes/{id}/raw_fragments/{sid}-{salt}/{seq}
interface FragmentRecord {
  crc32?: string;
  received_ts?: number;
  payload?: Fragment;
}

// Sessão v2 durante conversão/leitura de coordenadas (geo separado — item 5)
interface V2GeoSessionRow {
  scans?: (AssembledScan & {coordinates?: unknown})[];
  geo?: Record<string, GeoEntry>;
}

// Sessão v2 resumida (comando /status)
interface V2StatusRow {
  session_start_ms?: number;
  scans?: unknown[];
  complete?: boolean;
  ingest_ts?: number;
}

// Sessão v2 recém-montada (notificação)
interface V2AssembledRow {
  session_start_ms?: number;
  scan_count?: number;
}

/**
 * Compara chaves de sessão "{sid}-{salt}" NUMERICAMENTE (N10).
 * A ordenação lexicográfica coloca "9999-1" DEPOIS de "10000-1" e
 * selecionava a sessão errada como "mais recente".
 * @param {string} a Primeira chave
 * @param {string} b Segunda chave
 * @return {number} Negativo se a < b, positivo se a > b
 */
function sessionKeyComparator(a: string, b: string): number {
  const pa = a.split("-");
  const pb = b.split("-");
  const sidA = Number(pa[0]) || 0;
  const sidB = Number(pb[0]) || 0;
  if (sidA !== sidB) return sidA - sidB;
  return (Number(pa[1]) || 0) - (Number(pb[1]) || 0);
}

/**
 * Normaliza uma rede WiFi para o DTO interno.
 * @param {unknown} net Rede no formato array ou objeto
 * @return {NetworkDto} DTO normalizado
 */
// Fragmento BLE produzido pela bike (data_decoded do lote da central)
interface Fragment {
  session_id: number;
  session_salt: number;
  seq: number;
  total: number;
  is_final: boolean;
  boot_id?: number;
  session_start_millis?: number;
  scan_count?: number;
  resumed?: boolean; // N04: sessão retomada entre boots
  scans?: ScanChunk[]; // chunks de observação (item 5: montagem em ./assembly)
  battery?: unknown[][];
}

// Item do lote gravado pela central em /bases/{baseId}/data/{batchId}
interface BatchItem {
  bike_id: string;
  ts: number;
  size: number;
  crc32: string;
  session_id: number;
  session_salt: number;
  seq: number;
  data_decoded?: Fragment;
  data?: string;
}

/**
 * Extrai o fragmento do item do lote (decodificado ou string JSON).
 * @param {BatchItem} item Item do lote gravado pela central
 * @return {Fragment|null} Fragmento válido ou null
 */
function parseFragment(item: BatchItem): Fragment | null {
  if (item.data_decoded && typeof item.data_decoded === "object") {
    return item.data_decoded;
  }
  if (typeof item.data === "string" && item.data.length > 0) {
    try {
      return JSON.parse(item.data) as Fragment;
    } catch {
      return null;
    }
  }
  return null;
}

// === A01: normalizador de lotes ===========================================
// O firmware grava em /bases/{baseId}/data/{batchId}; este trigger reconstitui
// as sessões por (bike, session_id, salt), valida completude por seq/total e
// publica a projeção versionada em /bikes/{bikeId}/sessions_v2/{sid}-{salt}.
// Idempotente por construção: fragmento é gravado por seq (reescrita = mesmo
// valor) e a sessão só é publicada quando todos os fragmentos existem.
export const onBatchUploaded = functions.database
  .ref("/bases/{baseId}/data/{batchId}")
  .onWrite(async (change, context) => {
    const batchId = context.params.batchId as string;
    const batch = change.after.val() as {data?: BatchItem[]} | null;

    if (!batch || !Array.isArray(batch.data)) {
      console.log(`Lote ${batchId} vazio/removido - nada a normalizar`);
      return;
    }

    // 1. Grava os fragmentos brutos por seq (idempotente) e coleta as
    //    sessões tocadas por este lote
    const updates: {[path: string]: unknown} = {};
    const seen = new Set<string>();
    const touched = new Map<string, TouchedSession>();

    for (const raw of batch.data) {
      const item = raw as BatchItem;
      if (!item.bike_id) continue;
      const frag = parseFragment(item);
      if (!frag || typeof frag.seq !== "number" ||
          typeof frag.total !== "number") {
        console.log(`Item inválido no lote ${batchId}: seq=${item.seq}`);
        continue;
      }
      if (frag.total <= 0 || frag.total > 512) {
        console.log(`Total fora de faixa no lote ${batchId}: ${frag.total}`);
        continue;
      }
      if (frag.is_final !== true && !Array.isArray(frag.scans)) {
        continue;
      }

      const skey = `${item.bike_id}|${frag.session_id}|${frag.session_salt}`;
      const fragKey = `${skey}|${frag.seq}`;
      if (seen.has(fragKey)) continue; // dedupe dentro do lote
      seen.add(fragKey);

      const fragPath = `bikes/${item.bike_id}/raw_fragments/` +
        `${frag.session_id}-${frag.session_salt}/${frag.seq}`;
      updates[fragPath] = {
        crc32: item.crc32,
        received_ts: item.ts,
        payload: frag,
      };
      touched.set(skey, {
        bikeId: item.bike_id,
        sessionId: frag.session_id,
        salt: frag.session_salt,
        total: frag.total,
      });
    }

    if (Object.keys(updates).length > 0) {
      await db.ref().update(updates);
      const n = Object.keys(updates).length;
      console.log(`Lote ${batchId}: ${n} fragmentos armazenados`);
    }

    // 2. Sessões completas -> projeção normalizada
    for (const t of touched.values()) {
      await assembleSessionIfComplete(
        t.bikeId, t.sessionId, t.salt, t.total, batchId);
    }
  });

/**
 * Remonta a sessão se todos os fragmentos 0..total-1 existirem.
 * @param {string} bikeId ID canônico da bike (bpr-XXXXXX)
 * @param {number} sessionId ID da sessão (millis de início no firmware)
 * @param {number} salt sal da sessão (desambigua reboots)
 * @param {number} total total de fragmentos declarado pela bike
 * @param {string} sourceBatch lote que trouxe o último fragmento
 */
async function assembleSessionIfComplete(
  bikeId: string,
  sessionId: number,
  salt: number,
  total: number,
  sourceBatch: string
): Promise<void> {
  const fragsRef =
    db.ref(`bikes/${bikeId}/raw_fragments/${sessionId}-${salt}`);
  const snap = await fragsRef.once("value");
  const frags = (snap.val() ?? {}) as {[seq: string]: FragmentRecord};

  const bySeq = new Map<number, Fragment>();
  for (const [key, value] of Object.entries(frags)) {
    const seq = Number(key);
    if (!Number.isInteger(seq) || seq < 0) continue;
    if (seq >= total || !value?.payload) continue;
    bySeq.set(seq, value.payload);
  }

  if (bySeq.size < total) {
    console.log(
      `Sessão ${sessionId}-${salt} de ${bikeId}: ` +
      `${bySeq.size}/${total} fragmentos - aguardando`);
    return;
  }

  await assembleFromFragments(
    bikeId, sessionId, salt, total, bySeq, sourceBatch);
}

/**
 * Publica a projeção v2 a partir dos fragmentos já completos — usada pelo
 * fluxo legado (raw_fragments) e pelo protocolo de lotes (BTP1), que faz a
 // MESMA identidade convergir para a MESMA sessão (T10: sem cópias).
 * @param {string} bikeId ID canônico da bike
 * @param {number} sessionId ID da sessão
 * @param {number} salt sal da sessão
 * @param {number} total total de fragmentos
 * @param {Map<number, Fragment>} bySeq fragmentos por seq (0..total-1)
 * @param {string} sourceLabel origem auditável (lote/base)
 * @return {Promise<void>}
 */
async function assembleFromFragments(
  bikeId: string,
  sessionId: number,
  salt: number,
  total: number,
  bySeq: Map<number, Fragment>,
  sourceLabel: string
): Promise<void> {
  const finalFrag = bySeq.get(total - 1);
  if (!finalFrag) return;

  // N05/item 5: os chunks de um MESMO scan são REUNIDOS em uma única
  // observação (agrupamento por scan_key = idx do produtor, fallback ts);
  // redes ordenadas por net_offset e concatenadas. Antes, um scan denso
  // dividido em N fragments virava N scans incompletos. A montagem vive em
  // ./assembly (pura, testável) e grava scan_key + content_hash por scan.
  const chunksBySeq: ScanChunk[][] = [];
  for (let s = 0; s < total - 1; s++) {
    const frag = bySeq.get(s);
    if (!frag) continue;
    chunksBySeq.push(frag.scans ?? []);
  }
  const scans: AssembledScan[] = reassembleScans(chunksBySeq);

  // A04: bateria como {ts_ms, percent} — sem ambiguidade de unidade
  const battery = (finalFrag.battery ?? []).map((entry) => ({
    ts_ms: Number(Array.isArray(entry) ? entry[0] : 0),
    percent: Number(Array.isArray(entry) ? entry[1] : 0),
  }));

  const sessionRef =
    db.ref(`bikes/${bikeId}/sessions_v2/${sessionId}-${salt}`);

  // Item 5 (GEO-02): o normalizador escreve APENAS os campos-base em
  // `scans`; o enriquecimento canônico vive em `geo/{scan_key}` (path do
  // conversor) — escritores disjuntos não se sobrescrevem. Extras legado
  // embutidos (ex.: `coordinates` do conversor antigo) são preservados por
  // compat de leitura, com guarda de conteúdo (mesma ts), sem virar fonte
  // de verdade editável.
  const existingSnap = await sessionRef.child("scans").once("value");
  const mergedScans = mergeLegacyExtras(scans, existingSnap.val());

  await sessionRef.update({
    schema_version: 2,
    bike_id: bikeId,
    session_id: sessionId,
    session_salt: salt,
    boot_id: finalFrag.boot_id ?? null,
    resumed: finalFrag.resumed ?? false, // N04: sessão retomada entre boots
    session_start_ms: finalFrag.session_start_millis ?? null,
    scan_count: finalFrag.scan_count ?? scans.length,
    scans: mergedScans,
    battery,
    complete: true,
    ingest_ts: Math.floor(Date.now() / 1000),
  });
  // Registra a origem sem sobrescrever as anteriores (replay auditável)
  await sessionRef.child(`source_batches/${sourceLabel}`).set(true);

  const nscans = scans.length;
  console.log(`✅ Sessão ${sessionId}-${salt} de ${bikeId} ` +
    `(${nscans} observações reunidas)`);
}

// === PRD lotes/recibos: conclusão canônica do lote (BTP1) ==================
// A central publica as partes (por índice, idempotentes) e por último o meta.
// Este gatilho valida o conjunto INTEIRO contra o contrato de bytes (CRC por
// parte + SHA-256 global) e só então instala a confirmação canônica por
// ORIGEM — independente da estação que fez o upload (T10/T17/T18). Um
// pedido isolado bem-sucedido não representa a entrega completa (PRD §3).
export const onBatchMetaUploaded = functions.database
  .ref("/bikes/{bikeId}/batches/{batchId}/meta")
  .onWrite(async (change, context) => {
    const bikeId = context.params.bikeId as string;
    const batchId = context.params.batchId as string; // "{sid}-{salt}"
    const meta = change.after.val() as BatchMeta | null;

    if (!meta) return; // meta removido — nada a concluir
    if (meta.v !== BATCH_PROTO_VERSION) {
      console.log("Meta " + bikeId + "/" + batchId +
          ": versão " + meta.v + " não suportada");
      return;
    }

    const partsSnap =
      await db.ref(`bikes/${bikeId}/batches/${batchId}/parts`).once("value");
    const parts = (partsSnap.val() ?? {}) as
      {[index: string]: BatchPartRecord};

    const validation = validateBatch(meta, parts);
    if (!validation.valid) {
      // T02/T04/T05: pendência explícita — nada é confirmado com partes
      // ausentes/corrompidas ou hash divergente; o meta pode ser reescrito
      console.log(`Lote ${bikeId}/${batchId} PENDENTE: ${validation.reason}`);
      return;
    }

    // Conclusão canônica por origem — criação exclusiva (T10: duas
    // instâncias convergem para o MESMO registro; mesmos ID/hash reutilizam)
    const confirmRef =
      db.ref(`bikes/${bikeId}/batch_confirmations/${batchId}`);
    const confirmedAt = Date.now();
    const result = await confirmRef.transaction((current) => {
      if (current !== null) return undefined; // já concluído — aborta
      return {
        state: "confirmed",
        bike_id: bikeId,
        total: Number(meta.total),
        total_bytes: Number(meta.total_bytes),
        sha256: String(meta.sha256).toLowerCase(),
        confirmed_at: confirmedAt,
        sources: {[String(meta.uploaded_by ?? "unknown")]: true},
      };
    });

    if (!result.committed) {
      // Já existia: mesmo hash reutiliza a conclusão; divergente é conflito
      const existingSnap = await confirmRef.once("value");
      const existing = existingSnap.val() as {sha256?: string} | null;
      if (existing && String(existing.sha256).toLowerCase() !==
          String(meta.sha256).toLowerCase()) {
        console.log("🚨 Hash divergente na conclusão existente " +
            `de ${bikeId}/${batchId} — canônica mantida`);
      }
      // Proveniência de outra estação é acrescentada sem trocar a canônica
      if (meta.uploaded_by) {
        await confirmRef.child(`sources/${meta.uploaded_by}`).set(true);
      }
    }

    // Processa o conjunto CONFIRMADO preservando a identidade existente
    // (PRD passo 5): as partes são os fragmentos do produtor — a mesma
    // remontagem do fluxo legado, mesma chave sessions_v2/{sid}-{salt}
    const [sidStr, saltStr] = batchId.split("-");
    const sessionId = Number(sidStr);
    const salt = Number(saltStr);
    const total = Number(meta.total);
    if (!Number.isInteger(sessionId) || !Number.isInteger(salt) ||
        sessionId <= 0 || salt <= 0) {
      console.log(`batchId ${batchId} fora do formato — sessão não montada`);
      return;
    }

    const bySeq = new Map<number, Fragment>();
    for (let i = 0; i < total; i++) {
      const rec = parts[String(i)];
      if (!rec || typeof rec.payload !== "string") continue;
      try {
        bySeq.set(i, JSON.parse(rec.payload) as Fragment);
      } catch {
        console.log(`Parte ${i} de ${bikeId}/${batchId} com payload ilegível`);
        return;
      }
    }
    if (bySeq.size < total) {
      console.log(`Lote ${bikeId}/${batchId} sem partes legíveis ` +
          "após validar — inconsistência");
      return;
    }
    await assembleFromFragments(bikeId, sessionId, salt, total, bySeq,
      `batch:${batchId}`);
  });

// Notificação de sessão completa (projeção v2)
export const onSessionAssembled = functions.database
  .ref("/bikes/{bikeId}/sessions_v2/{sessionKey}")
  .onCreate(async (snapshot, context) => {
    const bikeId = context.params.bikeId as string;
    const sessionKey = context.params.sessionKey as string;
    const session = snapshot.val() as V2AssembledRow;

    console.log(`📝 Sessão completa: ${bikeId}/${sessionKey}`);

    // N04: session_start_ms é MILLIS DESDE O BOOT da bike, não Unix time —
    // exibir como data absoluta inventava horário; permanece relativo
    const startLabel = session.session_start_ms ?
      `relativo ao boot da bike (${session.session_start_ms} ms)` :
      "origem desconhecida";

    const message = `🚴 Bike ${bikeId.toUpperCase()}\n` +
      "🎆 SESSÃO NORMALIZADA\n" +
      `📅 ${startLabel}\n` +
      `🏷️ Sessão: ${sessionKey}\n` +
      `📡 Scans: ${session.scan_count ?? 0}`;

    await sendNotification(message);
  });

/**
 * Distância haversine em metros entre dois pontos.
 * @param {number} lat1 Latitude do ponto 1
 * @param {number} lng1 Longitude do ponto 1
 * @param {number} lat2 Latitude do ponto 2
 * @param {number} lng2 Longitude do ponto 2
 * @return {number} Distância em metros
 */
function calculateGPSDistance(
  lat1: number, lng1: number, lat2: number, lng2: number): number {
  const R = 6371000; // Raio da Terra em metros
  const dLat = (lat2 - lat1) * Math.PI / 180;
  const dLng = (lng2 - lng1) * Math.PI / 180;
  const a = Math.sin(dLat/2) * Math.sin(dLat/2) +
            Math.cos(lat1 * Math.PI / 180) * Math.cos(lat2 * Math.PI / 180) *
            Math.sin(dLng/2) * Math.sin(dLng/2);
  const c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1-a));
  return R * c;
}

/**
 * Converte redes WiFi em coordenadas (A02: aceita os dois formatos).
 * @param {unknown[]} networks Redes no formato array ou objeto
 * @return {Promise<{lat: number, lng: number, accuracy: number}|null>}
 */
async function convertWifiToCoordinates(networks: unknown[]) {
  try {
    const wifiAccessPoints = networks.map((net) => {
      const dto = normalizeNetwork(net);
      return {
        macAddress: dto.bssid,
        signalStrength: dto.rssi,
        channel: dto.channel,
      };
    });

    const response = await axios.post(
      `https://www.googleapis.com/geolocation/v1/geolocate?key=${GOOGLE_API_KEY}`,
      {wifiAccessPoints}
    );

    if (response.status === 200 && response.data.location) {
      return {
        lat: response.data.location.lat,
        lng: response.data.location.lng,
        accuracy: response.data.accuracy,
      };
    }
  } catch (error) {
    // A06: erro tipado (axios) em vez de acessar campos de unknown
    if (isAxiosError(error)) {
      const detail = error.response?.data ?? error.message;
      console.log("Erro na geolocalização:", detail);
    } else {
      console.log("Erro na geolocalização:", error);
    }
  }
  return null;
}

/**
 * Processa observações da projeção v2 e grava o enriquecimento geográfico
 * (API paga) no destino PRÓPRIO `geo/{scan_key}` (item 5/M06).
 *
 * GEO-02: escritores disjuntos — o normalizador escreve `scans` (base),
 * este conversor escreve `geo/`; nenhuma lista é substituída por cópia antiga.
 * GEO-03/04: o resultado é vinculado ao `content_hash` exato que foi
 * geolocalizado; DEPOIS da consulta externa (demorada) o scan é RE-LIDO e o
 * hash re-conferido antes de instalar — resultado de revisão antiga não é
 * atribuído à observação nova (fica como "stale" e é recalculado).
 * GEO-05: falha/indisponibilidade não apaga nada — a observação permanece
 * com seus dados Wi-Fi e estado pendente (ausência explícita).
 * A chamada paga acontece FORA de qualquer transação (o SDK pode repetir
 * handlers de transação — nunca chamar serviços dentro deles).
 * @param {string} bikeId ID canônico da bike
 * @return {Promise<void>}
 */
async function processScansWithCoordinates(bikeId: string) {
  const sessionsRef = db.ref(`bikes/${bikeId}/sessions_v2`);
  const snapshot = await sessionsRef.once("value");
  const sessions = snapshot.val() as
    {[key: string]: V2GeoSessionRow} | null;

  if (!sessions) return;

  for (const [sessionKey, session] of Object.entries(sessions)) {
    if (!session?.scans) continue;

    console.log(`Processando sessão ${sessionKey}...`);
    const geoRef = sessionsRef.child(`${sessionKey}/geo`);

    for (let i = 0; i < session.scans.length; i++) {
      const scan = session.scans[i];
      if (!scan || typeof scan !== "object") continue;

      // Identidade estável da observação (GEO-01) — nunca o índice da lista
      const key = scanKey(scan.idx, scan.ts_ms);
      const networks = scan.networks ?? [];
      if (networks.length === 0) continue;

      // Já enriquecido e ATUAL (mesmo conteúdo)? Nada a fazer
      const existingGeo = (session.geo ?? {})[key] ?? null;
      const current = resolveGeo(scan, existingGeo);
      if (current.status === "fresh") continue;
      // 'stale' = geo de conteúdo antigo: recalcula (o novo substitui no
      // path por scan_key, com o hash do conteúdo atual)
      if (current.status === "stale") {
        console.log(`  Scan ${key}: geo desatualizado - recalculando`);
      }

      // Assinatura do conteúdo NO MOMENTO DA CONSULTA (GEO-03) —
      // observações legado sem content_hash recebem o hash calculado agora
      const hashAtRequest = scan.content_hash ?? hashNetworks(networks);

      console.log(`  Scan ${key} - ${networks.length} redes...`);

      // Consulta externa (paga) — fora de transação
      const coordinates = await convertWifiToCoordinates(networks);
      if (!coordinates) {
        // GEO-05: falha mantém dados e pendência — nada é apagado
        console.log(`    ❌ ${key}: geolocalização indisponível (pendente)`);
        await new Promise((resolve) => setTimeout(resolve, 1000));
        continue;
      }

      // RE-LEITURA após a consulta demorada: a observação pode ter sido
      // remontada/corrigida nesse intervalo (C245-11/C245-12)
      const scanNowSnap =
        await sessionsRef.child(`${sessionKey}/scans`).once("value");
      const scanNowList = scanNowSnap.val() as
        AssembledScan[] | null;
      const scanNow = findScanByKey(scanNowList, key);

      if (!canInstallGeo(hashAtRequest, scanNow)) {
        console.log(`    ⚠️ ${key}: conteúdo mudou durante a consulta - ` +
            "resultado descartado (será recalculado)");
        await new Promise((resolve) => setTimeout(resolve, 1000));
        continue;
      }

      const entry: GeoEntry = {
        lat: coordinates.lat,
        lng: coordinates.lng,
        accuracy: coordinates.accuracy,
        base_hash: hashAtRequest,
        source: "google_geolocation_v1",
        computed_at: Date.now(),
      };
      // Instalação em path ESPECÍFICO por identidade — nunca substitui a lista
      await geoRef.child(key).set(entry);
      console.log(`    ✅ ${key}: ${coordinates.lat}, ${coordinates.lng}`);

      // Delay para não sobrecarregar a API
      await new Promise((resolve) => setTimeout(resolve, 1000));
    }
  }
}

/**
 * Localiza uma observação pela chave estável (GEO-01) em uma lista —
 * independe da posição ocasional dos itens.
 * @param {AssembledScan[]|null} scans Lista de observações
 * @param {string} key Chave estável (scan_key)
 * @return {AssembledScan|null} Observação encontrada ou null
 */
function findScanByKey(
  scans: AssembledScan[] | null,
  key: string
): AssembledScan | null {
  if (!Array.isArray(scans)) return null;
  for (const scan of scans) {
    if (scan && typeof scan === "object" &&
        scanKey(scan.idx, scan.ts_ms) === key) {
      return scan;
    }
  }
  return null;
}

/**
 * Send notification to admin
 * @param {string} message Message to send
 */
async function sendNotification(message: string): Promise<void> {
  try {
    await bot.telegram.sendMessage(ADMIN_CHAT_ID, message);
  } catch (error) {
    console.error("Erro notificação:", error);
  }
}

// A09: resfriamento por usuário para o comando de conversão (API paga)
const converterCooldown = new Map<number, number>();
const CONVERTER_COOLDOWN_MS = 60 * 1000;

bot.start((ctx) => {
  const who = ctx.from?.username || ctx.from?.id;
  console.log("📱 Comando /start recebido de:", who);
  const welcomeMessage = "🚴 *Bot de Monitoramento de Bicicletas*\n\n" +
    "Comandos:\n" +
    "/status [bike] - Status da bike\n" +
    "/rota [bike] - Última rota\n" +
    "/converter [bike] - Converter WiFi em GPS\n" +
    "/bikes - Listar bikes";

  ctx.reply(welcomeMessage, {parse_mode: "Markdown"});
});

bot.hears(/bota pra rodar/i, (ctx) => {
  console.log("🎯 Mensagem 'bota pra rodar' detectada:", ctx.message.text);
  ctx.reply("Bot");
});

// A06: um único handler /converter (o duplicado que referenciava
// GeolocationService inexistente foi removido)
bot.command("converter", async (ctx) => {
  const bikeId = ctx.message.text.split(" ")[1];
  if (!bikeId) {
    ctx.reply("❌ Especifique o ID da bike.\nExemplo: /converter bpr-01e240");
    return;
  }

  // A09: limite de uso por usuário
  const userId = ctx.from?.id ?? 0;
  const last = converterCooldown.get(userId) ?? 0;
  if (Date.now() - last < CONVERTER_COOLDOWN_MS) {
    ctx.reply("⏳ Aguarde um minuto entre conversões (API com custo).");
    return;
  }
  converterCooldown.set(userId, Date.now());

  ctx.reply(`🔄 Processando scans de ${bikeId}...`);

  try {
    await processScansWithCoordinates(bikeId);
    ctx.reply(`✅ Scans de ${bikeId} processados com coordenadas!`);
  } catch (error) {
    console.error("Erro no comando converter:", error);
    ctx.reply("❌ Erro ao processar scans");
  }
});

bot.command("status", async (ctx) => {
  const bikeId = ctx.message.text.split(" ")[1];
  if (!bikeId) {
    ctx.reply("❌ Especifique o ID da bike.\nExemplo: /status bpr-01e240");
    return;
  }

  try {
    // N10: a seleção não pode ser orderByKey (lexicográfica escolhe
    // "9999-1" sobre "10000-1"); recência ancorada no ingest_ts da montagem
    const snapshot = await db.ref(`bikes/${bikeId}/sessions_v2`)
      .once("value");
    const sessions = snapshot.val() as
      {[key: string]: V2StatusRow & {ingest_ts?: number}} | null;

    if (!sessions) {
      ctx.reply(`❌ Bike ${bikeId} não encontrada`);
      return;
    }

    const sessionKey = Object.keys(sessions).reduce(
      (acc: string | null, key) => {
        if (!acc) return key;
        const accTs = Number(sessions[acc]?.ingest_ts) || 0;
        const keyTs = Number(sessions[key]?.ingest_ts) || 0;
        if (accTs !== keyTs) return keyTs > accTs ? key : acc;
        return sessionKeyComparator(acc, key) < 0 ? key : acc;
      },
      null
    );

    if (!sessionKey) {
      ctx.reply(`❌ Bike ${bikeId} não encontrada`);
      return;
    }
    const session = sessions[sessionKey];

    // N04: millis desde o boot NÃO é data absoluta — rótulo relativo
    const startLabel = session.session_start_ms ?
      `relativo ao boot (${session.session_start_ms} ms)` :
      "origem desconhecida";

    let message = `🚴 Bike ${bikeId.toUpperCase()}\n\n` +
                 `📍 Status: ${session.complete ? "Completa" : "Parcial"}\n` +
                 `🏷️ Sessão: ${sessionKey}\n` +
                 `🚀 Início: ${startLabel}\n`;

    if (session.scans) {
      message += `📊 Scans: ${session.scans.length}\n`;
    }

    ctx.reply(message);
  } catch (error) {
    console.error("Erro no comando status:", error);
    ctx.reply("❌ Erro ao buscar status");
  }
});

bot.command("rota", async (ctx) => {
  const bikeId = ctx.message.text.split(" ")[1];
  if (!bikeId) {
    ctx.reply("❌ Especifique o ID da bike.\nExemplo: /rota bpr-01e240");
    return;
  }

  try {
    // Item 5 (GEO-06): leitor combina a base (`scans`) com o enriquecimento
    // canônico (`geo/{scan_key}`); `coordinates` embutido é legado read-only
    const ref = db.ref(`bikes/${bikeId}/sessions_v2`);
    const sessionsSnapshot = await ref.once("value");
    const sessions = sessionsSnapshot.val() as
      {[key: string]: V2GeoSessionRow} | null;

    if (!sessions) {
      ctx.reply(`❌ Nenhuma sessão encontrada para ${bikeId}`);
      return;
    }

    const sessionIds = Object.keys(sessions)
      .sort(sessionKeyComparator).reverse();
    const latestSessionId = sessionIds[0];
    const latestSession = sessions[latestSessionId];

    if (!latestSession.scans || latestSession.scans.length < 2) {
      const n = latestSession.scans?.length ?? 0;
      ctx.reply(`❌ Poucos scans na sessão (${n})`);
      return;
    }

    const scans = latestSession.scans;
    const geoMap = latestSession.geo ?? {};
    let totalDistance = 0;
    let movementPoints = 0;
    let convertedScans = 0;
    let staleScans = 0;

    // Resolve cada observação pela identidade estável (nunca pelo índice)
    const resolved: (GeoResolution | null)[] = scans.map((scan) => {
      if (!scan || typeof scan !== "object") return null;
      const key = scanKey(scan.idx, scan.ts_ms);
      return resolveGeo(scan, geoMap[key] ?? null);
    });

    resolved.forEach((res) => {
      if (!res) return;
      if (res.coordinates) convertedScans++;
      if (res.status === "stale") staleScans++;
    });

    for (let i = 1; i < resolved.length; i++) {
      const prev = resolved[i - 1];
      const curr = resolved[i];

      if (prev?.coordinates && curr?.coordinates) {
        const distance = calculateGPSDistance(
          prev.coordinates.lat, prev.coordinates.lng,
          curr.coordinates.lat, curr.coordinates.lng
        );

        if (distance > 5) {
          totalDistance += distance;
          movementPoints++;
        }
      }
    }

    // A08: duração em unidade única (scans v2 carregam ts_ms)
    const firstTs = scans[0]?.ts_ms ?? 0;
    const lastTs = scans[scans.length - 1]?.ts_ms ?? 0;
    const duration = Math.round((lastTs - firstTs) / 60000);

    const allNetworks = new Set<string>();
    scans.forEach((scan) => {
      for (const net of scan.networks ?? []) {
        allNetworks.add(net.bssid || net.ssid);
      }
    });

    let message = `🗺️ *Rota ${bikeId.toUpperCase()}*\n\n` +
                 `🏷️ Sessão: ${latestSessionId}\n` +
                 `📊 Scans: ${scans.length}\n` +
                 `🌐 Convertidos: ${convertedScans}/${scans.length}\n` +
                 `📏 Distância: ${totalDistance.toFixed(0)}m (estimativa)\n` +
                 `⏱️ Duração: ${duration} min\n` +
                 `📡 Redes detectadas: ${allNetworks.size}\n\n`;

    // C245-14/GEO-05: ausência EXPLÍCITA — leitura consistente com dados
    // parcialmente enriquecidos (pendentes e desatualizados são visíveis)
    if (staleScans > 0) {
      message += `🔁 ${staleScans} observação(ões) com geo desatualizado ` +
        "(aguardando recálculo)\n";
    }
    if (convertedScans < scans.length) {
      message += `⏳ ${scans.length - convertedScans} sem coordenadas ` +
        "(use /converter)\n";
    }

    if (movementPoints > 0) {
      message += `✅ Movimento detectado em ${movementPoints} pontos`;
    } else {
      message += "⚠️ Pouco movimento detectado - bike pode ter ficado parada";
    }

    ctx.reply(message, {parse_mode: "Markdown"});
  } catch (error) {
    console.error("Erro no comando rota:", error);
    ctx.reply("❌ Erro ao calcular rota");
  }
});

bot.command("bikes", async (ctx) => {
  try {
    const snapshot = await db.ref("bikes").once("value");
    const bikes = snapshot.val() as Record<string, unknown> | null;

    if (!bikes) {
      ctx.reply("❌ Nenhuma bike encontrada");
      return;
    }

    const bikeIds = Object.keys(bikes);
    let message = "🚴 *Bikes Monitoradas*\n\n";

    bikeIds.forEach((id) => {
      message += `• ${id.toUpperCase()}\n`;
    });

    message += "\nPara mais informações:\n";
    bikeIds.forEach((id) => {
      message += `\`/status ${id}\` - Ver status atual\n`;
      message += `\`/rota ${id}\` - Ver última rota\n`;
    });

    ctx.reply(message, {parse_mode: "Markdown"});
  } catch (error) {
    console.error("Erro no comando bikes:", error);
    ctx.reply("❌ Erro ao listar bikes");
  }
});

// A09: webhook valida o segredo de origem quando configurado
// (firebase functions:config:set telegram.webhook_secret="..." e
//  setWebhook com secret_token)
export const telegramBot = functions.https.onRequest(async (req, res) => {
  const secret = functions.config().telegram?.webhook_secret;
  if (secret && req.header("x-telegram-bot-api-secret-token") !== secret) {
    console.warn("🚫 Webhook recusado: secret token inválido/ausente");
    res.status(403).send("Forbidden");
    return;
  }

  try {
    await bot.handleUpdate(req.body);
    res.status(200).send("OK");
  } catch (error) {
    console.error("❌ Webhook error:", error);
    res.status(500).send("Error");
  }
});
