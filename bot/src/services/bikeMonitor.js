const firebaseService = require('../config/firebase');
const geolocationService = require('./geolocation');

class BikeMonitorService {
  constructor(bot) {
    this.bot = bot;
    this.adminChatId = process.env.ADMIN_CHAT_ID;
    this.lastNotifications = new Map(); // Controle de spam
    this.bikeStates = new Map(); // Estado atual das bikes
    this.init();
  }

  init() {
    // Monitorar novas sessões
    firebaseService.listenToNewSessions((bikeId, sessionId, sessionData) => {
      this.handleNewSession(bikeId, sessionId, sessionData);
    });

    console.log('🚴 Monitor de bicicletas iniciado (modo trigger)');
  }

  async handleNewSession(bikeId, sessionId, sessionData) {
    // Verificar se é nova sessão
    const lastSessionId = this.lastNotifications.get(`session_${bikeId}`);
    if (lastSessionId === sessionId) return;

    this.lastNotifications.set(`session_${bikeId}`, sessionId);

    // Notificar início de nova sessão
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
}

module.exports = BikeMonitorService;