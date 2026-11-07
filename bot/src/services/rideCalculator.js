const geolocationService = require('./geolocation');
const firebaseService = require('../config/firebase');

// Item 2 (M03)/ID-03: versão da REGRA que deriva a viagem da unidade de
// coleta. A regra atual deriva UMA viagem por sessão (origem = bike +
// session_id + session_salt); mudanças na regra bumpam a versão aqui e o
// rideId canônico carrega essa versão.
const RIDE_RULE = 'session_v1';

class RideCalculator {
  constructor() {
    this.activeRides = new Map(); // Cache de viagens ativas
    // A08: fator de CO₂ documentado como HIPÓTESE de substituição modal (carro),
    // não como medição — 0,145 kg CO₂/km é a média de emissão de um carro leve
    // urbano; trocar o cenário aqui requer revalidar os indicadores públicos
    this.CO2_FACTOR = 0.145; // kg CO₂ por km (hipótese: substituição por carro)
  }

  // N04: v2 declara MILLIS (session_start_ms, ts_ms); o legado usava SEGUNDOS
  // (`start`). Um ÚNICO ponto de conversão por origem — a heurística
  // encadeada (toMs aplicado duas vezes) multiplicava 10.000 ms por 1000²
  fromV2Ms(tsMs) {
    const n = Number(tsMs);
    return Number.isFinite(n) && n > 0 ? n : 0;
  }

  fromLegacySec(sec) {
    const n = Number(sec);
    if (!Number.isFinite(n) || n <= 0) return 0;
    return n < 1e11 ? n * 1000 : n;
  }

  // MES-01/MES-03: bucket mensal YYYY-MM derivado de uma âncora ABSOLUTA do
  // servidor. O período é fixado na PRIMEIRA persistência da viagem e
  // reutilizado no reprocessamento — o relógio da tentativa nunca desloca
  // a viagem para outro mês.
  monthKeyFor(absoluteMs) {
    const when = absoluteMs && absoluteMs > 0 ? new Date(absoluteMs) : new Date();
    return `${when.getUTCFullYear()}-${String(when.getUTCMonth() + 1).padStart(2, '0')}`;
  }

  // Item 2 (ID-03/ID-04): identificador canônico DETERMINÍSTICO da origem —
  // bike + sessão + versão da regra. Distinge bikes diferentes e sessões
  // diferentes da mesma bike (o ride_10000 antigo colidia nos dois casos);
  // permanece IGUAL no reenvio, reinício do consumidor e reprocessamento.
  // A estação NÃO entra na identidade (ID-06: é proveniência, não chave).
  canonicalRideId(bikeId, origin) {
    return `ride_${RIDE_RULE}_${bikeId}_${origin.sessionId}-${origin.sessionSalt}`;
  }

  // Iniciar nova viagem.
  // Item 2 (ID-02/ID-07): a origem estável (session_id + session_salt) deve
  // acompanhar o processamento — horário relativo NÃO é identidade. Sem
  // origem suficiente: pendência EXPLÍCITA e null (nunca adota uptime).
  startRide(bikeId, origin) {
    const sessionId = Number(origin && origin.sessionId);
    const sessionSalt = Number(origin && origin.sessionSalt);
    if (!Number.isFinite(sessionId) || sessionId <= 0 ||
        !Number.isFinite(sessionSalt) || sessionSalt <= 0) {
      console.error(`🚨 startRide sem origem suficiente (${bikeId}): ` +
        `session_id=${origin && origin.sessionId} ` +
        `session_salt=${origin && origin.sessionSalt} — viagem NÃO iniciada ` +
        `(pendência: origem ausente/inválida; uptime não substitui identidade)`);
      return null;
    }

    const startMs = this.fromV2Ms(origin.startMs);
    const rideId = this.canonicalRideId(bikeId, {sessionId, sessionSalt});
    const ride = {
      bikeId,
      rideId,
      rideRule: RIDE_RULE,
      origin: {session_id: sessionId, session_salt: sessionSalt},
      startTime: startMs,
      endTime: null,
      points: [],
      totalDistance: 0,
      co2Saved: 0,
      status: 'active'
    };

    this.activeRides.set(bikeId, ride);
    console.log(`🚀 Nova viagem iniciada: ${bikeId} - ${rideId}`);
    return rideId;
  }

