const firebaseService = require('../config/firebase');
const geolocationService = require('./geolocation');
const subscriptionManager = require('./subscriptionManager');
const rideCalculator = require('./rideCalculator');
const ChannelPublisher = require('./channelPublisher');

const { normalizeScan } = geolocationService;

// A05: janela além da qual um scan é tratado como histórico, não tempo real
const HISTORICAL_SCAN_WINDOW_MS = 10 * 60 * 1000;

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
  // A04: percentual e tensão são campos distintos — sem conversão presumida
  async handleBatteryChange(bikeId, batteryData) {
    const { voltage, charging, timestamp, percent } = batteryData;

    // Percentual vem pronto do firmware; tensão é usada só como fallback
    const batteryPercent = (percent !== null && percent !== undefined)
      ? Math.max(0, Math.min(100, Math.round(percent)))
      : this.calculateBatteryPercent(voltage);

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
      `🔋 Nível: ${percent}%${voltage ? ` (${voltage.toFixed(2)}V)` : ''}\n` +
      `⚠️ Necessário carregamento em breve`;

    this.sendNotification(message);
  }

  async notifyChargingComplete(bikeId, percent, voltage) {
    await this.notifySubscribers(bikeId, 'charging_complete', { percent, voltage });

    const message = `✅ *CARREGAMENTO CONCLUÍDO*\n\n` +
      `🚲 Bike: ${bikeId.toUpperCase()}\n` +
      `🔋 Nível: ${percent}%${voltage ? ` (${voltage.toFixed(2)}V)` : ''}\n` +
      `🔌 Carregador desconectado`;
    
    this.sendNotification(message);
  }

  async handleNewSession(bikeId, sessionId, sessionData) {
    // Verificar se é nova sessão
    const lastSessionId = this.lastNotifications.get(`session_${bikeId}`);
    if (lastSessionId === sessionId) return;

    this.lastNotifications.set(`session_${bikeId}`, sessionId);

    // A02/A08: sessões v2 carregam session_start_ms (millis); o legado usava
    // `start` em segundos — normaliza para millis antes de qualquer uso
    const startMs = rideCalculator.toMs(sessionData.session_start_ms ?? sessionData.start);

    // Iniciar nova viagem
    const rideId = rideCalculator.startRide(bikeId, startMs);

    // Obter estação da bike
    const bikeData = await firebaseService.getBikeData(bikeId);
    const stationId = bikeData?.base_id || 'unknown';

    // Publicar saída no canal
    await this.channelPublisher.publishBikeDeparture(bikeId, stationId);

    // Notificar assinantes
    await this.notifySubscribers(bikeId, 'departure', { sessionId, stationId, timestamp: startMs });

    // Notificar início de nova sessão (admin)
    const message = this.formatSessionStartMessage(bikeId, sessionId, sessionData);
    this.sendNotification(message);

    // Escutar scans desta sessão (A05: registro interno evita duplicar listener)
    firebaseService.listenToSessionScans(bikeId, sessionId, (scanSnapshot) => {
      this.handleNewScan(bikeId, sessionId, scanSnapshot.val());
    });
  }

  async handleNewScan(bikeId, sessionId, scanData) {
    // A02: normaliza qualquer formato conhecido para o DTO interno
    const scan = normalizeScan(scanData);
    const { timestamp, networks } = scan;

    // Verificar se é um novo scan (dedupe inclui a sessão — A05)
    const lastScanTime = this.lastNotifications.get(`scan_${bikeId}`) || 0;
    const scanMs = rideCalculator.toMs(timestamp);
    if (scanMs <= lastScanTime) return;

    this.lastNotifications.set(`scan_${bikeId}`, scanMs);

    // A05: separa instante do evento do instante da ingestão — upload
    // histórico não é anunciado como movimento em tempo real
    const isHistorical = (Date.now() - scanMs) > HISTORICAL_SCAN_WINDOW_MS;

    // Tentar obter localização
    const location = await geolocationService.getLocationFromWifi(networks);

    if (location) {
      // Adicionar ponto à viagem ativa
      if (rideCalculator.hasActiveRide(bikeId)) {
        await rideCalculator.addPointToRide(bikeId, location, scanMs);
      }

      // Publicar movimento no canal (com throttling e marcador histórico)
      await this.channelPublisher.publishBikeInMotion(bikeId, location, isHistorical);

      // Notificar usuários assinantes
      await this.notifySubscribers(bikeId, 'scan', { location, networks, timestamp: scanMs, isHistorical });
    }

    // Notificação admin (apenas redes mais fortes)
    const message = this.formatScanMessage(bikeId, networks, location, scanMs, isHistorical);
    this.sendNotification(message);
  }

  formatSessionStartMessage(bikeId, sessionId, sessionData) {
    const startMs = rideCalculator.toMs(sessionData.session_start_ms ?? sessionData.start);

    let message = `🚴*Bike ${bikeId.toUpperCase()}*\n`;
    message += `🎆 *NOVA SESSÃO INICIADA*\n`;
    message += `📅 ${new Date(startMs).toLocaleString('pt-BR')}\n`;
    message += `🏷️ Sessão: ${sessionId}\n`;
    message += `⚙️ Esquema: v${sessionData.schema_version || 1}\n`;

    return message;
  }

  formatScanMessage(bikeId, networks, location, timestampMs, isHistorical = false) {
    const date = new Date(timestampMs).toLocaleString('pt-BR');
    const networksCount = networks.length;
    const strongNetworks = networks.filter(n => n.rssi > -60).length;

    let message = `🚴*Bike ${bikeId.toUpperCase()}*\n`;
    message += `📅 ${date}${isHistorical ? ' _(dado histórico)_' : ''}\n`;
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
      const { sessionId, session_start_ms: startMs, scans, battery, scan_count: scanCount } = lastSession;
      const complete = lastSession.complete !== false;

      // Status da sessão
      message += `📍 Status: ${complete ? '✅ Completa' : '⏳ Parcial'}\n`;
      message += `🏷️ Sessão: ${sessionId}\n`;

      // Datas (A08: unidades normalizadas — v2 carrega millis)
      if (startMs !== undefined && startMs !== null) {
        message += `🚀 Início: ${new Date(rideCalculator.toMs(startMs)).toLocaleString('pt-BR')}\n`;
      }

      // Estatísticas
      if (scans) {
        message += `📊 Scans coletados: ${scans.length}\n`;
      } else if (scanCount) {
        message += `📊 Scans coletados: ${scanCount}\n`;
      }

      // Bateria (A04: percentual direto, sem conversão de unidade)
      if (battery && battery.length > 0) {
        const lastBattery = battery[battery.length - 1];
        const percent = Array.isArray(lastBattery) ? lastBattery[1] : lastBattery.percent;
        if (percent !== undefined && percent !== null) {
          message += `🔋 Bateria: ${percent}%\n`;
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

    // A02: normaliza cada scan para o DTO interno (scans v2 já são objetos)
    const formattedScans = lastSession.scans.map((scan) => normalizeScan({
      timestamp: scan.ts_ms,
      networks: scan.networks
    }));

    const route = await geolocationService.calculateRoute(formattedScans);

    let message = `🗺️ *Última Rota - Bike ${bikeId.toUpperCase()}*\n\n`;
    message += `🏷️ Sessão: ${lastSession.sessionId}\n`;
    message += `📏 Distância total: ${route.totalDistance} km *(estimativa)*\n`;
    message += `📍 Pontos coletados: ${route.points}\n`;
    message += `📊 Scans analisados: ${formattedScans.length}\n`;

    if (route.locations.length > 0) {
      const firstPoint = route.locations[0];
      const lastPoint = route.locations[route.locations.length - 1];

      message += `\n🚀 *Início:*\n`;
      message += `   ${new Date(rideCalculator.toMs(firstPoint.timestamp)).toLocaleString('pt-BR')}\n`;
      message += `   ${firstPoint.latitude.toFixed(6)}, ${firstPoint.longitude.toFixed(6)}\n`;

      message += `\n🏁 *Fim:*\n`;
      message += `   ${new Date(rideCalculator.toMs(lastPoint.timestamp)).toLocaleString('pt-BR')}\n`;
      message += `   ${lastPoint.latitude.toFixed(6)}, ${lastPoint.longitude.toFixed(6)}\n`;
    }

    return message;
  }

  // Detectar chegada na base (quando sessão termina)
  async handleBikeArrival(bikeId, stationId, arrivalMs = null) {
    // A08: chegada com unidade coerente (ms); fallback = agora
    const arrivalTime = arrivalMs !== null ? rideCalculator.toMs(arrivalMs) : Date.now();

    // Finalizar viagem ativa
    const rideData = await rideCalculator.finishRide(bikeId, arrivalTime);

    // Publicar chegada no canal
    await this.channelPublisher.publishBikeArrival(bikeId, stationId, rideData);

    // Notificar assinantes
    await this.notifySubscribers(bikeId, 'arrival', { stationId, rideData, timestamp: arrivalTime });
    
    // Notificação admin
    let message = `🏠 *Bike ${bikeId.toUpperCase()} chegou na base*\n\n`;
    message += `🏢 Estação: ${stationId}\n`;
    
    if (rideData) {
      message += `\n📊 *Viagem concluída:*\n`;
      message += `📏 ${rideData.km} km percorridos *(estimativa)*\n`;
      message += `🌱 ${rideData.co2_saved_g}g de CO₂ economizado *(estimativa)*\n`;
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
            message += `⏰ ${new Date(data.timestamp).toLocaleString('pt-BR')}\n\n`;
            message += `📡 Acompanhe a viagem!`;
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
            message += `⏰ ${new Date(data.timestamp).toLocaleString('pt-BR')}`;
            // A05: upload histórico não é anunciado como tempo real
            if (data.isHistorical) {
              message += `\n_(dado histórico da viagem)_`;
            }
            break;
            
          case 'low_battery':
            message = `🔋 *Bateria baixa da sua bike*\n\n`;
            message += `🚲 ${bikeId.toUpperCase()}\n`;
            const v = data.voltage ? ` (${data.voltage.toFixed(2)}V)` : '';
            message += `🔋 Nível: ${data.percent}%${v}\n`;
            message += `⚠️ Recomendamos carregar em breve`;
            break;
            
          case 'charging_complete':
            message = `✅ *Carregamento concluído*\n\n`;
            message += `🚲 ${bikeId.toUpperCase()}\n`;
            message += `🔋 Nível: ${data.percent}%${data.voltage ? ` (${data.voltage.toFixed(2)}V)` : ''}\n`;
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