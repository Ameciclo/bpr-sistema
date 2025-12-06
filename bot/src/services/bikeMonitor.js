const firebaseService = require('../config/firebase');
const geolocationService = require('./geolocation');
const subscriptionManager = require('./subscriptionManager');
const rideCalculator = require('./rideCalculator');
const ChannelPublisher = require('./channelPublisher');

class BikeMonitorService {
  constructor(bot) {
    this.bot = bot;
    this.adminChatId = process.env.ADMIN_CHAT_ID;
    this.lastNotifications = new Map(); // Controle de spam
    this.bikeStates = new Map(); // Estado atual das bikes
    this.channelPublisher = new ChannelPublisher(bot);
    this.init();
  }

  init() {
    // Monitorar novas sessões
    firebaseService.listenToNewSessions((bikeId, sessionId, sessionData) => {
      this.handleNewSession(bikeId, sessionId, sessionData);
    });

    // Monitorar conexões BLE (chegadas na base)
    firebaseService.listenToBikeConnections((bikeId, connectionData) => {
      if (connectionData.event === 'connect') {
        this.handleBikeArrival(bikeId, connectionData.base);
      }
    });

    // Monitorar mudanças de bateria
    firebaseService.listenToBatteryChanges((bikeId, batteryData) => {
      this.handleBatteryChange(bikeId, batteryData);
    });

    console.log('🚴 Monitor de bicicletas iniciado (modo trigger)');
  }

  // Monitorar mudanças de bateria
  async handleBatteryChange(bikeId, batteryData) {
    const { voltage, charging, timestamp } = batteryData;
    const batteryPercent = this.calculateBatteryPercent(voltage);
    
    if (batteryPercent < 20 && !charging) {
      await this.notifyLowBattery(bikeId, batteryPercent, voltage);
    }
    
    const lastCharging = this.bikeStates.get(`${bikeId}_charging`) || false;
    if (lastCharging && !charging && batteryPercent > 80) {
      await this.notifyChargingComplete(bikeId, batteryPercent, voltage);
    }
    
    this.bikeStates.set(`${bikeId}_charging`, charging);
    this.bikeStates.set(`${bikeId}_battery`, batteryPercent);
  }

  calculateBatteryPercent(voltage) {
    const minVoltage = 3.0;
    const maxVoltage = 4.2;
    const percent = ((voltage - minVoltage) / (maxVoltage - minVoltage)) * 100;
    return Math.max(0, Math.min(100, Math.round(percent)));
  }

  async notifyLowBattery(bikeId, percent, voltage) {
    const lastNotify = this.lastNotifications.get(`low_battery_${bikeId}`) || 0;
    if (Date.now() - lastNotify < 30 * 60 * 1000) return;
    
    this.lastNotifications.set(`low_battery_${bikeId}`, Date.now());
    
    await this.notifySubscribers(bikeId, 'low_battery', { percent, voltage });
    
    const message = `🔋 *BATERIA BAIXA*\n\n` +
      `🚲 Bike: ${bikeId.toUpperCase()}\n` +
      `🔋 Nível: ${percent}% (${voltage.toFixed(2)}V)\n` +
      `⚠️ Necessário carregamento em breve`;
    
    this.sendNotification(message);
  }

  async notifyChargingComplete(bikeId, percent, voltage) {
    await this.notifySubscribers(bikeId, 'charging_complete', { percent, voltage });
    
    const message = `✅ *CARREGAMENTO CONCLUÍDO*\n\n` +
      `🚲 Bike: ${bikeId.toUpperCase()}\n` +
      `🔋 Nível: ${percent}% (${voltage.toFixed(2)}V)\n` +
      `🔌 Carregador desconectado`;
    
    this.sendNotification(message);
  }

