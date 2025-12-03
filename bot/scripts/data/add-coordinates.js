#!/usr/bin/env node
require('dotenv').config();
const fs = require('fs');
const axios = require('axios');

const GOOGLE_API_KEY = process.env.GOOGLE_GEOLOCATION_API_KEY;

async function getCoordinates(networks) {
  try {
    const wifiAccessPoints = networks.map(net => ({
      macAddress: net.bssid,
      signalStrength: net.rssi,
      channel: net.channel
    }));

    const response = await axios.post(
      `https://www.googleapis.com/geolocation/v1/geolocate?key=${GOOGLE_API_KEY}`,
      { wifiAccessPoints }
    );

    return response.data.location ? {
      lat: response.data.location.lat,
      lng: response.data.location.lng,
      accuracy: response.data.accuracy
    } : null;
  } catch (error) {
    console.log('❌ Erro API:', error.response?.data?.error?.message || error.message);
    return null;
  }
}

async function main() {
  const data = JSON.parse(fs.readFileSync('dados-bike.json', 'utf8'));
  let processed = 0;
  let success = 0;

  for (const [sessionId, session] of Object.entries(data.sessions)) {
    console.log(`📂 Processando sessão ${sessionId}...`);
    
    for (let i = 0; i < session.scans.length; i++) {
      const scan = session.scans[i];
      
      if (scan.coordinates) {
        console.log(`  ✅ Scan ${i} já tem coordenadas`);
        continue;
      }

      console.log(`  🔄 Processando scan ${i}...`);
      const coordinates = await getCoordinates(scan.networks);
      
      if (coordinates) {
        scan.coordinates = coordinates;
        success++;
        console.log(`  ✅ Coordenadas adicionadas: ${coordinates.lat}, ${coordinates.lng}`);
      } else {
        console.log(`  ❌ Não foi possível obter coordenadas`);
      }
      
      processed++;
      await new Promise(resolve => setTimeout(resolve, 1000)); // Rate limit
    }
  }

  fs.writeFileSync('dados-bike-with-coords.json', JSON.stringify(data, null, 2));
  console.log(`\n🎉 Processamento concluído:`);
  console.log(`📊 Total processado: ${processed}`);
  console.log(`✅ Sucessos: ${success}`);
  console.log(`📁 Arquivo salvo: dados-bike-with-coords.json`);
}

main();