  // Adicionar ponto à viagem ativa (timestampMs já está em ms — N04)
  async addPointToRide(bikeId, location, timestampMs) {
    const ride = this.activeRides.get(bikeId);
    if (!ride) return;

    const point = {
      lat: location.latitude,
      lng: location.longitude,
      accuracy: location.accuracy,
      timestamp: this.fromV2Ms(timestampMs)
    };

    ride.points.push(point);

    // Calcular distância se há ponto anterior
    if (ride.points.length > 1) {
      const prevPoint = ride.points[ride.points.length - 2];
      const distance = geolocationService.calculateDistance(
        prevPoint.lat, prevPoint.lng,
        point.lat, point.lng
      );

      // Filtrar pontos muito próximos (< 10m) ou muito distantes (> 5km)
      if (distance > 0.01 && distance < 5) {
        ride.totalDistance += distance;
      }
    }
  }

  // Finalizar viagem.
  // Item 2 (ID-05/ID-06): a MESMA identidade canônica percorre histórico,
  // tokens por bike, tokens públicos e buckets mensais; a estação entra como
  // PROVENIÊNCIA da entrega (delivery_bases), nunca como identidade.
  // Item 4 (MES-01..MES-05): o período e os valores da PRIMEIRA persistência
  // são autoritativos — o reprocessamento reutiliza o que foi persistido.
  async finishRide(bikeId, endTimestampMs, delivery = null) {
    const ride = this.activeRides.get(bikeId);
    if (!ride) return null;

    ride.endTime = this.fromV2Ms(endTimestampMs);
    ride.status = 'completed';

    // Calcular CO2 economizado (A08: estimativa com hipótese declarada)
    ride.co2Saved = Math.round(ride.totalDistance * this.CO2_FACTOR * 1000); // em gramas

    // Filtrar viagens muito curtas (< 80m)
    if (ride.totalDistance < 0.08) {
      console.log(`❌ Viagem muito curta descartada: ${ride.rideId} (${ride.totalDistance}km)`);
      this.activeRides.delete(bikeId);
      return null;
    }

    // N04: duração em domínio ÚNICO — os timestamps dos pontos são todos ms
    // do boot da bike; endTime (relógio do servidor) menos startTime (boot)
    // misturava relógios e produzia duração sem sentido
    const firstPtMs = ride.points.length > 0
      ? ride.points[0].timestamp : ride.startTime;
    const lastPtMs = ride.points.length > 0
      ? ride.points[ride.points.length - 1].timestamp : ride.startTime;
    const spanMs = Math.max(0, lastPtMs - firstPtMs);
    const durationMin = Math.max(0, Math.round(spanMs / 60000));

    // MES-01: período atribuído NA PRIMEIRA persistência — data utilizada
    // (âncora), sua origem e a regra/fuso ficam registrados na viagem.
    // Patch de horários (§7): a CHEGADA REAL (hora civil do evento) é a
    // âncora quando existe — a hora de upload/replay NUNCA a substitui;
    // sem evento válido, âncora de registro com base declarada.
    const arrivalCivilMs = delivery && delivery.arrivalCivilMs
      ? Number(delivery.arrivalCivilMs) : 0;
    const finishedAt = arrivalCivilMs > 0 ? arrivalCivilMs : Date.now();
    const periodBasis = arrivalCivilMs > 0
      ? "arrival_event_clock" : "server_ingest_clock";
    const periodMonth = this.monthKeyFor(finishedAt);

    const rideData = {
      rideId: ride.rideId,
      ride_rule: ride.rideRule, // ID-03: versão da regra de derivação
      origin: {bike_id: bikeId, ...ride.origin}, // ID-02: origem estável
      start_ts: ride.startTime,          // ms desde o boot da bike (domínio declarado)
      end_ts: ride.startTime + spanMs,   // MESMO domínio do start (fim real da coleta)
      finished_at: finishedAt,           // âncora absoluta (chegada real quando há)
      finished_basis: periodBasis,       // patch de horários: origem da âncora
      ts_domain: 'bike_boot_ms',         // N04: domínio explícito, sem heurística
      period_month: periodMonth,         // MES-01..03: período autoritativo
      period_basis: periodBasis,
      period_rule: 'registration_month_v1',
      period_ref_ms: finishedAt,
      km: Math.round(ride.totalDistance * 1000) / 1000, // 3 casas decimais
      co2_saved_g: ride.co2Saved,
      co2_method: 'estimate_car_substitution', // A08: método publicado
      route: ride.points.map(p => ({ lat: p.lat, lng: p.lng })),
      points_count: ride.points.length,
      duration_min: durationMin
    };
    // ID-06/C245-03: proveniência da entrega (não entra na identidade)
    if (delivery && delivery.stationId) {
      rideData.delivery_bases = { [delivery.stationId]: true };
    }

    try {
      // N11/item 2 (ID-04/C245-04): criação idempotente por TRANSAÇÃO sobre
      // a identidade CANÔNICA — duas tentativas simultâneas da mesma origem:
      // UMA cria (o ID é determinístico, não aleatório), a outra ABORTA
      const rideRef = firebaseService.db.ref(`rides/${bikeId}/${ride.rideId}`);
      const result = await rideRef.transaction((current) => {
        if (current !== null) return undefined; // já existe → aborta
        return rideData;
      });

      if (!result.committed) {
        console.log(`♻️ Viagem ${ride.rideId} já registrada - replay/concorrência ignorado`);
        this.activeRides.delete(bikeId);
        const existingSnap = await rideRef.once('value');
        const existing = existingSnap.val();
        if (!existing) return null;

        // MES-02/MES-03: reprocessamento comum REUTILIZA período, distância
        // e método da viagem PERSISTIDA — a tentativa atual não é autoridade
        // para reconstruir esses valores (reprocessar em fevereiro não move
        // a contribuição de janeiro)
        const canonical = this.canonicalValues(existing, rideData, ride);

        // C245-08: divergência de valores na tentativa de reparo é
        // diagnósticada explicitamente — a versão canônica prevalece
        // (mudança deliberada de conteúdo/período é revisão auditável,
        // MES-06, fora do reprocessamento comum)
        if (canonical.conflict) {
          console.warn(`⚠️ Conflito de valores no reparo de ${ride.rideId}: ` +
            `persistido km=${canonical.km}/co2=${canonical.co2Saved}/mês=${canonical.month} ` +
            `vs tentativa km=${rideData.km}/co2=${rideData.co2_saved_g} — ` +
            `usando a versão PERSISTIDA (revisão deliberada exige MES-06)`);
        }

        // ID-06/C245-03: proveniência de entrega por OUTRA estação é
        // registrada em path próprio — a viagem continua sendo A MESMA
        if (delivery && delivery.stationId) {
          await rideRef.child(`delivery_bases/${delivery.stationId}`).set(true);
        }

        // N11/MES-04: reparo conciliável — tokens por viagem garantem que
        // apenas os agregados INCOMPLETOS recebem a contribuição, no
        // período ORIGINAL da viagem
        await this.updateBikeMetrics(bikeId, ride.rideId, canonical.km);
        await this.updatePublicStats(ride.rideId, canonical.km,
          canonical.co2Saved, canonical.month);
        return existing;
      }

      // Métricas e estatísticas — idempotentes por rideId canônico (N11):
      // crash no meio é reparado pelo replay de cima; concorrência não duplica
      await this.updateBikeMetrics(bikeId, ride.rideId, ride.totalDistance);
      await this.updatePublicStats(ride.rideId, ride.totalDistance,
        ride.co2Saved, periodMonth);

      console.log(`✅ Viagem finalizada: ${ride.rideId} - ${ride.totalDistance}km`);

      this.activeRides.delete(bikeId);
      return rideData;

    } catch (error) {
      console.error('Erro ao salvar viagem:', error);
      return null;
    }
  }

