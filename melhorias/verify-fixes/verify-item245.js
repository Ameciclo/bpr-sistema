#!/usr/bin/env node
/*
 * Verificação nativa (host) — itens 2 e 4 (M03/M04) da especificação
 * correcoes_bpr_itens_2_4_5.md: identidade canônica de viagem e período
 * autoritativo no reprocessamento. Executa o rideCalculator REAL e o
 * MIGRADOR REAL com um adaptador local de RTDB (semântica de transação do
 * SDK: abort = return undefined). Nenhum serviço real é contatado.
 * Cenários: C245-01..C245-09.
 */
const path = require("path");
const Module = require("module");

// ---- Adaptador local de RTDB (idêntico ao verify-bot.js) ------------------
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
    path: p,
    once: async () => snap(read(p)),
    set: async (v) => { write(p, clone(v)); },
    update: async (obj) => {
      for (const [k, v] of Object.entries(obj)) write(p + "/" + k, clone(v));
    },
    child: (suffix) => makeRef(p + "/" + suffix),
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
  apps: [],
  initializeApp: () => { mockAdmin.apps.push({}); },
  database: () => mockDb,
  credential: { cert: () => ({}) },
};

const origLoad = Module._load;
Module._load = function (request, parent, isMain) {
  if (request === "firebase-admin") return mockAdmin;
  if (request === "axios") return { post: async () => { throw new Error("blocked"); } };
  return origLoad.apply(this, [request, parent, isMain]);
};

const ROOT = path.join(__dirname, "..", "..");
const rideCalculator = require(path.join(ROOT, "bot/src/services/rideCalculator.js"));
const { migrateRideIdentity } = require(path.join(ROOT, "bot/migrate-ride-identity.js"));

const results = [];
function result(name, v) { results.push(`${name}: ${v}`); }
function check(name, ok) { result(name, ok ? "yes" : "no(BUG)"); }

// Âncoras absolutas de teste (UTC): janeiro e fevereiro de 2026
const JAN_MS = Date.UTC(2026, 0, 15, 12, 0, 0);
const FEV_MS = Date.UTC(2026, 1, 20, 12, 0, 0);
const realNow = Date.now;

function fixClock(ms) { Date.now = () => ms; }
function restoreClock() { Date.now = realNow; }

async function rideFlow(bike, session, pointA, pointB, delivery) {
  const rideId = rideCalculator.startRide(bike, session);
  if (!rideId) return null;
  await rideCalculator.addPointToRide(bike, pointA, session.startMs + 1000);
  await rideCalculator.addPointToRide(bike, pointB, session.startMs + 61000);
  return rideCalculator.finishRide(bike, Date.now(), delivery);
}

const PA = { latitude: -8.05, longitude: -34.88, accuracy: 20 };
const PB = { latitude: -8.06, longitude: -34.89, accuracy: 20 }; // ~1,5 km

