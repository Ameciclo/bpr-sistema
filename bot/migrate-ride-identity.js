/*
 * BPR Sistema — Migração da identidade de viagem (item 2/M03)
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

/*
 * Migra viagens legado (`ride_{startMs}` — colidia entre bikes e entre
 * sessões da mesma bike) para a identidade canônica
 * `ride_session_v1_{bikeId}_{sessionId}-{salt}` (determinística da origem).
 *
 * Regras (especificação melhorias/correcoes_bpr_itens_2_4_5.md §2):
 * 1. Inventaria rides/{bike}/{} e os tokens atuais.
 * 2. Associação inequívoca legado→canônica por (bike + session_start_ms ==
 *    start_ts em sessions_v2 DA MESMA bike). Ambíguo/ausente = PENDÊNCIA
 *    registrada com evidência; nada é fundido, apagado ou inventado.
 * 3. O path legado recebe `migrated_to` (alias/proveniência), conteúdo
 *    preservado para leitores antigos.
 * 4. Agregados (bikes/{id}/metrics e public_stats) são RECONSTRUÍDOS a
 *    partir das viagens únicas recuperáveis — renomear tokens sem recalcular
 *    não corrige total que já perdeu contribuição (C245-09).
 * 5. Ambíguas ficam FORA dos agregados reconstruídos e na lista de pendências.
 *
 * Uso: node migrate-ride-identity.js [--apply]  (default: dry-run + relatório)
 */

const RIDE_RULE = 'session_v1';

function canonicalRideId(bikeId, sessionId, salt) {
  return `ride_${RIDE_RULE}_${bikeId}_${sessionId}-${salt}`;
}

function monthKeyOf(ms) {
  const when = new Date(ms);
  return `${when.getUTCFullYear()}-${String(when.getUTCMonth() + 1).padStart(2, '0')}`;
}

const isLegacyRideId = (id) => /^ride_\d+$/.test(id);
const isCanonicalRideId = (id) => id && id.startsWith(`ride_${RIDE_RULE}_`);

async function readPath(db, path) {
  const snap = await db.ref(path).once('value');
  return snap.val();
}

/**
 * Executa a migração completa contra o banco informado.
 * @param {object} db Instância de database (admin SDK ou adaptador de teste)
 * @param {object} opts {apply: boolean} — false = dry-run (só relatório)
 * @return {Promise<object>} relatório antes/depois + pendências
 */
