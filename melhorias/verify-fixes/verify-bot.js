#!/usr/bin/env node
/*
 * Verificação nativa (host) — correções do BOT (N11, N04) da revalidação.
 * Executa o rideCalculator REAL (bot/src/services/rideCalculator.js) com um
 * adaptador local de RTDB que implementa a semântica de transação do
 * Firebase (abort = return undefined; reexecução sobre o valor atual).
 * Nenhum Firebase/Telegram real é contatado. Não substitui homologação.
 */
const path = require("path");
const Module = require("module");

// ---- Adaptador local de RTDB (semântica de transação do SDK) -----------
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
    once: async (ev) => snap(read(p)),
    set: async (v) => { write(p, clone(v)); },
    update: async (obj) => {
      for (const [k, v] of Object.entries(obj)) write(p + "/" + k, clone(v));
    },
    child: (suffix) => makeRef(p + "/" + suffix),
    on: () => {}, off: () => {},
    // Transação estilo RTDB: fn(current); undefined => aborta
    transaction: async (fn) => {
      for (let i = 0; i < 10; i++) {
        const current = read(p);
        const res = fn(current === null ? null : clone(current));
        if (res === undefined) return { committed: false, snapshot: snap(read(p)) };
        write(p, clone(res));
        return { committed: true, snapshot: snap(res) };
      }
      return { committed: false, snapshot: snap(read(p)) };
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

// Interposição de módulos: firebase-admin (banco) e axios (HTTP pago)
const origLoad = Module._load;
Module._load = function (request, parent, isMain) {
  if (request === "firebase-admin") return mockAdmin;
  if (request === "axios") return { post: async () => { throw new Error("blocked"); } };
  return origLoad.apply(this, [request, parent, isMain]);
};

const ROOT = path.join(__dirname, "..", "..");
const rideCalculator = require(path.join(ROOT, "bot/src/services/rideCalculator.js"));

const results = [];
function result(name, v) { results.push(`${name}: ${v}`); }
function check(name, ok) { result(name, ok ? "yes" : "no(BUG)"); }

(async () => {
  // ---------- N11/E07: duas finalizações concorrentes = UMA contribuição --
  {
    const bike = "bpr-0a1111";
    rideCalculator.startRide(bike, 10000);
    await rideCalculator.addPointToRide(bike, { latitude: -8.05, longitude: -34.88, accuracy: 20 }, 1000);
    await rideCalculator.addPointToRide(bike, { latitude: -8.06, longitude: -34.89, accuracy: 20 }, 61000);

    // Duas finalizações da MESMA viagem em concorrência (E07 da revalidação:
    // o par exists+set observava ausência nas duas e somava o contador 2×)
    const [r1, r2] = await Promise.all([
      rideCalculator.finishRide(bike, Date.now()),
      rideCalculator.finishRide(bike, Date.now()),
    ]);

    const ride = mockDb._read(`rides/${bike}/ride_10000`);
    result("n11_ride_saved_once", ride !== null ? "yes" : "no(BUG)");
    const metrics = mockDb._read(`bikes/${bike}/metrics`) || {};
    result("n11_bike_rides_total", String(metrics.rides_total)); // esperado 1
    const stats = mockDb._read("public_stats") || {};
    result("n11_public_total_rides_month", String(stats.total_rides_month)); // esperado 1
    const month = mockDb._read("public_stats/monthly") || {};
    const anyMonth = Object.values(month)[0] || {};
    result("n11_month_total_rides", String(anyMonth.total_rides)); // esperado 1

    // N04: duração no domínio do boot (pontos boot+1s..boot+61s => 1 min)
    result("n04_duration_min_single_domain", String(ride.duration_min)); // esperado 1
    result("n04_month_bucket_from_server_anchor",
      Object.keys(month).length === 1 && !Object.keys(month)[0].startsWith("1970")
        ? "yes" : "no(BUG)");
  }

  // ---------- N11/E08: primeiro resumo não gera NaN -----------------------
  {
    const bike = "bpr-0b2222";
    rideCalculator.startRide(bike, 20000);
    await rideCalculator.addPointToRide(bike, { latitude: -8.05, longitude: -34.88, accuracy: 20 }, 1000);
    await rideCalculator.addPointToRide(bike, { latitude: -8.07, longitude: -34.90, accuracy: 20 }, 121000);

    // Simula o banco com o pai public_stats JÁ existente (ramo monthly de
    // outro dado) mas SEM contadores gerais — o caso que gravava NaN
    mockDb._write("public_stats/monthly/2099-01/total_rides", 3);
    mockDb._write("public_stats/monthly/2099-01/km", 10);
    mockDb._write("public_stats/last_update", 1);

    await rideCalculator.finishRide(bike, Date.now());
    const stats = mockDb._read("public_stats") || {};
    const noNaN = Number.isFinite(stats.total_rides_month) &&
      Number.isFinite(stats.km_month) &&
      Number.isFinite(stats.co2_saved_month_g);
    check("n11_first_summary_no_nan", noNaN);
    result("n11_general_counters_after_first_summary", String(stats.total_rides_month));
  }

  // ---------- N11: agregação interrompida é reconciliável -----------------
  {
    const bike = "bpr-0c3333";
    const pointA = { latitude: -8.05, longitude: -34.88, accuracy: 20 };
    const pointB = { latitude: -8.06, longitude: -34.89, accuracy: 20 }; // ~1,5 km
    const rideStart = async () => {
      rideCalculator.startRide(bike, 30000);
      await rideCalculator.addPointToRide(bike, pointA, 1000);
      await rideCalculator.addPointToRide(bike, pointB, 181000);
    };

    await rideStart();
    const first = await rideCalculator.finishRide(bike, Date.now());
    if (!first) throw new Error("cenário de reparo: viagem inicial descartada");

    // Crash ENTRE a criação da viagem e a agregação: métricas somem
    mockDb._write(`bikes/${bike}/metrics`, null);
    mockDb._write("public_stats", null);

    // Replay da mesma sessão: a viagem existe, mas a contribuição é reparada
    await rideStart();
    await rideCalculator.finishRide(bike, Date.now());

    const metrics = mockDb._read(`bikes/${bike}/metrics`) || {};
    const stats = mockDb._read("public_stats") || {};
    check("n11_repair_rides_total_once",
      metrics.rides_total === 1); // nem 0 (esquecido) nem 2 (duplicado)
    check("n11_repair_public_total_once",
      stats.total_rides_month === 1);

    // Replay adicional: nada duplica (token por viagem)
    await rideStart();
    await rideCalculator.finishRide(bike, Date.now());
    const metrics2 = mockDb._read(`bikes/${bike}/metrics`) || {};
    check("n11_replay_does_not_duplicate", metrics2.rides_total === 1);
  }

  // ---------- N04: conversão de unidade ÚNICA por origem ------------------
  {
    // v2 declara ms: fromV2Ms não multiplica (a heurística antiga virava
    // 10.000 ms → 10.000.000 → 10.000.000.000)
    result("n04_from_v2_ms_no_double_conversion",
      String(rideCalculator.fromV2Ms(10000))); // esperado 10000
    result("n04_from_legacy_sec_converts_once",
      String(rideCalculator.fromLegacySec(10))); // esperado 10000
  }

  for (const line of results) console.log(line);
  const bugs = results.filter((l) => l.endsWith("no(BUG)")).length;
  console.log(bugs === 0 ? "== bot checks: sem BUG ==" : `== bot checks: ${bugs} BUG ==`);
  process.exit(bugs === 0 ? 0 : 1);
})().catch((e) => { console.error("FALHA:", e); process.exit(2); });
