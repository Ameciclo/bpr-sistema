#!/usr/bin/env node
/*
 * Verificação nativa (host) — PRD de lotes/recibos (lado backend).
 * Compila o batchProto.ts REAL das Functions (contrato de bytes BTP1) com o
 * tsc do projeto e valida: concordância com o firmware (T17 — os mesmos
 * vetores são computados pelo batch_checks.cpp e comparados no run.sh) e a
 * validação de lotes da nuvem (T02/T04/T05 no nível do backend).
 * Nenhum serviço real é contatado.
 */
const { execSync } = require("child_process");
const path = require("path");
const fs = require("fs");

const HERE = __dirname;
const ROOT = path.join(HERE, "..", "..");
const TSC = path.join(ROOT, "bot/functions/node_modules/.bin/tsc");
const OUT = path.join(HERE, "build", "ts-batchproto");

fs.rmSync(OUT, { recursive: true, force: true });
// Compila a partir do diretório do projeto (types/@types/node resolvem)
execSync(
  `"${TSC}" src/batchProto.ts ` +
  `--outDir "${OUT}" --module commonjs --target es2020 --skipLibCheck --esModuleInterop`,
  { stdio: "pipe", cwd: path.join(ROOT, "bot/functions") }
);

const {
  batchCrc32, batchSha256, validateBatch, BATCH_PROTO_BIKE_ID_LEN,
} = require(path.join(OUT, "batchProto.js"));

const results = [];
function result(name, v) { results.push(`${name}: ${v}`); }
function check(name, ok) { result(name, ok ? "yes" : "no(BUG)"); }

// Vetores idênticos aos do batch_checks.cpp (T17) — o run.sh compara
function synthParts(bike, total, seed) {
  const parts = [];
  for (let i = 0; i < total; i++) {
    let data = "";
    for (let k = 0; k < 90; k++) {
      data += String.fromCharCode(seed + ((i + k) % 26));
    }
    parts.push(`{"seq":${i},"data":"${data}"}`);
  }
  return parts;
}

const cases = [
  { bike: "bpr-0aa001", sid: 4242, salt: 7, total: 1, seed: 65 },
  { bike: "bpr-0bb002", sid: 100, salt: 65535, total: 3, seed: 72 },
  { bike: "bpr-0cc003", sid: 4294967295, salt: 1, total: 56, seed: 79 },
];

for (let c = 0; c < cases.length; c++) {
  const cs = cases[c];
  const parts = synthParts(cs.bike, cs.total, cs.seed);
  const totalBytes = parts.reduce((n, p) => n + Buffer.byteLength(p, "utf8"), 0);
  const sha = batchSha256(
    { bike_id: cs.bike, session_id: cs.sid, session_salt: cs.salt,
      total: cs.total, total_bytes: totalBytes },
    parts.map((payload, index) => ({ index, payload }))
  );
  result(`t17_case_${c}`, sha);
}

// ---------- CRC-32/ISO-HDLC: vetor clássico -------------------------------
result("t00_crc32_iso_hdlc", batchCrc32("123456789")); // cbf43926

// ---------- Validador de lote (backend): T02/T04/T05 ----------------------
{
  const bike = "bpr-210013";
  const parts = synthParts(bike, 6, 65);
  const totalBytes = parts.reduce((n, p) => n + Buffer.byteLength(p, "utf8"), 0);
  const sha = batchSha256(
    { bike_id: bike, session_id: 777, session_salt: 3, total: 6, total_bytes: totalBytes },
    parts.map((payload, index) => ({ index, payload }))
  );

  const meta = { v: 1, bike_id: bike, session_id: 777, session_salt: 3,
    total: 6, total_bytes: totalBytes, sha256: sha, uploaded_by: "base-x" };
  const recs = {};
  parts.forEach((payload, i) => {
    recs[String(i)] = {
      len: Buffer.byteLength(payload, "utf8"),
      crc32: batchCrc32(payload),
      payload,
    };
  });

  // Lote íntegro: válido
  const ok = validateBatch(meta, recs);
  check("t02_valid_batch_accepted", ok.valid && ok.totalBytes === totalBytes);

  // T02: parte ausente → pendente, sem conclusão
  const missing = { ...recs };
  delete missing["3"];
  check("t02_missing_part_pending", !validateBatch(meta, missing).valid);

  // T02: parte truncada (payload menor que len declarado)
  const truncated = { ...recs, "3": { ...recs["3"], len: recs["3"].len + 5 } };
  check("t02_truncated_part_pending", !validateBatch(meta, truncated).valid);

  // T02: CRC divergente (payload corrompido no caminho)
  const corrupted = { ...recs, "3": { ...recs["3"],
    payload: recs["3"].payload.slice(0, -1) + "X" } };
  check("t02_corrupted_crc_pending", !validateBatch(meta, corrupted).valid);

  // T04: total fora do contrato
  check("t04_total_above_contract",
    !validateBatch({ ...meta, total: 513 },
      Object.fromEntries(Array.from({ length: 513 }, (_, i) => [String(i), recs["0"]])),
    ).valid);

  // T05: todos os CRCs ok, hash global divergente (sha anunciado de outro conteúdo)
  check("t05_global_hash_mismatch_rejected",
    !validateBatch({ ...meta, sha256: "0".repeat(64) }, recs).valid);

  // T04/T20: versão não suportada
  check("t20_unsupported_version", !validateBatch({ ...meta, v: 2 }, recs).valid);

  // bike_id fora do formato canônico
  check("t04_noncanonical_bike_id",
    !validateBatch({ ...meta, bike_id: "bpr-12" }, recs).valid);
}

for (const line of results) console.log(line);
const bugs = results.filter((l) => l.endsWith("no(BUG)")).length;
console.log(bugs === 0
  ? "== batch backend checks: sem BUG =="
  : `== batch backend checks: ${bugs} BUG ==`);
process.exit(bugs === 0 ? 0 : 1);