async function migrateRideIdentity(db, opts = {}) {
  const apply = !!opts.apply;
  const report = {
    apply,
    bikes: [],
    migrated: 0,
    alreadyCanonical: 0,
    pending: [],
    aggregates: { before: null, after: null, rebuilt: false },
  };

  // 1. Inventário de viagens por bike
  const ridesByBike = (await readPath(db, 'rides')) || {};

  for (const [bikeId, ridesObj] of Object.entries(ridesByBike)) {
    if (!ridesObj || typeof ridesObj !== 'object') continue;
    const bikeReport = { bikeId, legacy: [], canonical: 0 };

    // Sessões desta bike — base da associação inequívoca
    const sessions = (await readPath(db, `bikes/${bikeId}/sessions_v2`)) || {};

    // Índice session_start_ms -> [sessões] (só chaves {sid}-{salt})
    const byStartMs = new Map();
    for (const [sessionKey, session] of Object.entries(sessions)) {
      if (!session || typeof session !== 'object') continue;
      const sid = Number(session.session_id);
      const salt = Number(session.session_salt);
      const startMs = Number(session.session_start_ms);
      if (!Number.isFinite(sid) || !Number.isFinite(salt) || !Number.isFinite(startMs)) {
        continue;
      }
      const list = byStartMs.get(startMs) || [];
      list.push({ sessionKey, sid, salt });
      byStartMs.set(startMs, list);
    }

    for (const [oldRideId, ride] of Object.entries(ridesObj)) {
      if (!ride || typeof ride !== 'object') continue;

      if (isCanonicalRideId(oldRideId)) {
        bikeReport.canonical++;
        report.alreadyCanonical++;
        continue;
      }
      if (!isLegacyRideId(oldRideId)) continue;

      // 2. Associação inequívoca: sessão da MESMA bike com o mesmo
      // session_start_ms (start_ts legado era exatamente esse valor)
      const startTs = Number(ride.start_ts);
      const matches = Number.isFinite(startTs)
        ? (byStartMs.get(startTs) || []) : [];

      if (matches.length !== 1) {
        // 5. Ambíguo/ausente: pendência com evidência, registro preservado
        const pending = {
          bikeId,
          oldRideId,
          start_ts: ride.start_ts,
          reason: matches.length === 0
            ? 'no_matching_session' : 'ambiguous_multiple_sessions',
          matches: matches.map((m) => m.sessionKey),
        };
        report.pending.push(pending);
        bikeReport.legacy.push({ oldRideId, action: 'pending', ...pending });
        if (apply) {
          await db.ref(`migrations/pending/ride_identity/${bikeId}/${oldRideId}`)
            .set({ ...pending, ride_snapshot: ride, detected_at: Date.now() });
        }
        continue;
      }

      const { sid, salt } = matches[0];
      const newRideId = canonicalRideId(bikeId, sid, salt);
      const destExisting = await readPath(db, `rides/${bikeId}/${newRideId}`);

      if (destExisting) {
        // Destino já existe: não sobrescreve — registra pendência
        const pending = {
          bikeId,
          oldRideId,
          reason: 'canonical_destination_already_exists',
          newRideId,
        };
        report.pending.push(pending);
        bikeReport.legacy.push({ oldRideId, action: 'pending', ...pending });
        if (apply) {
          await db.ref(`migrations/pending/ride_identity/${bikeId}/${oldRideId}`)
            .set({ ...pending, ride_snapshot: ride, detected_at: Date.now() });
        }
        continue;
      }

      // MES-01 (legado): período atribuído a partir da âncora PERSISTIDA na
      // viagem; sem âncora, mês de registro assumido (declarado, não comprovado)
      const refMs = Number(ride.finished_at) || 0;
      const period = {
        month: monthKeyOf(refMs > 0 ? refMs : Date.now()),
        basis: refMs > 0 ? 'legacy_finished_at' : 'legacy_unknown_assumed_registration',
        ref_ms: refMs,
      };

      const migrated = {
        ...ride,
        rideId: newRideId,
        ride_rule: RIDE_RULE,
        origin: { bike_id: bikeId, session_id: sid, session_salt: salt },
        period_month: period.month,
        period_basis: period.basis,
        period_rule: 'registration_month_v1',
        period_ref_ms: refMs,
        migrated_from: oldRideId,
      };

      bikeReport.legacy.push({
        oldRideId, action: 'migrated', newRideId, period: period.month,
      });
      report.migrated++;

      if (apply) {
        // 3. Alias/proveniência no path antigo (conteúdo preservado)
        await db.ref(`rides/${bikeId}/${oldRideId}`)
          .update({ migrated_to: newRideId, ride_rule: RIDE_RULE });
        // Nova identidade canônica (criada apenas se ausente — conferido acima)
        await db.ref(`rides/${bikeId}/${newRideId}`).set(migrated);
      }
    }
    report.bikes.push(bikeReport);
  }

  // 4. Reconstrução dos agregados a partir das viagens únicas recuperáveis
  report.aggregates.before = {
    public: await readPath(db, 'public_stats'),
    metrics: await readPath(db, 'bikes'),
  };

  // Estado pós-migração (em memória): viagens canônicas completas
  const canonicalRides = [];
  for (const [bikeId, ridesObj] of Object.entries(ridesByBike)) {
    if (!ridesObj || typeof ridesObj !== 'object') continue;
    const sessions = (await readPath(db, `bikes/${bikeId}/sessions_v2`)) || {};
    const byStartMs = new Map();
    for (const [sessionKey, session] of Object.entries(sessions)) {
      if (!session || typeof session !== 'object') continue;
      const sid = Number(session.session_id);
      const salt = Number(session.session_salt);
      const startMs = Number(session.session_start_ms);
      if (Number.isFinite(sid) && Number.isFinite(salt) && Number.isFinite(startMs)) {
        byStartMs.set(startMs, { sid, salt });
      }
    }
    for (const [oldRideId, ride] of Object.entries(ridesObj)) {
      if (!ride || typeof ride !== 'object') continue;
      if (isCanonicalRideId(oldRideId)) {
        canonicalRides.push({ bikeId, rideId: oldRideId, ride });
        continue;
      }
      if (!isLegacyRideId(oldRideId)) continue;
      const startTs = Number(ride.start_ts);
      const match = Number.isFinite(startTs) ? byStartMs.get(startTs) : null;
      if (!match) continue; // ambígua/sem sessão: fora dos agregados (pendente)
      const newRideId = canonicalRideId(bikeId, match.sid, match.salt);
      const refMs = Number(ride.finished_at) || 0;
      canonicalRides.push({
        bikeId,
        rideId: newRideId,
        ride: {
          ...ride,
          km: Number(ride.km) || 0,
          co2_saved_g: Number(ride.co2_saved_g) || 0,
          period_month: monthKeyOf(refMs > 0 ? refMs : Date.now()),
        },
      });
    }
  }

  // Métricas por bike (soma das viagens únicas recuperáveis)
  const metricsAfter = {};
  const publicAfter = { monthly: {}, contributed_rides: {} };
  let totalRides = 0;
  let totalKm = 0;
  let totalCo2 = 0;
  let latestMonth = null;

  for (const { bikeId, rideId, ride } of canonicalRides) {
    const m = (metricsAfter[bikeId] = metricsAfter[bikeId] ||
      { km_total: 0, rides_total: 0, contributed_rides: {} });
    m.km_total = Math.round((m.km_total + (Number(ride.km) || 0)) * 1000) / 1000;
    m.rides_total += 1;
    m.contributed_rides[rideId] = true;
    m.last_ride = Date.now();

    const month = ride.period_month || monthKeyOf(Number(ride.finished_at) || Date.now());
    const bucket = (publicAfter.monthly[month] = publicAfter.monthly[month] ||
      { total_rides: 0, km: 0, co2_saved_g: 0, contributed_rides: {} });
    bucket.total_rides += 1;
    bucket.km = Math.round((bucket.km + (Number(ride.km) || 0)) * 1000) / 1000;
    bucket.co2_saved_g += Number(ride.co2_saved_g) || 0;
    bucket.contributed_rides[rideId] = true;

    publicAfter.contributed_rides[rideId] = true;
    totalRides += 1;
    totalKm = Math.round((totalKm + (Number(ride.km) || 0)) * 1000) / 1000;
    totalCo2 += Number(ride.co2_saved_g) || 0;
    if (!latestMonth || month > latestMonth) latestMonth = month;
  }

  publicAfter.total_rides = totalRides;
  publicAfter.km_total = totalKm;
  publicAfter.co2_saved_g = totalCo2;
  publicAfter.current_month = latestMonth;
  if (latestMonth && publicAfter.monthly[latestMonth]) {
    // MES-05: espelho legado do bucket mais recente
    publicAfter.total_rides_month = publicAfter.monthly[latestMonth].total_rides;
    publicAfter.km_month = publicAfter.monthly[latestMonth].km;
    publicAfter.co2_saved_month_g = publicAfter.monthly[latestMonth].co2_saved_g;
  }
  publicAfter.last_update = Date.now();
  publicAfter.rebuilt_from = 'migrate_ride_identity_v1';

  report.aggregates.after = { public: publicAfter, metrics: metricsAfter };
  report.aggregates.canonicalRides = canonicalRides.length;

  if (apply) {
    // Métricas por bike (sobrescreve o nó de métricas reconstruído)
    for (const [bikeId, metrics] of Object.entries(metricsAfter)) {
      await db.ref(`bikes/${bikeId}/metrics`).set(metrics);
    }
    // Agregado público reconstruído (substitui contadores/tokens antigos;
    // ramos não reconstruídos — ex.: dados de outra natureza — preservados
    // via update por campo)
    await db.ref('public_stats').update(publicAfter);
    report.aggregates.rebuilt = true;
  }

  return report;
}

