const geolocationService = require('./geolocation');
const firebaseService = require('../config/firebase');

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

  // Iniciar nova viagem (timestampMs já está em ms — N04)
  startRide(bikeId, timestampMs) {
    const startMs = this.fromV2Ms(timestampMs);
    const rideId = `ride_${startMs}`;
    const ride = {
      bikeId,
      rideId,
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

  // Finalizar viagem
  async finishRide(bikeId, endTimestampMs) {
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

    const rideData = {
      start_ts: ride.startTime,          // ms desde o boot da bike (domínio declarado)
      end_ts: ride.startTime + spanMs,   // MESMO domínio do start (fim real da coleta)
      finished_at: Date.now(),           // âncora absoluta do servidor (ingest)
      ts_domain: 'bike_boot_ms',         // N04: domínio explícito, sem heurística
      km: Math.round(ride.totalDistance * 1000) / 1000, // 3 casas decimais
      co2_saved_g: ride.co2Saved,
      co2_method: 'estimate_car_substitution', // A08: método publicado
      route: ride.points.map(p => ({ lat: p.lat, lng: p.lng })),
      points_count: ride.points.length,
      duration_min: durationMin
    };

    try {
      // N11: criação idempotente por TRANSAÇÃO — duas finalizações
      // concorrentes da mesma viagem: UMA cria, a outra ABORTA (o par
      // exists+set observava ausência nas duas e somava o contador 2×)
      const rideRef = firebaseService.db.ref(`rides/${bikeId}/${ride.rideId}`);
      const result = await rideRef.transaction((current) => {
        if (current !== null) return undefined; // já existe → aborta
        return rideData;
      });

      if (!result.committed) {
        console.log(`♻️ Viagem ${ride.rideId} já registrada - replay/concorrência ignorado`);
        this.activeRides.delete(bikeId);
        const existing = await rideRef.once('value');
        // N11: REPARO reconciliável — um crash entre a criação da viagem e a
        // agregação deixava a contribuição perdida para sempre; as métricas
        // são idempotentes POR VIAGEM (token), então rodar aqui nunca duplica
        await this.updateBikeMetrics(bikeId, ride.rideId, ride.totalDistance);
        await this.updatePublicStats(ride.rideId, ride.totalDistance,
          ride.co2Saved, rideData.finished_at);
        return existing.val();
      }

      // Métricas e estatísticas — idempotentes por rideId (N11): crash no
      // meio é reparado pelo replay de cima; concorrência não duplica
      await this.updateBikeMetrics(bikeId, ride.rideId, ride.totalDistance);
      await this.updatePublicStats(ride.rideId, ride.totalDistance,
        ride.co2Saved, rideData.finished_at);

      console.log(`✅ Viagem finalizada: ${ride.rideId} - ${ride.totalDistance}km`);

      this.activeRides.delete(bikeId);
      return rideData;

    } catch (error) {
      console.error('Erro ao salvar viagem:', error);
      return null;
    }
  }

  // Atualizar métricas da bicicleta (transação atômica — A08)
  // N11: a contribuição de cada viagem entra UMA única vez — o token
  // contributed_rides/{rideId} viaja DENTRO da mesma transação do contador;
  // reexecução/concorrência/reparo abortam quando o token já existe
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
  // A08: agregados por período (mês) e derivados de viagens únicas; totais
  // gerais mantidos por compatibilidade com consumidores existentes
  // N04: o período usa a âncora ABSOLUTA do servidor (finished_at) — o
  // end_ts da viagem está no domínio boot-da-bike e não forma data civil
  // N11: token contributed_rides/{rideId} na MESMA transação do contador —
  // contribuição única mesmo com replay/concorrência/reparo
  async updatePublicStats(rideId, distance, co2Saved, finishedAtMs) {
    try {
      const when = finishedAtMs && finishedAtMs > 0
        ? new Date(finishedAtMs) : new Date();
      const monthKey = `${when.getUTCFullYear()}-${String(when.getUTCMonth() + 1).padStart(2, '0')}`;

      // Período mensal (transação)
      const monthRef = firebaseService.db.ref(`public_stats/monthly/${monthKey}`);
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

      // Totais gerais (transação, compatibilidade)
      // N11: no primeiro resumo o pai public_stats já existe (ramo monthly
      // criado acima) mas os contadores gerais NÃO — sem coerção, o update
      // gravava NaN como estatística pública definitiva
      const statsRef = firebaseService.db.ref('public_stats');
      await statsRef.transaction((current) => {
        const cur = (current && typeof current === 'object') ? current : {};
        if (cur.contributed_rides && cur.contributed_rides[rideId]) {
          return undefined; // já contribuiu no total geral — aborta
        }
        const rides = Number(cur.total_rides_month) || 0;
        const km = Number(cur.km_month) || 0;
        const co2 = Number(cur.co2_saved_month_g) || 0;
        const contributed = (cur.contributed_rides && typeof cur.contributed_rides === 'object')
          ? { ...cur.contributed_rides } : {};
        contributed[rideId] = true;
        return {
          ...cur,
          total_rides_month: rides + 1,
          km_month: Math.round((km + distance) * 1000) / 1000,
          co2_saved_month_g: co2 + co2Saved,
          contributed_rides: contributed,
          current_month: monthKey, // A08: período explícito do resumo
          last_update: Date.now()
        };
      });
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