  // MES-02/MES-03/C245-08: extrai os valores AUTORITATIVOS da viagem já
  // persistida (período, distância, método); registra conflito quando a
  // tentativa atual diverge. Fallbacks documentados para registros legado
  // sem período (a data utilizada continua sendo a PERSISTIDA, nunca a da
  // tentativa — MES-03).
  canonicalValues(existing, attempt, ride) {
    const km = Number(existing.km);
    const co2Saved = Number(existing.co2_saved_g);
    let month = existing.period_month;
    let basis = existing.period_basis || 'legacy_finished_at';

    if (!month) {
      // Legado sem período registrado: âncora absoluta PERSISTIDA na viagem
      const refMs = Number(existing.finished_at) ||
        Number(existing.period_ref_ms) || 0;
      month = this.monthKeyFor(refMs > 0 ? refMs : attempt.period_ref_ms);
      basis = 'legacy_finished_at';
      if (refMs <= 0) {
        console.warn(`⚠️ Viagem legado sem âncora absoluta — período ` +
          `atribuído pelo registro (${month}); mês de REGISTRO, não comprovado`);
      }
    }

    const conflict =
      (Number.isFinite(km) && km !== attempt.km) ||
      (Number.isFinite(co2Saved) && co2Saved !== attempt.co2_saved_g);

    return {
      km: Number.isFinite(km) ? km : attempt.km,
      co2Saved: Number.isFinite(co2Saved) ? co2Saved : attempt.co2_saved_g,
      month,
      basis,
      conflict
    };
  }

