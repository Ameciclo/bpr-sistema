/*
 * BPR Sistema — Contrato de bytes do protocolo de lotes (espelho TS do
 * firmware/common/batch_proto.{h,cpp} — PRD transferência de lotes).
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

import {createHash} from "crypto";

// Contrato de bytes (firmware/common/batch_proto.h e docs/BATCH_PROTOCOL.md):
//   "BTP1" | bike_id(12 ASCII) | sid(u32 LE) | salt(u16 LE) |
//   total_parts(u16 LE) | total_bytes(u32 LE) |
//   [por parte em ordem: index(u16 LE) | len(u16 LE) | bytes(len) ]
// SHA-256 (FIPS 180-4) sobre esses bytes. Índices 0..total-1.

export const BATCH_PROTO_VERSION = 1;
export const BATCH_PROTO_MAGIC = "BTP1";
export const BATCH_PROTO_BIKE_ID_LEN = 10;
export const BATCH_PROTO_MAX_PARTS = 512;
export const BATCH_PROTO_MAX_PART_BYTES = 460;

/** Metadados imutáveis do lote (anunciados pelo produtor) */
export interface BatchMeta {
  v: number;
  bike_id: string;
  session_id: number;
  session_salt: number;
  total: number;
  total_bytes: number;
  sha256: string; // hex 64
  uploaded_by?: string; // proveniência (base) — fora do conteúdo congelado
}

/** Parte persistida: payload = bytes exatos da parte como transmitidos */
export interface BatchPartRecord {
  len: number;
  crc32: string; // hex 8
  payload: string; // bytes UTF-8 da parte (fragmento do produtor)
}

/**
 * CRC-32/ISO-HDLC idêntico ao do firmware (refletido, poly 0xEDB88320).
 * @param {string} payload Bytes da parte (string UTF-8 = bytes exatos)
 * @return {string} CRC em hex de 8 caracteres
 */
export function batchCrc32(payload: string): string {
  let crc = 0xFFFFFFFF;
  for (let i = 0; i < payload.length; i++) {
    crc ^= payload.charCodeAt(i) & 0xFF;
    for (let b = 0; b < 8; b++) {
      crc = (crc >>> 1) ^ (0xEDB88320 & -(crc & 1));
    }
  }
  crc = (crc ^ 0xFFFFFFFF) >>> 0;
  return crc.toString(16).padStart(8, "0");
}

/**
 * Hash do lote sobre a representação canônica de bytes (mesma do firmware).
 * @param {object} meta Metadados imutáveis (sem uploaded_by)
 * @param {Array} parts Partes em ordem ({index, payload})
 * @return {string} SHA-256 hex 64
 */
export function batchSha256(
  meta: Pick<BatchMeta, "bike_id" | "session_id" | "session_salt" |
    "total" | "total_bytes">,
  parts: {index: number; payload: string}[]
): string {
  if (meta.bike_id.length !== BATCH_PROTO_BIKE_ID_LEN) {
    throw new Error("bike_id deve ter 10 caracteres canônicos");
  }
  const h = createHash("sha256");
  h.update(BATCH_PROTO_MAGIC, "ascii");
  h.update(meta.bike_id, "ascii");
  h.update(u32le(meta.session_id));
  h.update(u16le(meta.session_salt));
  h.update(u16le(meta.total));
  h.update(u32le(meta.total_bytes));
  for (const part of parts) {
    h.update(u16le(part.index));
    h.update(u16le(Buffer.byteLength(part.payload, "utf8")));
    h.update(Buffer.from(part.payload, "utf8"));
  }
  return h.digest("hex");
}

/**
 * Inteiro de 16 bits em little-endian (contrato de bytes)
 * @param {number} v Valor
 * @return {Buffer} 2 bytes LE
 */
function u16le(v: number): Buffer {
  const b = Buffer.alloc(2);
  b.writeUInt16LE(v & 0xFFFF, 0);
  return b;
}

/**
 * Inteiro de 32 bits em little-endian (contrato de bytes)
 * @param {number} v Valor
 * @return {Buffer} 4 bytes LE
 */
function u32le(v: number): Buffer {
  const b = Buffer.alloc(4);
  b.writeUInt32LE(v >>> 0, 0);
  return b;
}

/** Resultado da validação de um lote completo */
export interface BatchValidation {
  valid: boolean;
  reason?: string;
  totalBytes?: number;
}

/**
 * Valida um lote completo contra o contrato: índices exatos 0..total-1,
 * comprimentos, CRC por parte, soma de bytes e hash global (T02/T04/T05).
 * @param {BatchMeta} meta Metadados anunciados
 * @param {object} parts Parte por índice (chave string)
 * @return {BatchValidation} válido + motivo detalhado quando inválido
 */
export function validateBatch(
  meta: BatchMeta,
  parts: Record<string, BatchPartRecord>
): BatchValidation {
  if (meta.v !== BATCH_PROTO_VERSION) {
    return {valid: false, reason: `versão ${meta.v} não suportada`};
  }
  const total = Number(meta.total);
  if (!Number.isInteger(total) || total <= 0 || total > BATCH_PROTO_MAX_PARTS) {
    return {valid: false, reason: `total ${total} fora do contrato`};
  }
  if (!/^[0-9a-f]{64}$/.test(meta.sha256 ?? "")) {
    return {valid: false, reason: "sha256 anunciado inválido"};
  }
  if (String(meta.bike_id ?? "").length !== BATCH_PROTO_BIKE_ID_LEN) {
    return {valid: false, reason: "bike_id fora do formato canônico"};
  }

  let totalBytes = 0;
  const ordered: {index: number; payload: string}[] = [];
  for (let i = 0; i < total; i++) {
    const rec = parts[String(i)];
    if (!rec || typeof rec !== "object" || typeof rec.payload !== "string") {
      return {valid: false, reason: `parte ${i} ausente`};
    }
    const payload = rec.payload;
    const len = Buffer.byteLength(payload, "utf8");
    if (len > BATCH_PROTO_MAX_PART_BYTES) {
      return {valid: false, reason: `parte ${i} excede o orçamento`};
    }
    if (Number(rec.len) !== len) {
      return {valid: false, reason: `parte ${i}: len ${rec.len} != ${len}`};
    }
    const crc = batchCrc32(payload);
    if ((rec.crc32 ?? "").toLowerCase() !== crc) {
      return {valid: false, reason: `parte ${i}: crc divergente`};
    }
    totalBytes += len;
    ordered.push({index: i, payload});
  }

  const wantBytes = Number(meta.total_bytes);
  if (Number.isFinite(wantBytes) && wantBytes !== totalBytes) {
    return {valid: false, reason: `total_bytes ${wantBytes} != ${totalBytes}`};
  }

  const sha = batchSha256(meta, ordered);
  if (sha !== meta.sha256.toLowerCase()) {
    return {valid: false, reason: `hash global divergente (${sha})`};
  }
  return {valid: true, totalBytes};
}
