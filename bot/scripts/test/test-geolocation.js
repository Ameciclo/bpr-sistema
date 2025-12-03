#!/usr/bin/env node
require('dotenv').config();
const fs = require('fs');
const axios = require('axios');

const GOOGLE_API_KEY = process.env.GOOGLE_GEOLOCATION_API_KEY;

async function testGeolocation(networks) {
  try {
    const wifiAccessPoints = networks.map(net => ({
      macAddress: net.bssid,
      signalStrength: net.rssi,
      channel: net.channel
    }));

    console.log('📡 Enviando para Google:', JSON.stringify(wifiAccessPoints, null, 2));

    const response = await axios.post(
      `https://www.googleapis.com/geolocation/v1/geolocate?key=${GOOGLE_API_KEY}`,
      { wifiAccessPoints }
    );

    if (response.status === 200) {
      return {
        lat: response.data.location.lat,
        lng: response.data.location.lng,
        accuracy: response.data.accuracy
      };
    }
  } catch (error) {
    console.error('❌ Erro:', error.response?.data || error.message);
  }
  return null;
}

async function main() {
  if (!GOOGLE_API_KEY) {
    console.error('❌ GOOGLE_GEOLOCATION_API_KEY não encontrada no .env');
    return;
  }

  const data = JSON.parse(fs.readFileSync('dados-bike.json', 'utf8'));
  const firstSession = Object.values(data.sessions)[0];
  const firstScan = firstSession.scans[0];
  
  console.log('🧪 Testando com primeiro scan...');
  const coordinates = await testGeolocation(firstScan.networks);
  
  if (coordinates) {
    console.log('✅ Coordenadas obtidas:', coordinates);
  } else {
    console.log('❌ Não foi possível obter coordenadas');
  }
}

main();