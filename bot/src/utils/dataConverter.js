// Utilitário para converter entre estruturas de dados antiga e nova

class DataConverter {
  // Converter scan da estrutura antiga para nova
  static convertOldScanToNew(oldScan) {
    const { bike, timestamp, networks } = oldScan;
    
    // Converter redes para formato compacto [ssid, bssid, rssi, channel]
    const compactNetworks = networks.map(network => [
      network.ssid,
      network.bssid || `${network.ssid}_${network.channel}`, // Gerar BSSID se não existir
      network.rssi,
      network.channel
    ]);

    return [timestamp, compactNetworks];
  }

  // Converter estrutura completa antiga para nova
  static convertOldStructureToNew(oldData) {
    const { scans, status } = oldData;
    const newStructure = { bikes: {} };

    if (status) {
      const bikeId = status.bike;
      
      // Criar estrutura da bike
      newStructure.bikes[bikeId] = {
        sessions: {},
        networks: {}
      };

      // Gerar sessão única com todos os scans
      const sessionId = this.generateSessionId(new Date());
      const sessionScans = [];
      const knownNetworks = {};

      // Processar scans
      if (scans) {
        Object.entries(scans).forEach(([scanId, scanData]) => {
          const convertedScan = this.convertOldScanToNew(scanData);
          sessionScans.push(convertedScan);

          // Catalogar redes
          scanData.networks.forEach(network => {
            const bssid = network.bssid || `${network.ssid}_${network.channel}`;
            if (!knownNetworks[bssid]) {
              knownNetworks[bssid] = {
                ssid: network.ssid,
                first: scanData.timestamp
              };
            }
          });
        });
      }

      // Converter bateria para formato compacto [timestamp, level]
      const batteryData = status.battery ? 
        status.battery.map(b => [b.time, b.level]) : [];

      // Converter conexões para formato compacto [timestamp, event, base, ip]
      const connectionsData = status.connections ? 
        status.connections.map(c => [c.time, c.event, c.base, c.ip]) : [];

      // Criar sessão
      const firstScan = sessionScans[0];
      const lastScan = sessionScans[sessionScans.length - 1];

      newStructure.bikes[bikeId].sessions[sessionId] = {
        start: firstScan ? firstScan[0] : Date.now(),
        end: lastScan ? lastScan[0] : null,
        mode: 'normal',
        scans: sessionScans,
        battery: batteryData,
        connections: connectionsData
      };

      // Adicionar redes conhecidas
      newStructure.bikes[bikeId].networks = knownNetworks;
    }

    return newStructure;
  }

  // Gerar ID de sessão baseado na data
  static generateSessionId(date = new Date()) {
    const year = date.getFullYear();
    const month = String(date.getMonth() + 1).padStart(2, '0');
    const day = String(date.getDate()).padStart(2, '0');
    const hour = String(date.getHours()).padStart(2, '0');
    const minute = String(date.getMinutes()).padStart(2, '0');
    
    return `${year}${month}${day}_${hour}${minute}`;
  }

  // Converter scan da nova estrutura para formato legível
  static convertNewScanToReadable(newScan) {
    const [timestamp, networks] = newScan;
    
    return {
      timestamp,
      networks: networks.map(([ssid, bssid, rssi, channel]) => ({
        ssid,
        bssid,
        rssi,
        channel
      }))
    };
  }

  // Validar estrutura de dados
  static validateNewStructure(data) {
    if (!data.bikes) return false;
    
    for (const [bikeId, bikeData] of Object.entries(data.bikes)) {
      if (!bikeData.sessions || !bikeData.networks) return false;
      
      for (const [sessionId, sessionData] of Object.entries(bikeData.sessions)) {
        if (!sessionData.start || !Array.isArray(sessionData.scans)) return false;
        
        // Validar formato dos scans
        for (const scan of sessionData.scans) {
          if (!Array.isArray(scan) || scan.length !== 2) return false;
          const [timestamp, networks] = scan;
          if (typeof timestamp !== 'number' || !Array.isArray(networks)) return false;
        }
      }
    }
    
    return true;
  }
}

module.exports = DataConverter;