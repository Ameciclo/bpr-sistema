#!/usr/bin/env node
/*
 * Verificação nativa (host) — item 5 (M06) da especificação
 * correcoes_bpr_itens_2_4_5.md: enriquecimento geográfico sob escrita
 * concorrente. Compila os módulos REAIS (assembly.ts + dto.ts das
 * Functions) com o tsc do projeto e exercita a política de
 * identidade/versão/instalação (C245-10..C245-14, GEO-01..GEO-06).
 * Nenhum serviço real é contatado.
 */
const { execSync } = require("child_process");
const path = require("path");
const fs = require("fs");

const HERE = __dirname;
const ROOT = path.join(HERE, "..", "..");
const TSC = path.join(ROOT, "bot/functions/node_modules/.bin/tsc");
const OUT = path.join(HERE, "build", "ts-assembly");

// Compila SOMENTE os módulos puros (sem firebase-functions/admin/telegraf)
fs.rmSync(OUT, { recursive: true, force: true });
execSync(
  `"${TSC}" "${ROOT}/bot/functions/src/assembly.ts" ` +
  `"${ROOT}/bot/functions/src/dto.ts" ` +
  `--outDir "${OUT}" --module commonjs --target es2020 --skipLibCheck`,
  { stdio: "pipe" }
);

const {
  scanKey, hashNetworks, reassembleScans,
  resolveGeo, canInstallGeo, mergeLegacyExtras,
} = require(path.join(OUT, "assembly.js"));

const results = [];
function result(name, v) { results.push(`${name}: ${v}`); }
function check(name, ok) { result(name, ok ? "yes" : "no(BUG)"); }

const net = (bssid, rssi) => ({ ssid: "NET", bssid, rssi, channel: 6 });

// ---------- N05/GEO-01: chunks de um scan = UMA observação com scan_key ---
{
  const scans = reassembleScans([
    [ // fragmento 0: primeira metade do scan idx 3
      { idx: 3, ts: 47000, net_offset: 0, nets: [["A", "AA:00", -60, 6]] },
      { idx: 4, ts: 48000, net_offset: 0, nets: [["B", "BB:00", -70, 6]] },
    ],
    [ // fragmento 1: segunda metade do scan idx 3 (offset 1)
      { idx: 3, ts: 47000, net_offset: 1, nets: [["C", "CC:00", -80, 6]] },
    ],
  ]);
  result("n05_observations", String(scans.length)); // esperado 2
  const s3 = scans.find((s) => s.scan_key === "i3");
  check("n05_dense_scan_reassembled", s3 && s3.networks.length === 2);
  check("geo01_scan_key_from_producer_idx",
    scans.every((s) => s.scan_key === `i${s.idx}`));
  check("geo03_content_hash_present",
    scans.every((s) => /^[0-9a-f]{8}$/.test(s.content_hash)));
}

// ---------- C245-13: legado sem idx, ts repetido — sem undefined ----------
{
  const scans = reassembleScans([
    [ // legado: dois chunks SEM idx e com o MESMO ts → mesma observação
      // (fusão documentada do fallback — comportamento legado preservado)
      { ts: 9000, net_offset: 0, nets: [["A", "AA:00", -60, 6]] },
      { ts: 9000, net_offset: 1, nets: [["B", "BB:00", -70, 6]] },
      { ts: 9500, net_offset: 0, nets: [["C", "CC:00", -80, 6]] },
    ],
  ]);
  result("c245_13_legacy_observations", String(scans.length)); // 2 (fusão por ts)
  check("c245_13_no_undefined_keys",
    scans.every((s) => typeof s.scan_key === "string" && s.scan_key.length > 1));
  check("c245_13_nothing_discarded",
    scans.reduce((n, s) => n + s.networks.length, 0) === 3);
}