  async handleNewSession(bikeId, sessionId, sessionData) {
    // Verificar se é nova sessão
    const lastSessionId = this.lastNotifications.get(`session_${bikeId}`);
    if (lastSessionId === sessionId) return;

    this.lastNotifications.set(`session_${bikeId}`, sessionId);

    // Iniciar nova viagem
    const rideId = rideCalculator.startRide(bikeId, sessionData.start);
    
    // Obter estação da bike
    const bikeData = await firebaseService.getBikeData(bikeId);
    const stationId = bikeData?.base_id || 'unknown';
    
    // Publicar saída no canal
    await this.channelPublisher.publishBikeDeparture(bikeId, stationId);
    
    // Notificar assinantes
    await this.notifySubscribers(bikeId, 'departure', { sessionId, stationId, timestamp: sessionData.start });
    
    // Notificar início de nova sessão (admin)
    const message = this.formatSessionStartMessage(bikeId, sessionId, sessionData);
    this.sendNotification(message);

    // Escutar scans desta sessão
    firebaseService.listenToSessionScans(bikeId, sessionId, (scanSnapshot) => {
      this.handleNewScan(bikeId, sessionId, scanSnapshot.val());
    });
  }

  async handleNewScan(bikeId, sessionId, scanData) {
    const [timestamp, networks] = scanData;
    
    // Verificar se é um novo scan
    const lastScanTime = this.lastNotifications.get(`scan_${bikeId}`) || 0;
    if (timestamp <= lastScanTime) return;

    this.lastNotifications.set(`scan_${bikeId}`, timestamp);

    // Converter formato de redes
    const formattedNetworks = networks.map(([ssid, bssid, rssi, channel]) => ({
      ssid, bssid, rssi, channel
    }));

    // Tentar obter localização
    const location = await geolocationService.getLocationFromWifi(formattedNetworks);
    
    if (location) {
      // Adicionar ponto à viagem ativa
      if (rideCalculator.hasActiveRide(bikeId)) {
        await rideCalculator.addPointToRide(bikeId, location, timestamp);
      }
      
      // Publicar movimento no canal (com throttling)
      await this.channelPublisher.publishBikeInMotion(bikeId, location);
      
      // Notificar usuários assinantes
      await this.notifySubscribers(bikeId, 'scan', { location, networks: formattedNetworks, timestamp });
    }
    
    // Notificação admin (apenas redes mais fortes)
    const message = this.formatScanMessage(bikeId, formattedNetworks, location, timestamp);
    this.sendNotification(message);
  }

  formatSessionStartMessage(bikeId, sessionId, sessionData) {
    const startDate = new Date(sessionData.start * 1000).toLocaleString('pt-BR');
    
    let message = `🚴*Bike ${bikeId.toUpperCase()}*\n`;
    message += `🎆 *NOVA SESSÃO INICIADA*\n`;
    message += `📅 ${startDate}\n`;
    message += `🏷️ Sessão: ${sessionId}\n`;
    message += `⚙️ Modo: ${sessionData.mode || 'normal'}\n`;
    
    return message;
  }

  formatScanMessage(bikeId, networks, location, timestamp) {
    const date = new Date(timestamp * 1000).toLocaleString('pt-BR');
    const networksCount = networks.length;
    const strongNetworks = networks.filter(n => n.rssi > -60).length;
    
    let message = `🚴*Bike ${bikeId.toUpperCase()}*\n`;
    message += `📅 ${date}\n`;
    message += `📡 ${networksCount} redes WiFi detectadas\n`;
    message += `💪 ${strongNetworks} redes com sinal forte\n`;

    if (location) {
      message += `📍 Localização estimada:\n`;
      message += `   Lat: ${location.latitude.toFixed(6)}\n`;
      message += `   Lng: ${location.longitude.toFixed(6)}\n`;
      message += `   Precisão: ±${location.accuracy}m\n`;
    }

    // Mostrar as 3 redes mais fortes
    const topNetworks = networks
      .sort((a, b) => b.rssi - a.rssi)
      .slice(0, 3);

    message += `\n🔝 *Redes mais fortes:*\n`;
    topNetworks.forEach((network, i) => {
      message += `${i + 1}. ${network.ssid} (${network.rssi}dBm)\n`;
    });

    return message;
  }

  async sendNotification(message) {
    if (!this.adminChatId) return;

    try {
      await this.bot.telegram.sendMessage(this.adminChatId, message, {
        parse_mode: 'Markdown'
      });
    } catch (error) {
      console.error('Erro ao enviar notificação:', error.message);
    }
  }

