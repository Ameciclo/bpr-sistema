const firebaseService = require('../config/firebase');

class StationMonitor {
  constructor(bot) {
    this.bot = bot;
    this.stationStatus = new Map();
    this.HEARTBEAT_TIMEOUT = 30 * 60 * 1000; // 30 minutos
    this.init();
  }

  init() {
    // Verificar heartbeats a cada 30 minutos
    setInterval(() => {
      this.checkStationHeartbeats();
    }, this.HEARTBEAT_TIMEOUT);

    console.log('🏢 Monitor de estações iniciado');
  }

  // Verificar heartbeats de todas as estações
  async checkStationHeartbeats() {
    try {
      const basesSnapshot = await firebaseService.db.ref('bases').once('value');
      const bases = basesSnapshot.val() || {};
      
      for (const [baseId, baseData] of Object.entries(bases)) {
        await this.checkStationHeartbeat(baseId, baseData);
      }
    } catch (error) {
      console.error('Erro ao verificar heartbeats:', error);
    }
  }

  // Verificar heartbeat de uma estação específica
  async checkStationHeartbeat(baseId, baseData) {
    try {
      const heartbeatRef = firebaseService.db.ref(`bases/${baseId}/last_heartbeat`);
      const snapshot = await heartbeatRef.once('value');
      const heartbeat = snapshot.val();

      const now = Date.now();
      let isOnline = null; // null = sem evidência (horário inválido/ausente)

      if (heartbeat && typeof heartbeat.timestamp === 'number' && heartbeat.timestamp > 0) {
        // A03: o firmware grava Unix em SEGUNDOS — compara em milissegundos
        const tsMs = heartbeat.timestamp < 1e11
          ? heartbeat.timestamp * 1000
          : heartbeat.timestamp;

        if (heartbeat.timestamp_valid === false) {
          // A03: horário desconhecido não inventa conectividade
          isOnline = null;
          console.warn(`⏰ Heartbeat de ${baseId} sem horário válido (timestamp_valid=false)`);
        } else {
          isOnline = (now - tsMs) < this.HEARTBEAT_TIMEOUT;
        }
      }

      const wasOnline = this.stationStatus.get(baseId);
      // A03 (seção 6): o estado DESCONHECIDO também é gravado — depois de um
      // heartbeat inválido, o mapa não pode continuar afirmando "online" do
      // período anterior; consumidores veem null = sem evidência
      this.stationStatus.set(baseId, isOnline);
      // Transição notificada apenas entre estados CONHECIDOS (null não é
      // "offline" — ausência de evidência não é queda)
      if (isOnline !== null && wasOnline !== null && wasOnline !== isOnline) {
        if (!isOnline) {
          await this.notifyStationOffline(baseId, baseData);
        } else {
          await this.notifyStationOnline(baseId, baseData);
        }
      }

    } catch (error) {
      console.error(`Erro ao verificar heartbeat da base ${baseId}:`, error);
    }
  }

  // Notificar estação offline
  async notifyStationOffline(baseId, baseData) {
    const message = `🚨 *ESTAÇÃO OFFLINE*\n\n` +
      `🏢 ${baseData.name || baseId}\n` +
      `⏰ Sem heartbeat há mais de 30 minutos\n` +
      `📍 ${baseData.location ? `${baseData.location.lat}, ${baseData.location.lng}` : 'Localização não definida'}\n\n` +
      `⚠️ Verificar conexão da central`;

    // Notificar admin
    if (process.env.ADMIN_CHAT_ID) {
      try {
        await this.bot.telegram.sendMessage(process.env.ADMIN_CHAT_ID, message, {
          parse_mode: 'Markdown'
        });
      } catch (error) {
        console.error('Erro ao enviar notificação de estação offline:', error);
      }
    }

    console.log(`🚨 Estação ${baseId} está OFFLINE`);
  }

  // Notificar estação online
  async notifyStationOnline(baseId, baseData) {
    const message = `✅ *ESTAÇÃO ONLINE*\n\n` +
      `🏢 ${baseData.name || baseId}\n` +
      `🔄 Heartbeat restaurado\n` +
      `📍 ${baseData.location ? `${baseData.location.lat}, ${baseData.location.lng}` : 'Localização não definida'}`;

    // Notificar admin
    if (process.env.ADMIN_CHAT_ID) {
      try {
        await this.bot.telegram.sendMessage(process.env.ADMIN_CHAT_ID, message, {
          parse_mode: 'Markdown'
        });
      } catch (error) {
        console.error('Erro ao enviar notificação de estação online:', error);
      }
    }

    console.log(`✅ Estação ${baseId} está ONLINE`);
  }

  // Obter bikes disponíveis em uma estação
  async getAvailableBikes(stationId) {
    try {
      const bikesSnapshot = await firebaseService.db.ref('bikes')
        .orderByChild('base_id')
        .equalTo(stationId)
        .once('value');
      
      const bikes = bikesSnapshot.val() || {};
      const available = [];
      const now = Date.now();
      
      Object.entries(bikes).forEach(([bikeId, bikeData]) => {
        // Considerar disponível se teve contato BLE recente (< 5 min).
        // A03: tolera seconds (contrato do firmware) e milissegundos
        const rawContact = bikeData.last_ble_contact || 0;
        const lastContactMs = rawContact > 0 && rawContact < 1e11
          ? rawContact * 1000
          : rawContact;
        const isAvailable = (now - lastContactMs) < 5 * 60 * 1000;

        if (isAvailable) {
          available.push({
            id: bikeId,
            battery: bikeData.battery_voltage || 0,
            lastContact: lastContactMs,
            status: bikeData.status || 'unknown'
          });
        }
      });
      
      return available;
    } catch (error) {
      console.error('Erro ao buscar bikes disponíveis:', error);
      return [];
    }
  }

  // Obter status de uma estação
  async getStationStatus(stationId) {
    try {
      const baseSnapshot = await firebaseService.db.ref(`bases/${stationId}`).once('value');
      const baseData = baseSnapshot.val();
      
      if (!baseData) {
        return null;
      }

      const availableBikes = await this.getAvailableBikes(stationId);
      // A03: ausência de evidência não é "online" — só o estado verificado
      const knownOnline = this.stationStatus.has(stationId)
        ? this.stationStatus.get(stationId)
        : null;

      return {
        id: stationId,
        name: baseData.name || stationId,
        location: baseData.location,
        isOnline: knownOnline, // true | false | null (desconhecido)
        maxBikes: baseData.max_bikes || 10,
        availableBikes: availableBikes.length,
        bikes: availableBikes,
        lastSync: baseData.last_sync
      };
    } catch (error) {
      console.error('Erro ao obter status da estação:', error);
      return null;
    }
  }

  // Listar todas as estações
  async getAllStations() {
    try {
      const basesSnapshot = await firebaseService.db.ref('bases').once('value');
      const bases = basesSnapshot.val() || {};
      
      const stations = [];
      
      for (const [baseId, baseData] of Object.entries(bases)) {
        const status = await this.getStationStatus(baseId);
        if (status) {
          stations.push(status);
        }
      }
      
      return stations;
    } catch (error) {
      console.error('Erro ao listar estações:', error);
      return [];
    }
  }
}

module.exports = StationMonitor;