module.exports = { migrateRideIdentity, canonicalRideId, RIDE_RULE };

// ---------------------------------------------------------------------------
// CLI: node migrate-ride-identity.js [--apply]
// ---------------------------------------------------------------------------
if (require.main === module) {
  const apply = process.argv.includes('--apply');
  const firebaseService = require('./src/config/firebase');

  (async () => {
    if (!firebaseService.db) {
      console.error('❌ Firebase não conectado — verifique as variáveis de ambiente');
      process.exit(1);
    }
    console.log(apply
      ? '🚨 APLICANDO migração de identidade de viagem (gravações no banco)'
      : '🔎 DRY-RUN da migração de identidade de viagem (nenhuma gravação)');

    const report = await migrateRideIdentity(firebaseService.db, { apply });

    console.log('\n=== Viagens ===');
    for (const bike of report.bikes) {
      console.log(`Bike ${bike.bikeId}: ${bike.canonical} canônicas, ` +
        `${bike.legacy.length} legado`);
      for (const item of bike.legacy) {
        if (item.action === 'migrated') {
          console.log(`  ✅ ${item.oldRideId} → ${item.newRideId} (mês ${item.period})`);
        } else {
          console.log(`  ⚠️ ${item.oldRideId}: PENDENTE (${item.reason}) — preservada`);
        }
      }
    }
    console.log(`\nMigradas: ${report.migrated} | já canônicas: ${report.alreadyCanonical} | ` +
      `pendentes: ${report.pending.length}`);
    console.log(`Agregados reconstruídos a partir de ${report.aggregates.canonicalRides} ` +
      `viagens únicas recuperáveis`);
    console.log(`Pós-migração: total_rides=${report.aggregates.after.public.total_rides} ` +
      `km=${report.aggregates.after.public.km_total} ` +
      `co2=${report.aggregates.after.public.co2_saved_g}`);
    if (!apply) {
      console.log('\n(Dry-run — execute com --apply para gravar. Ambíguas ficam em ' +
        'migrations/pending/ride_identity/ e NÃO entram nos agregados.)');
    }
  })().catch((e) => { console.error('FALHA:', e); process.exit(2); });
}
