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
// DTO interno de rede WiFi — único formato aceito por produtores/consumidores
interface NetworkDto {
  ssid: string;
  bssid: string;
  rssi: number;
  channel: number;
}

// Sessão tocada por um lote (aguarda completude)
interface TouchedSession {
  bikeId: string;
  sessionId: number;
  salt: number;
  total: number;
}

// Scan da projeção v2 (unidades explícitas: ts_ms)
interface ScanV2 {
  idx?: number; // N04: identidade do scan na sessão (índice do produtor)
  ts_ms: number;
  net_offset: number;
  networks: NetworkDto[];
}

// Registro de fragmento bruto em /bikes/{id}/raw_fragments/{sid}-{salt}/{seq}
interface FragmentRecord {
  crc32?: string;
  received_ts?: number;
  payload?: Fragment;
}

// Sessão tocada por um lote (aguarda completude)
interface TouchedSession {
  bikeId: string;
  sessionId: number;
  salt: number;
  total: number;
}

// Scan da projeção v2 (unidades explícitas: ts_ms)
interface ScanV2 {
  idx?: number; // N04: identidade do scan na sessão (índice do produtor)
  ts_ms: number;
  net_offset: number;
  networks: NetworkDto[];
}

// Registro de fragmento bruto em /bikes/{id}/raw_fragments/{sid}-{salt}/{seq}
interface FragmentRecord {
  crc32?: string;
  received_ts?: number;
  payload?: Fragment;
}

// Scan de uma sessão v2 durante conversão de coordenadas
interface V2ScanRow {
  coordinates?: {lat: number; lng: number};
  networks?: NetworkDto[];
}

// Sessão v2 resumida (comando /status)
interface V2StatusRow {
  session_start_ms?: number;
  scans?: unknown[];
  complete?: boolean;
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
function normalizeNetwork(net: unknown): NetworkDto {
  if (Array.isArray(net)) {
    return {
      ssid: String(net[0] ?? ""),
      bssid: String(net[1] ?? ""),
      rssi: Number(net[2] ?? 0),
      channel: Number(net[3] ?? 0),
    };
  }
  if (net && typeof net === "object") {
    const o = net as Record<string, unknown>;
    return {
      ssid: String(o.ssid ?? ""),
      bssid: String(o.bssid ?? ""),
      rssi: Number(o.rssi ?? 0),
      channel: Number(o.channel ?? 0),
    };
  }
  return {ssid: "", bssid: "", rssi: 0, channel: 0};
}

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
  scans?: {idx?: number; ts: number; net_offset: number; nets: unknown[]}[];
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

  const finalFrag = bySeq.get(total - 1);
  if (!finalFrag) return;

  // N05: os chunks de um MESMO scan são REUNIDOS em uma única observação —
  // identidade = idx (índice do produtor) com fallback em ts; as redes dos
  // chunks são ordenadas por net_offset e concatenadas. Antes, um scan
  // denso dividido em N fragments virava N scans incompletos.
  const groups = new Map<string, {
    idx?: number;
    ts: number;
    chunks: {off: number; nets: unknown[]}[];
  }>();
  for (let s = 0; s < total - 1; s++) {
    const frag = bySeq.get(s);
    if (!frag) continue;
    for (const scan of frag.scans ?? []) {
      const key = scan.idx !== undefined ?
        `i${scan.idx}` : `t${scan.ts}`;
      let group = groups.get(key);
      if (!group) {
        group = {
          idx: scan.idx,
          ts: Number(scan.ts),
          chunks: [],
        };
        groups.set(key, group);
      }
      group.chunks.push({
        off: Number(scan.net_offset) || 0,
        nets: scan.nets ?? [],
      });
    }
  }

  const scans: ScanV2[] = [];
  for (const group of groups.values()) {
    group.chunks.sort((a, b) => a.off - b.off);
    const networks = group.chunks
      .flatMap((chunk) => chunk.nets.map(normalizeNetwork));
    scans.push({
      idx: group.idx,
      ts_ms: group.ts,
      net_offset: 0, // observação completa: todas as redes presentes
      networks,
    });
  }
  // ordem estável pela identidade do produtor (fallback: ts)
  scans.sort((a, b) => (a.idx ?? a.ts_ms) - (b.idx ?? b.ts_ms));

