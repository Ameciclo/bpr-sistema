#!/usr/bin/env node
require('dotenv').config();
const firebase = require('./src/config/firebase');
const geolocation = require('./src/services/geolocation');
const fs = require('fs');

async function generateGeoJSON(bikeName) {
  try {
    console.log(`🚴 Buscando dados da bike: ${bikeName}`);
    
    const bikeData = await firebase.getBikeData(bikeName);
    if (!bikeData || !bikeData.sessions) {
      console.log('❌ Nenhuma sessão encontrada para esta bike');
      return;
    }

    const geojson = {
      type: "FeatureCollection",
      features: []
    };

    console.log(`📊 Processando ${Object.keys(bikeData.sessions).length} sessões...`);

    for (const [sessionId, session] of Object.entries(bikeData.sessions)) {
      if (!session.scans) continue;

      const scans = Object.values(session.scans).filter(scan => scan.coordinates);
      if (scans.length < 2) continue;
      
      const coordinates = scans.map(scan => [scan.coordinates.lng, scan.coordinates.lat]);
      
      // Calcular distância total
      let totalDistance = 0;
      for (let i = 1; i < scans.length; i++) {
        const distance = geolocation.calculateDistance(
          scans[i-1].coordinates.lat,
          scans[i-1].coordinates.lng,
          scans[i].coordinates.lat,
          scans[i].coordinates.lng
        );
        totalDistance += distance;
      }
      
      geojson.features.push({
        type: "Feature",
        properties: {
          sessionId,
          bikeName,
          startTime: new Date(session.start * 1000).toISOString(),
          endTime: session.end ? new Date(session.end * 1000).toISOString() : null,
          totalDistance: Math.round(totalDistance * 1000) / 1000,
          points: coordinates.length,
          scans: scans.length
        },
        geometry: {
          type: "LineString",
          coordinates
        }
      });

      console.log(`✅ Sessão ${sessionId}: ${Math.round(totalDistance * 1000) / 1000}km, ${coordinates.length} pontos`);
    }

    const filename = `${bikeName}-routes.geojson`;
    fs.writeFileSync(filename, JSON.stringify(geojson, null, 2));
    
    console.log(`🎉 GeoJSON gerado: ${filename}`);
    console.log(`📈 Total de ${geojson.features.length} rotas processadas`);
    
  } catch (error) {
    console.error('❌ Erro:', error.message);
  } finally {
    process.exit(0);
  }
}

// Usar argumento da linha de comando ou padrão
const bikeName = process.argv[2] || 'economico';
generateGeoJSON(bikeName);