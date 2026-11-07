/*
 * BPR Sistema — Montagem de sessão e política de enriquecimento (puro/testável)
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

// Itens 2/4/5 (M03/M04/M06): lógica PURA de montagem de sessão, identidade
// de observação e política de enriquecimento geográfico. Sem I/O — o
// index.ts cuida do Firebase; este módulo existe para ser testado no host
// (C245-10..15) sem serviço real.

import {NetworkDto, normalizeNetwork} from "./dto";

// ---------------------------------------------------------------------------
// Identidade de observação (GEO-01)
// ---------------------------------------------------------------------------

/**
 * Chave estável de uma observação dentro da origem (sessão): o `idx`
 * transmitido pelo produtor; fallback documentado `t{ts}` para fragmentos
 * legados sem idx (C245-13 — fusão por ts é o comportamento documentado do
 * legado; nenhum undefined é produzido).
 * @param {unknown} idx Índice do scan na sessão (quando transmitido)
 * @param {unknown} tsMs Timestamp do scan (ms do boot da bike)
 * @return {string} chave estável da observação
 */
export function scanKey(idx: unknown, tsMs: unknown): string {
  const n = Number(idx);
  if (Number.isInteger(n) && n >= 0) return `i${n}`;
  const t = Number(tsMs);
  return `t${Number.isFinite(t) ? t : 0}`;
}

// ---------------------------------------------------------------------------
// Revisão de conteúdo (GEO-03/GEO-04)
// ---------------------------------------------------------------------------

/**
 * Hash estável (FNV-1a 32 bits) do CONTEÚDO de redes de uma observação.
 * Igualdade de timestamp sozinha não comprova conteúdo igual: o hash vincula
 * o resultado geográfico ao conteúdo exato que foi geolocalizado; conteúdo
 * que muda invalida o resultado (fica "stale", nunca atribuído à nova).
 * @param {NetworkDto[]} networks Redes normalizadas da observação
 * @return {string} hash hexadecimal de 8 caracteres
 */
export function hashNetworks(networks: NetworkDto[]): string {
  let canonical = "";
  for (const net of networks) {
    canonical += `${net.bssid}|${net.rssi}|${net.channel};`;
  }
  // FNV-1a 32 bits — determinístico entre execuções, suficiente para
  // detecção de mudança de conteúdo (não é propósito criptográfico)
  let h = 0x811c9dc5;
  for (let i = 0; i < canonical.length; i++) {
    h ^= canonical.charCodeAt(i);
    h = Math.imul(h, 0x01000193) >>> 0;
  }
  return h.toString(16).padStart(8, "0");
}

// ---------------------------------------------------------------------------
// Remontagem (N05 + identidade por scan_key)
// ---------------------------------------------------------------------------

/** Chunk de um scan transportado por um fragmento de dados */
export interface ScanChunk {
  idx?: number;
  ts: number;
  net_offset: number;
  nets: unknown[];
}

/** Observação remontada da projeção v2 (base — sem enriquecimento) */
export interface AssembledScan {
  scan_key: string;
  idx: number | null;
  ts_ms: number;
  net_offset: number;
  networks: NetworkDto[];
  content_hash: string;
}

/**
 * Reúne os chunks de um mesmo scan em UMA observação, agrupados por
 * `scan_key` (idx do produtor; fallback ts), redes ordenadas por
 * `net_offset` e concatenadas (N05: uma coleta = uma observação).
 * @param {Array} chunksBySeq chunks por fragmento (seq 0..total-2)
 * @return {Array} observações ordenadas pela identidade do produtor
 */
export function reassembleScans(chunksBySeq: ScanChunk[][]): AssembledScan[] {
  const groups = new Map<string, {idx: number | null; ts: number;
    chunks: {off: number; nets: unknown[]}[]}>();

  for (const chunks of chunksBySeq) {
    for (const scan of chunks) {
      const key = scanKey(scan.idx, scan.ts);
      let group = groups.get(key);
      if (!group) {
        group = {idx: scan.idx ?? null, ts: Number(scan.ts), chunks: []};
        groups.set(key, group);
      }
      group.chunks.push({
        off: Number(scan.net_offset) || 0,
        nets: scan.nets ?? [],
      });
    }
  }

  const scans: AssembledScan[] = [];
  for (const group of groups.values()) {
    group.chunks.sort((a, b) => a.off - b.off);
    const networks = group.chunks
      .flatMap((chunk) => chunk.nets.map(normalizeNetwork));
    scans.push({
      scan_key: scanKey(group.idx, group.ts),
      idx: group.idx,
      ts_ms: group.ts,
      net_offset: 0, // observação completa: todas as redes presentes
      networks,
      content_hash: hashNetworks(networks),
    });
  }
  // ordem estável pela identidade do produtor (fallback: ts)
  scans.sort((a, b) => (a.idx ?? a.ts_ms) - (b.idx ?? b.ts_ms));
  return scans;
}