  // Atualizar métricas da bicicleta (transação atômica — A08)
  // N11: a contribuição de cada viagem entra UMA única vez — o token
  // contributed_rides/{rideId} viaja DENTRO da mesma transação do contador;
  // reexecução/concorrência/reparo abortam quando o token já existe.
  // Item 2 (ID-05): o token usa o rideId CANÔNICO (bike + sessão) — tokens
  // globais não colidem mais entre bikes com o mesmo início relativo.
  async updateBikeMetrics(bikeId, rideId, distance) {
    try {
      const bikeRef = firebaseService.db.ref(`bikes/${bikeId}/metrics`);
      // N11: campos coercidos — nó existente SEM um dos contadores gerava
      // NaN que se propagava para sempre (NaN + x = NaN)
      const result = await bikeRef.transaction((current) => {
        const cur = (current && typeof current === 'object') ? current : {};
        if (cur.contributed_rides && cur.contributed_rides[rideId]) {
          return undefined; // esta viagem já contribuiu — aborta
        }
        const kmTotal = Number(cur.km_total) || 0;
        const ridesTotal = Number(cur.rides_total) || 0;
        const contributed = (cur.contributed_rides && typeof cur.contributed_rides === 'object')
          ? { ...cur.contributed_rides } : {};
        contributed[rideId] = true;
        return {
          km_total: Math.round((kmTotal + distance) * 1000) / 1000,
          rides_total: ridesTotal + 1,
          contributed_rides: contributed,
          last_ride: Date.now()
        };
      });
      if (result.committed) return;
      // abortada por token (já contribuiu) ou por conflito — nada a fazer
    } catch (error) {
      console.error('Erro ao atualizar métricas da bike:', error);
    }
  }

