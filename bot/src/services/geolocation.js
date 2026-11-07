const axios = require('axios');

// A02: DTO interno de scan/rede — adaptadores de entrada aceitam os formatos
// conhecidos (array do firmware [ssid,bssid,rssi,channel] e objeto v2) e
// normalizam para UM formato usado por todos os consumidores.

function normalizeNetwork(net) {
  if (Array.isArray(net)) {
    return {
      ssid: String(net[0] ?? ''),
      bssid: String(net[1] ?? ''),
      rssi: Number(net[2] ?? 0),
      channel: Number(net[3] ?? 0)
    };
  }
  if (net && typeof net === 'object') {
    return {
      ssid: String(net.ssid ?? ''),
      bssid: String(net.bssid ?? ''),
      rssi: Number(net.rssi ?? 0),
      channel: Number(net.channel ?? 0)
    };
  }
  return { ssid: '', bssid: '', rssi: 0, channel: 0 };
}

function normalizeNetworks(networks) {
  if (!Array.isArray(networks)) return [];
  return networks.map(normalizeNetwork);
}

// A02: scan em array [timestamp, redes] (legado) ou objeto {timestamp, networks}
function normalizeScan(scan) {
  if (Array.isArray(scan)) {
    return {
      timestamp: Number(scan[0] ?? 0),
      networks: normalizeNetworks(scan[2] || scan[1] || [])
    };
  }
  if (scan && typeof scan === 'object') {
    return {
      timestamp: Number(scan.timestamp ?? scan.ts_ms ?? 0),
      networks: normalizeNetworks(scan.networks || scan.nets || [])
    };
  }
  return { timestamp: 0, networks: [] };
}

class GeolocationService {
  constructor() {
    this.apiKey = process.env.GOOGLE_GEOLOCATION_API_KEY;
    this.baseUrl = 'https://www.googleapis.com/geolocation/v1/geolocate';
  }

  // Converter dados WiFi para formato da API do Google (aceita os dois formatos)
  formatWifiData(networks) {
    const normalized = normalizeNetworks(networks);
    return normalized
      .filter(n => n.bssid) // sem BSSID a API rejeita o ponto
      .map(n => ({
        macAddress: n.bssid,
        signalStrength: n.rssi,
        channel: n.channel
      }));
  }

  // Obter localização baseada em redes WiFi
  async getLocationFromWifi(networks) {
    if (!this.apiKey) {
      throw new Error('Google Geolocation API key não configurada');
    }

    const wifiAccessPoints = this.formatWifiData(networks);
    if (wifiAccessPoints.length === 0) {
      return null;
    }

    try {
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

  // Calcular rota total baseada em múltiplos scans (aceita os dois formatos)
  async calculateRoute(scans) {
    const locations = [];
    let totalDistance = 0;

    for (const rawScan of scans) {
      const scan = normalizeScan(rawScan);
      if (scan.networks.length === 0) continue;

      const location = await this.getLocationFromWifi(scan.networks);
      if (location) {
        locations.push({
          timestamp: scan.timestamp,
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
module.exports.normalizeNetwork = normalizeNetwork;
module.exports.normalizeNetworks = normalizeNetworks;
module.exports.normalizeScan = normalizeScan;