// ---------- C245-10/GEO-02: normalização repetida preserva enriquecimento --
{
  const fresh = reassembleScans([
    [{ idx: 0, ts: 1000, net_offset: 0, nets: [["A", "AA:00", -60, 6]] }],
  ]);
  // Conversor "gravou" coordenada entre a leitura e a gravação do
  // normalizador: no layout novo isso vive em geo/ (path próprio) — o
  // normalizador NUNCA toca lá; o legado embutido segue preservado pelo
  // merge defensivo com guarda de conteúdo
  const existing = [
    { scan_key: "i0", ts_ms: 1000, networks: fresh[0].networks,
      content_hash: fresh[0].content_hash,
      coordinates: { lat: -8.06, lng: -34.88 } }, // legado embutido
  ];
  const merged = mergeLegacyExtras(fresh, existing);
  check("c245_10_legacy_enrichment_survives",
    merged[0].coordinates && merged[0].coordinates.lat === -8.06);
  // Conteúdo divergente: extras NÃO herdados (GEO-04)
  const other = [
    { scan_key: "i0", ts_ms: 9999, coordinates: { lat: 1, lng: 2 } },
  ];
  const merged2 = mergeLegacyExtras(fresh, other);
  check("geo04_divergent_content_no_inheritance",
    merged2[0].coordinates === undefined);
}

// ---------- C245-12/GEO-03/GEO-04: resultado de revisão antiga ------------
{
  const scanNow = reassembleScans([
    [{ idx: 0, ts: 1000, net_offset: 0, nets: [["A", "AA:00", -60, 6]] }],
  ])[0];
  const oldHash = hashNetworks([net("AA:00", -95)]); // conteúdo ANTIGO

  // Resultado calculado sobre o conteúdo antigo não é instalado como atual
  check("c245_12_stale_result_not_installed",
    !canInstallGeo(oldHash, scanNow));
  // Resultado do conteúdo atual é instalado
  check("c245_12_fresh_result_installed",
    canInstallGeo(scanNow.content_hash, scanNow));
  // Observação desapareceu (remontagem sem ela): não instala
  check("c245_12_missing_scan_not_installed",
    !canInstallGeo(scanNow.content_hash, null));

  // resolveGeo: geo stale não é atribuído à observação nova
  const staleGeo = {
    lat: -8.0, lng: -34.8, accuracy: 30,
    base_hash: oldHash, source: "google_geolocation_v1", computed_at: 1,
  };
  const res = resolveGeo(scanNow, staleGeo);
  check("geo04_stale_not_silently_assigned",
    res.coordinates === null && res.status === "stale");
  // geo fresh do conteúdo atual
  const freshGeo = { ...staleGeo, base_hash: scanNow.content_hash };
  const res2 = resolveGeo(scanNow, freshGeo);
  check("geo03_fresh_matches_content",
    res2.status === "fresh" && res2.coordinates.lat === -8.0);
}

// ---------- GEO-05/C245-14: falha e ausência explícitas -------------------
{
  const scan = reassembleScans([
    [{ idx: 0, ts: 1000, net_offset: 0, nets: [["A", "AA:00", -60, 6]] }],
  ])[0];
  const missing = resolveGeo(scan, null);
  check("geo05_missing_is_explicit",
    missing.coordinates === null && missing.status === "missing");
  // Leitura com dados parcialmente enriquecidos: legado ainda legível
  const legacy = resolveGeo({ ...scan, coordinates: { lat: 1, lng: 2 } }, null);
  check("c245_14_legacy_readable",
    legacy.status === "legacy" && legacy.coordinates.lat === 1);
}

// ---------- C245-11: remontagem e geolocalização fora de ordem ------------
{
  // Duas remontagens (mesma origem → mesmo conteúdo) + geo instalado entre
  // elas: os dados-base convergem e o geo continua VÁLIDO (fresh)
  const chunks = [[{ idx: 0, ts: 1000, net_offset: 0, nets: [["A", "AA:00", -60, 6]] }]];
  const first = reassembleScans(chunks);
  const geoEntry = {
    lat: -8.05, lng: -34.88, accuracy: 25,
    base_hash: first[0].content_hash,
    source: "google_geolocation_v1", computed_at: Date.now(),
  };
  const second = reassembleScans(chunks); // remontagem idempotente
  const res = resolveGeo(second[0], geoEntry);
  check("c245_11_out_of_order_converges",
    res.status === "fresh" && second[0].content_hash === first[0].content_hash);
}

for (const line of results) console.log(line);
const bugs = results.filter((l) => l.endsWith("no(BUG)")).length;
console.log(bugs === 0
  ? "== item 5 checks: sem BUG =="
  : `== item 5 checks: ${bugs} BUG ==`);
process.exit(bugs === 0 ? 0 : 1);