  // Método para obter resumo de uma bike
  async getBikeSummary(bikeId) {
    const bikeData = await firebaseService.getBikeData(bikeId);
    const lastSession = await firebaseService.getLastSession(bikeId);

    if (!bikeData) {
      return `❌ Bike ${bikeId} não encontrada`;
    }

    let message = `🚴*Resumo - Bike ${bikeId.toUpperCase()}*\n\n`;
    
    if (lastSession) {
      const { sessionId, start, end, mode, scans, battery, connections } = lastSession;
      
      // Status da sessão
      const isActive = !end;
      message += `📍 Status: ${isActive ? '🚀 Ativa' : '✅ Finalizada'}\n`;
      message += `🏷️ Sessão: ${sessionId}\n`;
      message += `⚙️ Modo: ${mode || 'normal'}\n`;
      
      // Datas
      const startDate = new Date(start * 1000).toLocaleString('pt-BR');
      message += `🚀 Início: ${startDate}\n`;
      
      if (end) {
        const endDate = new Date(end * 1000).toLocaleString('pt-BR');
        message += `🏁 Fim: ${endDate}\n`;
      }
      
      // Estatísticas
      if (scans) {
        message += `📊 Scans coletados: ${scans.length}\n`;
      }
      
      // Bateria
      if (battery && battery.length > 0) {
        const lastBattery = battery[battery.length - 1];
        message += `🔋 Bateria: ${lastBattery[1]}%\n`;
      }
      
      // Conexões
      if (connections && connections.length > 0) {
        const lastConnection = connections[connections.length - 1];
        const [time, event, base, ip] = lastConnection;
        
        if (event === 'connect') {
          message += `🏠 Conectada em: ${base}\n`;
        } else {
          message += `📶 Desconectada da base\n`;
        }
      }
    } else {
      message += `❌ Nenhuma sessão encontrada`;
    }

    return message;
  }

  // Calcular rota da última sessão
  async getLastRoute(bikeId) {
    const lastSession = await firebaseService.getLastSession(bikeId);
    
    if (!lastSession || !lastSession.scans || lastSession.scans.length < 2) {
      return `❌ Dados insuficientes para calcular rota da bike ${bikeId}`;
    }

    // Converter scans para formato compatível
    const formattedScans = lastSession.scans.map(([timestamp, networks]) => ({
      timestamp,
      networks: networks.map(([ssid, bssid, rssi, channel]) => ({
        ssid, bssid, rssi, channel
      }))
    }));

    const route = await geolocationService.calculateRoute(formattedScans);
    
    let message = `🗺️ *Última Rota - Bike ${bikeId.toUpperCase()}*\n\n`;
    message += `🏷️ Sessão: ${lastSession.sessionId}\n`;
    message += `📏 Distância total: ${route.totalDistance} km\n`;
    message += `📍 Pontos coletados: ${route.points}\n`;
    message += `📊 Scans analisados: ${formattedScans.length}\n`;

    if (route.locations.length > 0) {
      const firstPoint = route.locations[0];
      const lastPoint = route.locations[route.locations.length - 1];
      
      message += `\n🚀 *Início:*\n`;
      message += `   ${new Date(firstPoint.timestamp * 1000).toLocaleString('pt-BR')}\n`;
      message += `   ${firstPoint.latitude.toFixed(6)}, ${firstPoint.longitude.toFixed(6)}\n`;
      
      message += `\n🏁 *Fim:*\n`;
      message += `   ${new Date(lastPoint.timestamp * 1000).toLocaleString('pt-BR')}\n`;
      message += `   ${lastPoint.latitude.toFixed(6)}, ${lastPoint.longitude.toFixed(6)}\n`;
    }

    return message;
  }