// ---------------------------------------------------------------------------
// Política de enriquecimento geográfico (GEO-02..GEO-06)
// ---------------------------------------------------------------------------

/** Resultado geográfico gravado pelo conversor em `geo/{scan_key}` */
export interface GeoEntry {
  lat: number;
  lng: number;
  accuracy: number;
  base_hash: string; // conteúdo exato que foi geolocalizado (GEO-03)
  source: string; // ex.: google_geolocation_v1
  computed_at: number; // âncora do servidor (ms)
}

export type GeoStatus = "fresh" | "legacy" | "stale" | "missing";

/** Coordenada resolvida de uma observação + status de validade */
export interface GeoResolution {
  coordinates: {lat: number; lng: number} | null;
  status: GeoStatus;
}

/**
 * Resolve a coordenada de uma observação combinando o enriquecimento
 * canônico (`geo/{scan_key}`, gravado pelo conversor) com o legado
 * (`scans[].coordinates`, read-only durante a transição).
 *
 * - `fresh`: geo presente e `base_hash` == `content_hash` do scan atual;
 * - `stale`: geo presente mas de OUTRO conteúdo — não é atribuído
 *   silenciosamente à observação nova (GEO-04/C245-12);
 * - `legacy`: sem geo canônico, com `coordinates` embutido legado (compat);
 * - `missing`: sem enriquecimento (GEO-05 — ausência explícita).
 *
 * @param {object} scan observação atual (com coordinates legado opcional)
 * @param {object|null} geo entrada canônica `geo/{scan_key}`
 * @return {object} coordenada válida + status
 */
export function resolveGeo(
  scan: AssembledScan & {coordinates?: unknown},
  geo: GeoEntry | null
): GeoResolution {
  if (geo && typeof geo === "object" && Number.isFinite(geo.lat) &&
      Number.isFinite(geo.lng)) {
    if (geo.base_hash === scan.content_hash) {
      return {coordinates: {lat: geo.lat, lng: geo.lng}, status: "fresh"};
    }
    return {coordinates: null, status: "stale"};
  }
  // Compat de leitura: enriquecimento legado embutido no scan (read-only —
  // o conversor não grava mais aqui; preservado pelo merge do normalizador)
  const c = scan.coordinates as {lat?: unknown; lng?: unknown} | undefined;
  if (c && typeof c === "object" && Number.isFinite(Number(c.lat)) &&
      Number.isFinite(Number(c.lng))) {
    return {
      coordinates: {lat: Number(c.lat), lng: Number(c.lng)},
      status: "legacy",
    };
  }
  return {coordinates: null, status: "missing"};
}

/**
 * Decide se o resultado de uma consulta externa deve ser instalado como
 * atual: o conteúdo do scan precisa ser o MESMO do momento da consulta
 * (re-conferido DEPOIS da chamada demorada — GEO-03/C245-12).
 * @param {string} hashAtRequest conteúdo observado quando a consulta começou
 * @param {object|null} scanNow observação atual (re-lida) por scan_key
 * @return {boolean} true = pode instalar
 */
export function canInstallGeo(
  hashAtRequest: string,
  scanNow: AssembledScan | null
): boolean {
  return scanNow !== null && scanNow.content_hash === hashAtRequest;
}

// ---------------------------------------------------------------------------
// Merge defensivo de extras legado (transição GEO-06)
// ---------------------------------------------------------------------------

/**
 * Preserva campos extras legado (ex.: `coordinates` embutidos) de uma
 * projeção existente quando o conteúdo-base da observação equivale.
 * O canônico NOVO é `geo/{scan_key}`; isto evita destruir o legado durante
 * a transição sem torná-lo fonte de verdade editável.
 * @param {AssembledScan[]} fresh observações remontadas agora
 * @param {unknown} existingScanList lista `scans` atual (pode ser null)
 * @return {AssembledScan[]} observações com extras legado transportados
 */
export function mergeLegacyExtras(
  fresh: AssembledScan[],
  existingScanList: unknown
): AssembledScan[] {
  const existing = Array.isArray(existingScanList) ? existingScanList : null;
  if (!existing || existing.length === 0) return fresh;
  return fresh.map((scan, i) => {
    const old = existing[i];
    if (!old || typeof old !== "object") return scan;
    const oldScan = old as AssembledScan & {ts_ms?: unknown};
    // mesmo índice, conteúdo divergente: extras não são herdados (GEO-04)
    if (Number(oldScan.ts_ms) !== Number(scan.ts_ms)) return scan;
    const extras: Record<string, unknown> = {};
    for (const [k, v] of Object.entries(oldScan)) {
      if (k === "scan_key" || k === "idx" || k === "ts_ms" ||
          k === "net_offset" || k === "networks" || k === "content_hash") {
        continue;
      }
      extras[k] = v; // ex.: coordinates legado do conversor antigo
    }
    return {...scan, ...extras} as AssembledScan;
  });
}