  // A04: bateria como {ts_ms, percent} — sem ambiguidade de unidade
  const battery = (finalFrag.battery ?? []).map((entry) => ({
    ts_ms: Number(Array.isArray(entry) ? entry[0] : 0),
    percent: Number(Array.isArray(entry) ? entry[1] : 0),
  }));

  const sessionRef =
    db.ref(`bikes/${bikeId}/sessions_v2/${sessionId}-${salt}`);

  // N05: replay NÃO destrói enriquecimento — coordenadas (e qualquer campo
  // extra gravado por conversores) da projeção existente são preservadas
  // por scan; apenas os campos-base são reescritos. O transporte de extras
  // exige CONTEÚDO equivalente (mesma ts) — índice sozinho poderia levar
  // coordenadas para o scan errado se a projeção anterior divergiu
  const existingSnap = await sessionRef.child("scans").once("value");
  const existingScans = existingSnap.val() as ScanV2[] | null;
  const mergedScans = Array.isArray(existingScans) && existingScans.length > 0 ?
    scans.map((fresh, i) => {
      const old = existingScans[i];
      if (!old || typeof old !== "object") return fresh;
      // mesmo índice, conteúdo divergente: extras não são herdados
      if (Number(old.ts_ms) !== Number(fresh.ts_ms)) return fresh;
      const extras: Record<string, unknown> = {};
      for (const [k, v] of Object.entries(old)) {
        if (k === "idx" || k === "ts_ms" || k === "net_offset" ||
              k === "networks") continue;
        extras[k] = v; // ex.: coordinates do conversor geográfico
      }
      return {...fresh, ...extras} as ScanV2;
    }) :
    scans;

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
  await sessionRef.child(`source_batches/${sourceBatch}`).set(true);

  const nscans = scans.length;
  console.log(`✅ Sessão ${sessionId}-${salt} de ${bikeId} ` +
    `(${nscans} scans, ${groups.size} observações reunidas)`);
}

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
 * Processa scans da projeção v2 e adiciona coordenadas (API paga).
 * @param {string} bikeId ID canônico da bike
 * @return {Promise<void>}
 */
async function processScansWithCoordinates(bikeId: string) {
  const sessionsRef = db.ref(`bikes/${bikeId}/sessions_v2`);
  const snapshot = await sessionsRef.once("value");
  const sessions = snapshot.val() as
    {[key: string]: {scans?: V2ScanRow[]}} | null;

  if (!sessions) return;

  for (const [sessionKey, session] of Object.entries(sessions)) {
    if (!session?.scans) continue;

    console.log(`Processando sessão ${sessionKey}...`);

    for (let i = 0; i < session.scans.length; i++) {
      const scan = session.scans[i];

      // Pula se já tem coordenadas
      if (scan.coordinates) continue;

      const networks = scan.networks ?? [];
      if (networks.length === 0) continue;

      console.log(`  Scan ${i} - ${networks.length} redes...`);

      // Converte redes WiFi em coordenadas
      const coordinates = await convertWifiToCoordinates(networks);

      // Atualiza o scan com coordenadas
      if (coordinates) {
        const path = `${sessionKey}/scans/${i}/coordinates`;
        await sessionsRef.child(path).set(coordinates);
        console.log(`    ✅ ${coordinates.lat}, ${coordinates.lng}`);
      } else {
        console.log("    ❌ Não foi possível obter coordenadas");
      }

      // Delay para não sobrecarregar a API
      await new Promise((resolve) => setTimeout(resolve, 1000));
    }
  }
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
    const ref = db.ref(`bikes/${bikeId}/sessions_v2`);
    const sessionsSnapshot = await ref.once("value");
    const sessions = sessionsSnapshot.val() as
      {[key: string]: {scans?: {coordinates?: {lat: number; lng: number};
        networks?: NetworkDto[]; ts_ms?: number}[]}} | null;

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
    let totalDistance = 0;
    let movementPoints = 0;
    let convertedScans = 0;

    scans.forEach((scan) => {
      if (scan.coordinates) convertedScans++;
    });

    for (let i = 1; i < scans.length; i++) {
      const prev = scans[i - 1];
      const curr = scans[i];

      if (prev.coordinates && curr.coordinates) {
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