  // Atualizar estatísticas públicas.
  // A08: agregados derivados de viagens únicas.
  // Item 4 (MES-04/MES-05): o bucket mensal canônico é public_stats/monthly/
  // {YYYY-MM} (um bucket por mês, token por viagem); os totais GERAIS são
  // campos próprios (total_rides/km_total/co2_saved_g) — o par legado
  // total_*_month/current_month passa a ser ESPELHO do bucket do período da
  // viagem (compat de leitura), não um acumulador que mistura meses.
  // Item 2 (ID-05): token global com rideId canônico (colisão entre bikes
  // impossibilitava a SEGUNDA bike de contribuir publicamente).
  async updatePublicStats(rideId, distance, co2Saved, monthKey) {
    try {
      const month = monthKey && /^\d{4}-\d{2}$/.test(monthKey)
        ? monthKey : this.monthKeyFor(Date.now());

      // Período mensal (transação) — bucket correspondente (MES-05)
      const monthRef = firebaseService.db.ref(`public_stats/monthly/${month}`);
      await monthRef.transaction((current) => {
        const cur = (current && typeof current === 'object') ? current : {};
        if (cur.contributed_rides && cur.contributed_rides[rideId]) {
          return undefined; // já contribuiu neste mês — aborta
        }
        const rides = Number(cur.total_rides) || 0;
        const km = Number(cur.km) || 0;
        const co2 = Number(cur.co2_saved_g) || 0;
        const contributed = (cur.contributed_rides && typeof cur.contributed_rides === 'object')
          ? { ...cur.contributed_rides } : {};
        contributed[rideId] = true;
        return {
          total_rides: rides + 1,
          km: Math.round((km + distance) * 1000) / 1000,
          co2_saved_g: co2 + co2Saved,
          contributed_rides: contributed,
          last_update: Date.now()
        };
      });

      // Totais gerais (transação) — campos próprios de TOTAL (MES-05)
      const statsRef = firebaseService.db.ref('public_stats');
      await statsRef.transaction((current) => {
        const cur = (current && typeof current === 'object') ? current : {};
        if (cur.contributed_rides && cur.contributed_rides[rideId]) {
          return undefined; // já contribuiu no total geral — aborta
        }
        const rides = Number(cur.total_rides) || 0;
        const km = Number(cur.km_total) || 0;
        const co2 = Number(cur.co2_saved_g) || 0;
        const contributed = (cur.contributed_rides && typeof cur.contributed_rides === 'object')
          ? { ...cur.contributed_rides } : {};
        contributed[rideId] = true;
        return {
          ...cur,
          total_rides: rides + 1,
          km_total: Math.round((km + distance) * 1000) / 1000,
          co2_saved_g: co2 + co2Saved,
          contributed_rides: contributed,
          current_month: month,
          last_update: Date.now()
        };
      });

      // MES-05 (compat de leitura): os campos legado total_*_month passam a
      // ESPELHAR o bucket do período desta viagem (lido após o commit do
      // mensal — best-effort; o canônico mensal é monthly/{mês}). Nunca mais
      // acumulam meses diferentes num campo "mensal".
      const bucketSnap = await monthRef.once('value');
      const bucket = bucketSnap.val();
      if (bucket && typeof bucket === 'object') {
        await statsRef.update({
          total_rides_month: Number(bucket.total_rides) || 0,
          km_month: Number(bucket.km) || 0,
          co2_saved_month_g: Number(bucket.co2_saved_g) || 0,
          current_month: month
        });
      }
    } catch (error) {
      console.error('Erro ao atualizar estatísticas públicas:', error);
    }
  }

  // Verificar se bike tem viagem ativa
  hasActiveRide(bikeId) {
    return this.activeRides.has(bikeId);
  }

  // Obter viagem ativa
  getActiveRide(bikeId) {
    return this.activeRides.get(bikeId);
  }

  // Cancelar viagem (se bike fica muito tempo parada)
  cancelRide(bikeId, reason = 'timeout') {
    const ride = this.activeRides.get(bikeId);
    if (ride) {
      console.log(`❌ Viagem cancelada: ${ride.rideId} - ${reason}`);
      this.activeRides.delete(bikeId);
    }
  }
}

module.exports = new RideCalculator();