(async () => {
  // ---------- C245-01: duas bikes, mesmo início relativo 10000 ms ---------
  // Antes: ride_10000 colidia nos tokens PÚBLICOS e a 2ª bike não contribuía
  {
    fixClock(JAN_MS);
    await rideFlow("bpr-0a0001", { sessionId: 100, sessionSalt: 7, startMs: 10000 }, PA, PB);
    await rideFlow("bpr-0b0002", { sessionId: 200, sessionSalt: 9, startMs: 10000 }, PA, PB);
    restoreClock();

    const stats = mockDb._read("public_stats") || {};
    result("c245_01_two_bikes_two_public_contributions",
      String(stats.total_rides)); // esperado 2
    const jan = mockDb._read("public_stats/monthly/2026-01") || {};
    result("c245_01_monthly_january_rides", String(jan.total_rides)); // esperado 2
    const r1 = mockDb._read("rides/bpr-0a0001");
    const r2 = mockDb._read("rides/bpr-0b0002");
    check("c245_01_distinct_histories_both_persisted",
      !!r1 && !!r2 && Object.keys(r1).length === 1 && Object.keys(r2).length === 1);
  }

  // ---------- C245-02: mesma bike, sessões distintas, mesmo start relativo -
  // Antes: ride_10000 da 2ª sessão colidia com o da 1ª no HISTÓRICO
  {
    fixClock(JAN_MS);
    await rideFlow("bpr-0c0003", { sessionId: 300, sessionSalt: 1, startMs: 10000 }, PA, PB);
    await rideFlow("bpr-0c0003", { sessionId: 301, sessionSalt: 2, startMs: 10000 }, PA, PB);
    restoreClock();

    const rides = mockDb._read("rides/bpr-0c0003") || {};
    const ids = Object.keys(rides);
    check("c245_02_same_start_distinct_identities", ids.length === 2); // salts distintos
    const m = mockDb._read("bikes/bpr-0c0003/metrics") || {};
    result("c245_02_bike_metrics_rides_total", String(m.rides_total)); // esperado 2
    const stats = mockDb._read("public_stats") || {};
    result("c245_02_public_total", String(stats.total_rides)); // esperado 4 (2+2)
  }

  // ---------- C245-03: mesma origem por outra estação/consumidor ----------
  {
    fixClock(JAN_MS);
    const first = await rideFlow("bpr-0d0004",
      { sessionId: 400, sessionSalt: 3, startMs: 5000 }, PA, PB, { stationId: "base-a" });
    // Reprocessamento da MESMA origem entregue por OUTRA estação
    const again = await rideFlow("bpr-0d0004",
      { sessionId: 400, sessionSalt: 3, startMs: 5000 }, PA, PB, { stationId: "base-b" });
    restoreClock();

    const rides = mockDb._read("rides/bpr-0d0004") || {};
    check("c245_03_same_ride_single_history", Object.keys(rides).length === 1);
    const ride = Object.values(rides)[0] || {};
    const bases = ride.delivery_bases || {};
    check("c245_03_delivery_provenance_kept",
      bases["base-a"] === true && bases["base-b"] === true);
    const stats = mockDb._read("public_stats") || {};
    const contributed = stats.contributed_rides || {};
    const rideIds = Object.keys(contributed).filter((id) => id.includes("bpr-0d0004"));
    result("c245_03_single_public_contribution", String(rideIds.length)); // esperado 1
    check("c245_03_station_not_in_identity",
      !Object.keys(rides)[0].includes("base"));
  }

  // ---------- C245-04: dois trabalhadores simultâneos ----------------------
  {
    fixClock(JAN_MS);
    const bike = "bpr-0e0005";
    rideCalculator.startRide(bike, { sessionId: 500, sessionSalt: 4, startMs: 8000 });
    await rideCalculator.addPointToRide(bike, PA, 9000);
    await rideCalculator.addPointToRide(bike, PB, 61000);
    const [a, b] = await Promise.all([
      rideCalculator.finishRide(bike, Date.now()),
      rideCalculator.finishRide(bike, Date.now()),
    ]);
    restoreClock();
    const rides = mockDb._read(`rides/${bike}`) || {};
    check("c245_04_single_canonical_entity", Object.keys(rides).length === 1);
    const m = mockDb._read(`bikes/${bike}/metrics`) || {};
    result("c245_04_metrics_counted_once", String(m.rides_total)); // esperado 1
  }

  // ---------- C245-05: reinício entre criar viagem e agregar ---------------
  {
    fixClock(JAN_MS);
    const bike = "bpr-0f0006";
    const sess = { sessionId: 600, sessionSalt: 5, startMs: 7000 };
    const rideId = rideCalculator.startRide(bike, sess);
    await rideCalculator.addPointToRide(bike, PA, 7100);
    await rideCalculator.addPointToRide(bike, PB, 68000);
    // Cria a viagem e "crasha" ANTES dos agregados: simulação direta —
    // grava a viagem com a mesma transação que o finishRide usaria
    const rideRef = mockDb.ref(`rides/${bike}/${rideId}`);
    await rideRef.transaction((cur) => (cur !== null ? undefined : {
      rideId, ride_rule: "session_v1", km: 1.565, co2_saved_g: 227,
      period_month: "2026-01", finished_at: JAN_MS,
    }));
    // Reinício do consumidor: activeRides vazio; a sessão é reprocessada
    rideCalculator.cancelRide(bike, "restart");
    const rideId2 = rideCalculator.startRide(bike, sess);
    await rideCalculator.addPointToRide(bike, PA, 7100);
    await rideCalculator.addPointToRide(bike, PB, 68000);
    await rideCalculator.finishRide(bike, Date.now());
    restoreClock();

    const m = mockDb._read(`bikes/${bike}/metrics`) || {};
    result("c245_05_repair_completes_once", String(m.rides_total)); // esperado 1
    const stats = mockDb._read("public_stats") || {};
    const ids = Object.keys(stats.contributed_rides || {})
      .filter((id) => id.includes("bpr-0f0006"));
    result("c245_05_public_contribution_once", String(ids.length)); // esperado 1
  }

  // ---------- C245-06/C245-08: janeiro reprocessada em fevereiro -----------
  {
    // Estado limpo para o cenário do período
    mockDb._write("public_stats", null);
    mockDb._write("bikes", null);
    mockDb._write("rides", null);

    fixClock(JAN_MS);
    const bike = "bpr-110007";
    const sess = { sessionId: 700, sessionSalt: 6, startMs: 3000 };
    rideCalculator.startRide(bike, sess);
    await rideCalculator.addPointToRide(bike, PA, 3100);
    await rideCalculator.addPointToRide(bike, PB, 64000);
    const created = await rideCalculator.finishRide(bike, Date.now());
    const janKm = created.km;
    restoreClock();

    // Gravação interrompida entre histórico e buckets (C245-07 setup):
    // apaga só o monthly e o geral — o reparo deve completá-los em JANEIRO
    mockDb._write("public_stats/monthly", null);
    mockDb._write("public_stats/contributed_rides", null);
    mockDb._write("public_stats/total_rides", null);
    mockDb._write("public_stats/km_total", null);
    mockDb._write("public_stats/co2_saved_g", null);

    // Reprocessamento em FEVEREIRO com pontos DIFERENTES (km divergente —
    // C245-08: a versão canônica de janeiro prevalece). Ponto a ~3 km de PA
    // (dentro do filtro de outlier 0,01–5 km, e ≠ do PB original)
    fixClock(FEV_MS);
    const bikeReplay = "bpr-110007";
    rideCalculator.startRide(bikeReplay, sess);
    await rideCalculator.addPointToRide(bikeReplay, PA, 3100);
    const PC = { latitude: -8.07, longitude: -34.90, accuracy: 20 }; // ~3,1 km
    await rideCalculator.addPointToRide(bikeReplay, PC, 124000);
    await rideCalculator.finishRide(bikeReplay, Date.now());
    restoreClock();

    const jan = mockDb._read("public_stats/monthly/2026-01") || {};
    const fev = mockDb._read("public_stats/monthly/2026-02");
    const stats = mockDb._read("public_stats") || {};
    result("c245_06_january_rides", String(jan.total_rides)); // esperado 1
    result("c245_06_february_rides", String(fev ? fev.total_rides : 0)); // esperado 0
    result("c245_06_general_total", String(stats.total_rides)); // esperado 1
    result("c245_08_canonical_km_used", String(jan.km)); // esperado janKm (não o da tentativa)
    check("c245_08_attempt_values_not_applied",
      Number(jan.km) === Number(janKm));
    check("c245_07_repair_completed_buckets",
      jan.total_rides === 1 && stats.total_rides === 1);
    // MES-05: espelho legado representa o bucket do período da viagem
    check("c245_06_mirror_reflects_january_bucket",
      stats.current_month === "2026-01" &&
      stats.total_rides_month === 1 && Number(stats.km_month) === Number(janKm));
  }

  // ---------- ID-07: origem ausente → pendência explícita, sem viagem ------
  {
    const ok = rideCalculator.startRide("bpr-120008", { startMs: 10000 });
    check("id07_missing_origin_rejected", ok === null);
    check("id07_no_ride_created",
      mockDb._read("rides/bpr-120008") === null);
  }

  // ---------- C245-09: migração de legado com IDs textuais iguais ----------
  {
    // Estado ISOLADO: duas bikes legado com ride_10000 cada (colisão
    // textual); token público antigo só permitiu UMA contribuição
    mockDb._write("public_stats", null);
    mockDb._write("rides", null);
    mockDb._write("bikes", null);
    mockDb._write("public_stats", {
      total_rides_month: 1, km_month: 1.5, co2_saved_month_g: 217,
      current_month: "2026-01",
      contributed_rides: { ride_10000: true },
      monthly: { "2026-01": { total_rides: 1, km: 1.5, co2_saved_g: 217 } },
    });
    mockDb._write("rides/bpr-210009/ride_10000",
      { start_ts: 10000, km: 1.5, co2_saved_g: 217, finished_at: JAN_MS });
    mockDb._write("rides/bpr-220010/ride_10000",
      { start_ts: 10000, km: 2.2, co2_saved_g: 319, finished_at: JAN_MS });
    mockDb._write("bikes/bpr-210009/sessions_v2/100-7",
      { session_id: 100, session_salt: 7, session_start_ms: 10000 });
    mockDb._write("bikes/bpr-220010/sessions_v2/100-7",
      { session_id: 100, session_salt: 7, session_start_ms: 10000 });
    mockDb._write("bikes/bpr-210009/metrics",
      { km_total: 1.5, rides_total: 1, contributed_rides: { ride_10000: true } });

    // Dry-run: apenas relatório
    const dry = await migrateRideIdentity(mockDb, { apply: false });
    check("c245_09_dryrun_reports_both", dry.migrated === 2 && dry.pending.length === 0);
    check("c245_09_dryrun_no_writes",
      mockDb._read("rides/bpr-210009/ride_session_v1_bpr-210009_100-7") === null);

    // Apply: ambas preservadas com identidades canônicas DISTINTAS
    const applied = await migrateRideIdentity(mockDb, { apply: true });
    const r1 = mockDb._read("rides/bpr-210009/ride_session_v1_bpr-210009_100-7");
    const r2 = mockDb._read("rides/bpr-220010/ride_session_v1_bpr-220010_100-7");
    check("c245_09_both_preserved_canonical", !!r1 && !!r2);
    check("c245_09_legacy_kept_as_alias",
      (mockDb._read("rides/bpr-210009/ride_10000") || {}).migrated_to !== undefined);
    const stats = mockDb._read("public_stats") || {};
    // Agregado RECONSTRUÍDO a partir das DUAS viagens (antes: total 1 perdido)
    result("c245_09_rebuilt_total_rides", String(stats.total_rides)); // esperado 2
    const jan = mockDb._read("public_stats/monthly/2026-01") || {};
    result("c245_09_rebuilt_january", String(jan.total_rides)); // esperado 2
    check("c245_09_tokens_canonical",
      Object.keys(stats.contributed_rides || {}).every((id) =>
        id.startsWith("ride_session_v1_")) &&
      Object.keys(stats.contributed_rides || {}).length === 2);
  }

  restoreClock();
  for (const line of results) console.log(line);
  const bugs = results.filter((l) => l.endsWith("no(BUG)")).length;
  console.log(bugs === 0
    ? "== item 2/4 checks: sem BUG =="
    : `== item 2/4 checks: ${bugs} BUG ==`);
  process.exit(bugs === 0 ? 0 : 1);
})().catch((e) => { console.error("FALHA:", e); process.exit(2); });
