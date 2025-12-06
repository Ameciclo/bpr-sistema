const geolocationService = require('./geolocation');
const firebaseService = require('../config/firebase');

class RideCalculator {
  constructor() {
    this.activeRides = new Map(); // Cache de viagens ativas
    this.CO2_FACTOR = 0.145; // kg CO2 por km economizado
  }

  // Iniciar nova viagem
  startRide(bikeId, timestamp) {
    const rideId = `ride_${timestamp}`;
    const ride = {
      bikeId,
      rideId,
      startTime: timestamp,
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
  async addPointToRide(bikeId, location, timestamp) {
    const ride = this.activeRides.get(bikeId);
    if (!ride) return;

    const point = {
      lat: location.latitude,
      lng: location.longitude,
      accuracy: location.accuracy,
      timestamp
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
  async finishRide(bikeId, endTimestamp) {
    const ride = this.activeRides.get(bikeId);
    if (!ride) return null;

    ride.endTime = endTimestamp;
    ride.status = 'completed';
    
    // Calcular CO2 economizado
    ride.co2Saved = Math.round(ride.totalDistance * this.CO2_FACTOR * 1000); // em gramas

    // Filtrar viagens muito curtas (< 80m)
    if (ride.totalDistance < 0.08) {
      console.log(`❌ Viagem muito curta descartada: ${ride.rideId} (${ride.totalDistance}km)`);
      this.activeRides.delete(bikeId);
      return null;
    }

    // Salvar no Firebase
    const rideData = {
      start_ts: ride.startTime,
      end_ts: ride.endTime,
      km: Math.round(ride.totalDistance * 1000) / 1000, // 3 casas decimais
      co2_saved_g: ride.co2Saved,
      route: ride.points.map(p => ({ lat: p.lat, lng: p.lng })),
      points_count: ride.points.length,
      duration_min: Math.round((ride.endTime - ride.startTime) / 60)
    };

    try {
      // Salvar viagem
      await firebaseService.db.ref(`rides/${bikeId}/${ride.rideId}`).set(rideData);
      
      // Atualizar métricas da bike
      await this.updateBikeMetrics(bikeId, ride.totalDistance);
      
      // Atualizar estatísticas públicas
      await this.updatePublicStats(ride.totalDistance, ride.co2Saved);
      
      console.log(`✅ Viagem finalizada: ${ride.rideId} - ${ride.totalDistance}km`);
      
      this.activeRides.delete(bikeId);
      return rideData;
      
    } catch (error) {
      console.error('Erro ao salvar viagem:', error);
      return null;
    }
  }

  // Atualizar métricas da bicicleta
  async updateBikeMetrics(bikeId, distance) {
    try {
      const bikeRef = firebaseService.db.ref(`bikes/${bikeId}/metrics`);
      const snapshot = await bikeRef.once('value');
      const current = snapshot.val() || { km_total: 0, rides_total: 0 };
      
      await bikeRef.update({
        km_total: Math.round((current.km_total + distance) * 1000) / 1000,
        rides_total: current.rides_total + 1,
        last_ride: Date.now()
      });
    } catch (error) {
      console.error('Erro ao atualizar métricas da bike:', error);
    }
  }

  // Atualizar estatísticas públicas
  async updatePublicStats(distance, co2Saved) {
    try {
      const statsRef = firebaseService.db.ref('public_stats');
      const snapshot = await statsRef.once('value');
      const current = snapshot.val() || {
        total_rides_month: 0,
        km_month: 0,
        co2_saved_month_g: 0,
        last_update: Date.now()
      };
      
      await statsRef.update({
        total_rides_month: current.total_rides_month + 1,
        km_month: Math.round((current.km_month + distance) * 1000) / 1000,
        co2_saved_month_g: current.co2_saved_month_g + co2Saved,
        last_update: Date.now()
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