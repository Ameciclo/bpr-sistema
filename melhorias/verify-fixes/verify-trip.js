#!/usr/bin/env node
/*
 * Verificação nativa (host) — patch de horários (backend): reconstrução
 * temporal REAL (tripReconstruction.ts compilado com o tsc do projeto) +
 * rideCalculator com chegada civil REAL. Matriz H01..H16 (lado backend).
 * Nenhum serviço real é contatado.
 */
const { execSync } = require("child_process");
const path = require("path");
const fs = require("fs");
const Module = require("module");

const HERE = __dirname;
const ROOT = path.join(HERE, "..", "..");
const TSC = path.join(ROOT, "bot/functions/node_modules/.bin/tsc");
const OUT = path.join(HERE, "build", "ts-trip");

fs.rmSync(OUT, { recursive: true, force: true });
execSync(
  `"${TSC}" src/tripReconstruction.ts ` +
  `--outDir "${OUT}" --module commonjs --target es2020 --skipLibCheck --esModuleInterop`,
  { stdio: "pipe", cwd: path.join(ROOT, "bot/functions") }
);
const { reconstructTrip, buildTimeline } =
  require(path.join(OUT, "tripReconstruction.js"));

// ---- rideCalculator real com RTDB transacional local ---------------------
function makeMockDb() {
  const data = {};
  const clone = (v) => (v === undefined ? null : JSON.parse(JSON.stringify(v)));
  const read = (p) => {
    let cur = data;
    for (const k of p.split("/")) {
      if (cur === undefined || cur === null || typeof cur !== "object") return null;
      cur = cur[k];
    }
    return cur === undefined ? null : cur;
  };
  const write = (p, v) => {
    const parts = p.split("/");
    let cur = data;
    for (let i = 0; i < parts.length - 1; i++) {
      if (cur[parts[i]] === undefined || typeof cur[parts[i]] !== "object") {
        cur[parts[i]] = {};
      }
      cur = cur[parts[i]];
    }
    if (v === null) delete cur[parts[parts.length - 1]];
    else cur[parts[parts.length - 1]] = v;
  };
  const snap = (v) => ({ val: () => clone(v), exists: () => v !== null && v !== undefined });
  const makeRef = (p) => ({
    once: async () => snap(read(p)),
    set: async (v) => { write(p, clone(v)); },
    update: async (obj) => {
      for (const [k, v] of Object.entries(obj)) write(p + "/" + k, clone(v));
    },
    child: (s) => makeRef(p + "/" + s),
    on: () => {}, off: () => {},
    transaction: async (fn) => {
      const current = read(p);
      const res = fn(current === null ? null : clone(current));
      if (res === undefined) return { committed: false, snapshot: snap(read(p)) };
      write(p, clone(res));
      return { committed: true, snapshot: snap(res) };
    },
  });
  return { ref: makeRef, _data: data, _read: read, _write: write };
}

const mockDb = makeMockDb();
const mockAdmin = {
  apps: [], initializeApp: () => { mockAdmin.apps.push({}); },
  database: () => mockDb, credential: { cert: () => ({}) },
};
const origLoad = Module._load;
Module._load = function (request, parent, isMain) {
  if (request === "firebase-admin") return mockAdmin;
  if (request === "axios") return { post: async () => { throw new Error("blocked"); } };
  return origLoad.apply(this, [request, parent, isMain]);
};
const rideCalculator = require(path.join(ROOT, "bot/src/services/rideCalculator.js"));

const results = [];
function result(name, v) { results.push(`${name}: ${v}`); }
function check(name, ok) { result(name, ok ? "yes" : "no(BUG)"); }

// Âncoras civis de teste (UTC ms; o contrato do firmware é SEGUNDOS)
const T_1330 = 1731005400000; // 13h30
const S_1330 = T_1330 / 1000;
const T_1340 = T_1330 + 10 * 60000;
const T_1355 = T_1330 + 25 * 60000;
const T_1400 = T_1330 + 30 * 60000;
const T_1403 = T_1330 + 33 * 60000;