  // Detectar chegada na base (quando sessão termina)
  async handleBikeArrival(bikeId, stationId) {
    // Finalizar viagem ativa
    const rideData = await rideCalculator.finishRide(bikeId, Date.now());
    
    // Publicar chegada no canal
    await this.channelPublisher.publishBikeArrival(bikeId, stationId, rideData);
    
    // Notificar assinantes
    await this.notifySubscribers(bikeId, 'arrival', { stationId, rideData, timestamp: Date.now() });
    
    // Notificação admin
    let message = `🏠 *Bike ${bikeId.toUpperCase()} chegou na base*\n\n`;
    message += `🏢 Estação: ${stationId}\n`;
    
    if (rideData) {
      message += `\n📊 *Viagem concluída:*\n`;
      message += `📏 ${rideData.km} km percorridos\n`;
      message += `🌱 ${rideData.co2_saved_g}g de CO₂ economizado\n`;
      message += `⏱️ ${rideData.duration_min} minutos\n`;
      message += `📍 ${rideData.points_count} pontos coletados\n`;
    }
    
    this.sendNotification(message);
  }

  // Notificar usuários assinantes
  async notifySubscribers(bikeId, eventType, data) {
    const users = subscriptionManager.getUsersForBike(bikeId);
    
    for (const userId of users) {
      try {
        let message = '';
        
        switch (eventType) {
          case 'departure':
            message = `🚀 *Sua bike saiu da estação*\n\n`;
            message += `🚲 ${bikeId.toUpperCase()}\n`;
            message += `🏢 ${data.stationId}\n`;
            message += `⏰ ${new Date(data.timestamp * 1000).toLocaleString('pt-BR')}\n\n`;
            message += `📡 Acompanhe a viagem em tempo real!`;
            break;
            
          case 'arrival':
            message = `🏠 *Sua bike chegou na estação*\n\n`;
            message += `🚲 ${bikeId.toUpperCase()}\n`;
            message += `🏢 ${data.stationId}\n`;
            
            if (data.rideData) {
              message += `\n📊 *Viagem concluída:*\n`;
              message += `📏 ${data.rideData.km} km\n`;
              message += `🌱 ${data.rideData.co2_saved_g}g CO₂ economizado\n`;
              message += `⏱️ ${data.rideData.duration_min} min\n`;
              message += `\n🗺️ [Ver rota completa](https://prarodar.org/ride/${bikeId}/${data.rideData.start_ts})`;
            }
            break;
            
          case 'scan':
            // Notificar apenas a cada 5 minutos para não spammar
            const lastScanNotify = this.lastNotifications.get(`user_scan_${userId}_${bikeId}`) || 0;
            if (Date.now() - lastScanNotify < 5 * 60 * 1000) continue;
            
            this.lastNotifications.set(`user_scan_${userId}_${bikeId}`, Date.now());
            
            message = `📍 *Sua bike está em movimento*\n\n`;
            message += `🚲 ${bikeId.toUpperCase()}\n`;
            message += `📡 ${data.networks.length} redes detectadas\n`;
            message += `📍 ${data.location.latitude.toFixed(4)}, ${data.location.longitude.toFixed(4)}\n`;
            message += `⏰ ${new Date(data.timestamp * 1000).toLocaleString('pt-BR')}`;
            break;
            
          case 'low_battery':
            message = `🔋 *Bateria baixa da sua bike*\n\n`;
            message += `🚲 ${bikeId.toUpperCase()}\n`;
            message += `🔋 Nível: ${data.percent}% (${data.voltage.toFixed(2)}V)\n`;
            message += `⚠️ Recomendamos carregar em breve`;
            break;
            
          case 'charging_complete':
            message = `✅ *Carregamento concluído*\n\n`;
            message += `🚲 ${bikeId.toUpperCase()}\n`;
            message += `🔋 Nível: ${data.percent}% (${data.voltage.toFixed(2)}V)\n`;
            message += `🚀 Pronta para nova aventura!`;
            break;
        }
        
        if (message) {
          await this.bot.telegram.sendMessage(userId, message, {
            parse_mode: 'Markdown',
            disable_web_page_preview: true
          });
        }
        
      } catch (error) {
        console.error(`Erro ao notificar usuário ${userId}:`, error.message);
      }
    }
  }
}

module.exports = BikeMonitorService;