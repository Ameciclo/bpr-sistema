const axios = require('axios');

class GeolocationService {
  constructor() {
    this.apiKey = process.env.GOOGLE_GEOLOCATION_API_KEY;
    this.baseUrl = 'https://www.googleapis.com/geolocation/v1/geolocate';
  }

  // Converter dados WiFi para formato da API do Google
  formatWifiData(networks) {
    if (!networks || !Array.isArray(networks)) return [];
    // Nova estrutura: [ssid, bssid, rssi, channel]
    return networks.map(network => ({
      macAddress: network[1], // BSSID
      signalStrength: network[2], // RSSI
      channel: network[3] // Channel
    }));
  }

  // Obter localização baseada em redes WiFi
  async getLocationFromWifi(networks) {
    if (!this.apiKey) {
      throw new Error('Google Geolocation API key não configurada');
    }

    if (!networks || !Array.isArray(networks) || networks.length === 0) {
      return null;
    }

    try {
      const wifiAccessPoints = this.formatWifiData(networks);
      if (wifiAccessPoints.length === 0) return null;
      
      const response = await axios.post(`${this.baseUrl}?key=${this.apiKey}`, {
        wifiAccessPoints,
        considerIp: false
      });

      return {
        latitude: response.data.location.lat,
        longitude: response.data.location.lng,
        accuracy: response.data.accuracy
      };
    } catch (error) {
      console.error('Erro na geolocalização:', error.response?.data || error.message);
      return null;
    }
  }

  // Calcular distância entre dois pontos (fórmula de Haversine)
  calculateDistance(lat1, lon1, lat2, lon2) {
    const R = 6371; // Raio da Terra em km
    const dLat = this.toRad(lat2 - lat1);
    const dLon = this.toRad(lon2 - lon1);
    
    const a = Math.sin(dLat/2) * Math.sin(dLat/2) +
              Math.cos(this.toRad(lat1)) * Math.cos(this.toRad(lat2)) *
              Math.sin(dLon/2) * Math.sin(dLon/2);
    
    const c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1-a));
    return R * c; // Distância em km
  }

  toRad(degrees) {
    return degrees * (Math.PI / 180);
  }

  // Calcular rota total baseada em múltiplos scans
  async calculateRoute(scans) {
    const locations = [];
    let totalDistance = 0;

    for (const scan of scans) {
      // Nova estrutura: [timestamp, ?, [[ssid, bssid, rssi, channel]]]
      const networks = scan[2];
      if (!networks || networks.length === 0) continue;
      
      const location = await this.getLocationFromWifi(networks);
      if (location) {
        locations.push({
          timestamp: scan[0],
          ...location
        });
      }
    }

    // Calcular distância total
    for (let i = 1; i < locations.length; i++) {
      const distance = this.calculateDistance(
        locations[i-1].latitude,
        locations[i-1].longitude,
        locations[i].latitude,
        locations[i].longitude
      );
      totalDistance += distance;
    }

    return {
      locations,
      totalDistance: Math.round(totalDistance * 1000) / 1000, // Arredondar para 3 casas decimais
      points: locations.length
    };
  }
}

module.exports = new GeolocationService();