async function main() {
// ---------- H01: duração pelas horas civis; scan aos 25 min --------------
{
  const trip = {
    v: 1, bike_id: "bpr-3a0001", trip_seq: 1,
    events: [
      { event_id: 257, kind: "departure", base_id: "base-a",
        civil_s: S_1330, civil_valid: true, counter_ms: 0, boot_id: 0x1111,
        method: "last_base_contact" },
      { event_id: 258, kind: "arrival", base_id: "base-a",
        civil_s: T_1400 / 1000, civil_valid: true, counter_ms: 30 * 60000,
        boot_id: 0x1111, method: "first_contact_time_ref" },
    ],
    time_refs: [],
    sessions: [],
  };
  const r = reconstructTrip(trip);
  result("h01_duration_s", String(r.duration_s)); // 1800
  check("h01_civil_diff", r.duration_s === 1800 && r.quality === "ok");

  const scans = [
    { scan_key: "i0", boot_id: 0x1111, ts_ms: 10 * 60000 }, // 13h40
    { scan_key: "i1", boot_id: 0x1111, ts_ms: 25 * 60000 }, // 13h55
  ];
  const tl = buildTimeline(scans, trip);
  check("h01_scan_at_1340", tl.i0.utc_ms === T_1340);
  check("h01_last_scan_at_1355_not_arrival_time", tl.i1.utc_ms === T_1355);
  check("h02_scan_not_shifted_to_contact",
    tl.i1.utc_ms !== T_1400 && tl.i1.basis !== "none");
}

// ---------- H02: chegada usa o contador atual do evento ------------------
{
  const trip = {
    v: 1, bike_id: "bpr-3b0002", trip_seq: 1,
    events: [
      { event_id: 257, kind: "departure", base_id: "base-a",
        civil_s: S_1330, civil_valid: true, counter_ms: 0, boot_id: 0x1111,
        method: "last_base_contact" },
      { event_id: 258, kind: "arrival", base_id: "base-a",
        civil_s: T_1400 / 1000, civil_valid: true, counter_ms: 30 * 60000,
        boot_id: 0x1111, method: "first_contact_time_ref" },
    ],
  };
  const r = reconstructTrip(trip);
  check("h02_total_is_30min", r.duration_s === 1800);
}

// ---------- H03: contador físico 100→130 (origem explícita) --------------
{
  const trip = {
    v: 1, bike_id: "bpr-3c0003", trip_seq: 1,
    events: [
      { event_id: 257, kind: "departure", base_id: "base-a",
        civil_s: S_1330, civil_valid: true, counter_ms: 100 * 60000,
        boot_id: 0x1111, method: "last_base_contact" },
      { event_id: 258, kind: "arrival", base_id: "base-a",
        civil_s: T_1400 / 1000, civil_valid: true, counter_ms: 130 * 60000,
        boot_id: 0x1111, method: "first_contact_time_ref" },
    ],
  };
  const r = reconstructTrip(trip);
  check("h03_duration_from_civil_not_counter", r.duration_s === 1800);
  const scans = [{ scan_key: "i0", boot_id: 0x1111, ts_ms: 125 * 60000 }];
  const tl = buildTimeline(scans, trip);
  check("h03_counter_origin_used",
    tl.i0.utc_ms === T_1330 + 25 * 60000 && tl.i0.basis === "ref_forward");
}

// ---------- H04: upload 14h03 / replay no dia seguinte não mudam nada -----
{
  const trip = {
    v: 1, bike_id: "bpr-3d0004", trip_seq: 1,
    events: [
      { event_id: 257, kind: "departure", base_id: "base-a",
        civil_s: S_1330, civil_valid: true, counter_ms: 0, boot_id: 0x1111,
        method: "last_base_contact" },
      { event_id: 258, kind: "arrival", base_id: "base-a",
        civil_s: T_1400 / 1000, civil_valid: true, counter_ms: 30 * 60000,
        boot_id: 0x1111, method: "first_contact_time_ref" },
    ],
  };
  const r1 = reconstructTrip(trip);
  const r2 = reconstructTrip(JSON.parse(JSON.stringify(trip))); // replay
  check("h04_replay_keeps_identity_and_time",
    r1.duration_s === r2.duration_s && r1.trip_id === r2.trip_id &&
    r2.duration_s === 1800);
  // A hora de upload (14h03) não entra na duração — nada a calcular com ela
  check("h04_upload_time_outside_duration", T_1403 > T_1400 && r1.arrival.utc_ms === T_1400);
}

// ---------- H06/H07: reboot no caminho — dois trechos, 30 min -------------
{
  const A = 0x1111, B = 0x2222;
  const trip = {
    v: 1, bike_id: "bpr-3e0005", trip_seq: 1,
    events: [
      { event_id: 257, kind: "departure", base_id: "base-a",
        civil_s: S_1330, civil_valid: true, counter_ms: 0, boot_id: A,
        method: "last_base_contact" },
      { event_id: 258, kind: "arrival", base_id: "base-a",
        civil_s: T_1400 / 1000, civil_valid: true, counter_ms: 10 * 60000,
        boot_id: B, method: "first_contact_time_ref" },
    ],
    time_refs: [
      // H07: trecho B começa MAIOR que o último de A? aqui: B=1min após o
      // reboot — reconhecido pelo boot_id, não por redução numérica
      { ref_id: 9, civil_s: (T_1340 + 60000) / 1000, civil_valid: true,
        counter_ms: 1 * 60000, boot_id: B },
    ],
  };
  const r = reconstructTrip(trip);
  check("h06_total_30min_across_reboot", r.duration_s === 1800);

  const scans = [
    { scan_key: "i0", boot_id: A, ts_ms: 15 * 60000 },  // ~13h45 pela ref de A
    { scan_key: "i1", boot_id: B, ts_ms: 5 * 60000 },   // ~13h55 pela ref de B
  ];
  const tl = buildTimeline(scans, trip);
  check("h06_pointA_uses_A_reference", tl.i0.utc_ms === T_1330 + 15 * 60000);
  check("h06_pointB_uses_B_reference",
    tl.i1.utc_ms === T_1340 + 60000 + 4 * 60000); // ref B (13h41) + (5-1)min
  // A referência de B NÃO é aplicada ao contador de A (§6)
  check("h06_no_cross_segment_application",
    tl.i0.utc_ms !== T_1340 + 60000 + (15 - 1) * 60000);
}

// ---------- H08: trecho sem referência — desconhecido explícito -----------
{
  const trip = {
    v: 1, bike_id: "bpr-3f0006", trip_seq: 1,
    events: [
      { event_id: 257, kind: "departure", base_id: "base-a",
        civil_s: S_1330, civil_valid: true, counter_ms: 0, boot_id: 0x1111,
        method: "last_base_contact" },
      { event_id: 258, kind: "arrival", base_id: "base-a",
        civil_s: T_1400 / 1000, civil_valid: true, counter_ms: 10 * 60000,
        boot_id: 0x3333, method: "first_contact_time_ref" },
    ],
    time_refs: [], // trecho 0x2222 intermediário SEM referência
  };
  const scans = [{ scan_key: "i0", boot_id: 0x2222, ts_ms: 60000 }];
  const tl = buildTimeline(scans, trip);
  check("h08_unknown_explicit",
    tl.i0.utc_ms === null && tl.i0.quality === "unknown" && tl.i0.basis === "none");
  // O trecho da chegada usa a CHEGADA como referência efetiva (a conta
  // "para trás" do §3 é a mesma conta forward pela referência posterior)
  const scans2 = [{ scan_key: "i1", boot_id: 0x3333, ts_ms: 5 * 60000 }];
  const tl2 = buildTimeline(scans2, trip);
  check("h08_arrival_ref_same_segment",
    tl2.i1.utc_ms === T_1400 - 5 * 60000 && tl2.i1.basis === "ref_forward");
}

// ---------- H10: hora inválida — pendência, sem invenção ------------------
{
  const trip = {
    v: 1, bike_id: "bpr-400007", trip_seq: 1,
    events: [
      { event_id: 257, kind: "departure", base_id: "base-a",
        civil_s: 0, civil_valid: false, counter_ms: 0, boot_id: 0x1111,
        method: "last_base_contact" },
      { event_id: 258, kind: "arrival", base_id: "base-a",
        civil_s: T_1400 / 1000, civil_valid: true, counter_ms: 30 * 60000,
        boot_id: 0x1111, method: "first_contact_time_ref" },
    ],
  };
  const r = reconstructTrip(trip);
  check("h10_pending_without_invention",
    r.duration_s === null && r.quality === "pending" &&
    r.pending_reason === "departure_without_valid_time");

  // Relógio inconsistente (chegada antes da saída): divergência registrada
  const bad = JSON.parse(JSON.stringify(trip));
  bad.events[0].civil_s = T_1403 / 1000;
  bad.events[0].civil_valid = true;
  const rb = reconstructTrip(bad);
  check("h10_non_monotonic_explicit",
    rb.duration_s === null && rb.pending_reason === "non_monotonic_civil_clock");
}

// ---------- H12: saída e chegada em estações diferentes -------------------
{
  const trip = {
    v: 1, bike_id: "bpr-410008", trip_seq: 1,
    events: [
      { event_id: 257, kind: "departure", base_id: "base-a",
        civil_s: S_1330, civil_valid: true, counter_ms: 0, boot_id: 0x1111,
        method: "last_base_contact" },
      { event_id: 258, kind: "arrival", base_id: "base-b",
        civil_s: T_1400 / 1000, civil_valid: true, counter_ms: 30 * 60000,
        boot_id: 0x1111, method: "first_contact_time_ref" },
    ],
  };
  const r = reconstructTrip(trip);
  check("h12_both_bases_kept_same_trip",
    r.duration_s === 1800 && trip.events[0].base_id === "base-a" &&
    trip.events[1].base_id === "base-b");
}

// ---------- H14: scans com o mesmo contador permanecem distintos ---------
{
  const trip = {
    v: 1, bike_id: "bpr-420009", trip_seq: 1,
    events: [
      { event_id: 257, kind: "departure", base_id: "base-a",
        civil_s: S_1330, civil_valid: true, counter_ms: 0, boot_id: 0x1111,
        method: "last_base_contact" },
    ],
  };
  const scans = [
    { scan_key: "i0", boot_id: 0x1111, ts_ms: 60000 },
    { scan_key: "i1", boot_id: 0x1111, ts_ms: 60000 }, // mesmo contador
  ];
  const tl = buildTimeline(scans, trip);
  check("h14_same_counter_distinct_points",
    tl.i0 !== undefined && tl.i1 !== undefined &&
    tl.i0.utc_ms === tl.i1.utc_ms && "i0" in tl && "i1" in tl);
}

// ---------- H16: legado sem boot/sem trecho — compat explícita -----------
{
  const trip = {
    v: 1, bike_id: "bpr-430010", trip_seq: 1,
    events: [
      { event_id: 257, kind: "departure", base_id: "base-a",
        civil_s: S_1330, civil_valid: true, counter_ms: 0, boot_id: 0,
        method: "last_base_contact" },
      { event_id: 258, kind: "arrival", base_id: "base-a",
        civil_s: T_1400 / 1000, civil_valid: true, counter_ms: 30 * 60000,
        boot_id: 0, method: "first_contact_time_ref" },
    ],
  };
  const r = reconstructTrip(trip);
  check("h16_legacy_trip_still_reconstructs", r.duration_s === 1800);
  // Scan legado sem boot (0): trecho único declarado — as âncoras de
  // saída e chegada convergem (saída+60s == chegada−29min); resultado
  // explícito, determinístico e sem throw
  const tl = buildTimeline([{ scan_key: "i0", boot_id: 0, ts_ms: 60000 }], trip);
  check("h16_legacy_scan_ref_explicit",
    tl.i0.basis === "ref_forward" && tl.i0.utc_ms === T_1330 + 60000);
}

// ---------- Patch §7: chegada REAL como finished_at da viagem do bot -----
{
  const bike = "bpr-440011";
  const origin = { sessionId: 900, sessionSalt: 12, startMs: 10000 };
  const rideId = rideCalculator.startRide(bike, origin);
  const PA = { latitude: -8.05, longitude: -34.88, accuracy: 20 };
  const PB = { latitude: -8.06, longitude: -34.89, accuracy: 20 };
  await rideCalculator.addPointToRide(bike, PA, 1000);
  await rideCalculator.addPointToRide(bike, PB, 61000);

  // Chegada REAL do evento (14h00) — não a hora de processamento
  const ride = await rideCalculator.finishRide(bike, T_1400, {
    stationId: "base-b", arrivalCivilMs: T_1400,
  });
  check("patch7_finished_at_is_arrival_event", ride.finished_at === T_1400);
  check("patch7_basis_arrival_event", ride.finished_basis === "arrival_event_clock");
  const month = new Date(T_1400).toISOString().slice(0, 7);
  check("patch7_period_from_arrival", ride.period_month === month &&
    ride.period_basis === "arrival_event_clock");

  // H04 no bot: replay no "dia seguinte" — mesmo registro, mesma hora
  const rideId2 = rideCalculator.startRide(bike, origin);
  await rideCalculator.addPointToRide(bike, PA, 1000);
  await rideCalculator.addPointToRide(bike, PB, 61000);
  const replay = await rideCalculator.finishRide(bike, Date.now(), {
    stationId: "base-b", arrivalCivilMs: T_1400,
  });
  const saved = mockDb._read(`rides/${bike}/${rideId}`);
  check("h04_bot_replay_keeps_arrival",
    saved.finished_at === T_1400 && saved.finished_basis === "arrival_event_clock");
}

  for (const line of results) console.log(line);
  const bugs = results.filter((l) => l.endsWith("no(BUG)")).length;
  console.log(bugs === 0
    ? "== trip backend checks: sem BUG =="
    : `== trip backend checks: ${bugs} BUG ==`);
  process.exit(bugs === 0 ? 0 : 1);
}

main().catch((e) => { console.error("FALHA:", e); process.exit(2); });
