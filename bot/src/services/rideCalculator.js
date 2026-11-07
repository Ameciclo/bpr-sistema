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

  // A08: timestamps chegam em segundos (legado) ou millis (v2) — um único
  // auxiliar normaliza para millis antes de qualquer aritmética
  toMs(ts) {
    const n = Number(ts);
    if (!Number.isFinite(n) || n <= 0) return 0;
    return n < 1e11 ? n * 1000 : n;
  }

  // Iniciar nova viagem
  startRide(bikeId, timestampMs) {
    const startMs = this.toMs(timestampMs);
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

  // Adicionar ponto à viagem ativa
  async addPointToRide(bikeId, location, timestampMs) {
    const ride = this.activeRides.get(bikeId);
    if (!ride) return;

    const point = {
      lat: location.latitude,
      lng: location.longitude,
      accuracy: location.accuracy,
      timestamp: this.toMs(timestampMs)
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

    ride.endTime = this.toMs(endTimestampMs);
    ride.status = 'completed';

    // Calcular CO2 economizado (A08: estimativa com hipótese declarada)
    ride.co2Saved = Math.round(ride.totalDistance * this.CO2_FACTOR * 1000); // em gramas

    // Filtrar viagens muito curtas (< 80m)
    if (ride.totalDistance < 0.08) {
      console.log(`❌ Viagem muito curta descartada: ${ride.rideId} (${ride.totalDistance}km)`);
      this.activeRides.delete(bikeId);
      return null;
    }

    // A08: duração em unidades coerentes (ms → min)
    const durationMin = Math.max(0, Math.round((ride.endTime - ride.startTime) / 60000));

    const rideData = {
      start_ts: ride.startTime,   // ms, explícito
      end_ts: ride.endTime,       // ms, explícito
      km: Math.round(ride.totalDistance * 1000) / 1000, // 3 casas decimais
      co2_saved_g: ride.co2Saved,
      co2_method: 'estimate_car_substitution', // A08: método publicado
      route: ride.points.map(p => ({ lat: p.lat, lng: p.lng })),
      points_count: ride.points.length,
      duration_min: durationMin
    };

    try {
      // A05: idempotente por identidade — reprocessar a mesma sessão não
      // duplica viagem nem métricas
      const rideRef = firebaseService.db.ref(`rides/${bikeId}/${ride.rideId}`);
      const existing = await rideRef.once('value');
      if (existing.exists()) {
        console.log(`♻️ Viagem ${ride.rideId} já registrada - replay ignorado`);
        this.activeRides.delete(bikeId);
        return existing.val();
      }

      // Salvar viagem
      await rideRef.set(rideData);

      // Atualizar métricas da bike (transação — A08)
      await this.updateBikeMetrics(bikeId, ride.totalDistance);

      // Atualizar estatísticas públicas (transação + período mensal — A08)
      await this.updatePublicStats(ride.totalDistance, ride.co2Saved, ride.endTime);

      console.log(`✅ Viagem finalizada: ${ride.rideId} - ${ride.totalDistance}km`);

      this.activeRides.delete(bikeId);
      return rideData;

    } catch (error) {
      console.error('Erro ao salvar viagem:', error);
      return null;
    }
  }

  // Atualizar métricas da bicicleta (transação atômica — A08)
  async updateBikeMetrics(bikeId, distance) {
    try {
      const bikeRef = firebaseService.db.ref(`bikes/${bikeId}/metrics`);
      const result = await bikeRef.transaction((current) => {
        const cur = current || { km_total: 0, rides_total: 0 };
        return {
          km_total: Math.round((cur.km_total + distance) * 1000) / 1000,
          rides_total: cur.rides_total + 1,
          last_ride: Date.now()
        };
      });
      if (result.committed) return;
      // transação abortada (conflito) — próxima viagem reconcilia o total
      console.warn('⚠️ Métricas da bike não confirmadas (transação abortada)');
    } catch (error) {
      console.error('Erro ao atualizar métricas da bike:', error);
    }
  }

  // Atualizar estatísticas públicas.
  // A08: agregados por período (mês) e derivados de viagens únicas; totais
  // gerais mantidos por compatibilidade com consumidores existentes
  async updatePublicStats(distance, co2Saved, eventMs) {
    try {
      const when = eventMs && eventMs > 0 ? new Date(eventMs) : new Date();
      const monthKey = `${when.getUTCFullYear()}-${String(when.getUTCMonth() + 1).padStart(2, '0')}`;

      // Período mensal (transação)
      const monthRef = firebaseService.db.ref(`public_stats/monthly/${monthKey}`);
      await monthRef.transaction((current) => {
        const cur = current || { total_rides: 0, km: 0, co2_saved_g: 0 };
        return {
          total_rides: cur.total_rides + 1,
          km: Math.round((cur.km + distance) * 1000) / 1000,
          co2_saved_g: cur.co2_saved_g + co2Saved,
          last_update: Date.now()
        };
      });

      // Totais gerais (transação, compatibilidade)
      const statsRef = firebaseService.db.ref('public_stats');
      await statsRef.transaction((current) => {
        const cur = current || {
          total_rides_month: 0,
          km_month: 0,
          co2_saved_month_g: 0,
          last_update: Date.now()
        };
        return {
          ...cur,
          total_rides_month: cur.total_rides_month + 1,
          km_month: Math.round((cur.km_month + distance) * 1000) / 1000,
          co2_saved_month_g: cur.co2_saved_month_g + co2Saved,
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
