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

    // N10: o listener de conexões (schema v1: sessions/connections) foi
    // REMOVIDO — nenhum produtor escreve esse caminho no fluxo v2 e as
    // viagens nunca eram finalizadas. A chegada agora deriva do contrato
    // ATIVO: sessão v2 montada = bike entregou dados na base.

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

    // Obter estação da bike (necessário para o encerramento da viagem anterior)
    const bikeData = await firebaseService.getBikeData(bikeId);
    const stationId = bikeData?.base_id || 'unknown';

    // N10: fechamento do ciclo pela ponta a ponta — a viagem ATIVA (sessão
    // anterior) é finalizada quando a próxima sessão é montada (bike
    // entregou dados na base). Sem isso, viagens ficavam pendentes para
    // sempre aguardando o listener v1 de connections que nunca dispara.
    if (rideCalculator.hasActiveRide(bikeId)) {
      await this.handleBikeArrival(bikeId, stationId);
    }

    // N04: UNIDADE por origem — v2 session_start_ms já é ms (conversão
    // única); legado `start` era segundos. A heurística toMs encadeada
    // multiplicava o valor duas vezes por 1000
    const startMs = (sessionData.session_start_ms !== undefined &&
                     sessionData.session_start_ms !== null)
      ? rideCalculator.fromV2Ms(sessionData.session_start_ms)
      : rideCalculator.fromLegacySec(sessionData.start);

    // Item 2 (ID-01/ID-02/ID-07): identidade da viagem = bike do cadastro +
    // origem estável (session_id + session_salt da sessão montada). O
    // timestamp relativo NÃO é identidade; sem origem suficiente a viagem
    // fica PENDENTE (explícita), nunca substituída por uptime.
    const sessionIdNum = Number(sessionData.session_id);
    const sessionSaltNum = Number(sessionData.session_salt);
    const rideId = rideCalculator.startRide(bikeId, {
      sessionId: sessionIdNum,
      sessionSalt: sessionSaltNum,
      startMs
    });
    if (!rideId) {
      // ID-07: pendência visível — a sessão continua sendo notificada, mas
      // sem viagem/agregação até a origem existir
      this.sendNotification(`⚠️ Bike ${bikeId.toUpperCase()}: sessão ${sessionId} ` +
        `sem origem suficiente (session_id/salt ausentes) — viagem NÃO derivada; ` +
        `pendência registrada`);
    }

    // Publicar saída no canal
    await this.channelPublisher.publishBikeDeparture(bikeId, stationId);

    // Notificar assinantes
    await this.notifySubscribers(bikeId, 'departure', { sessionId, stationId, timestamp: startMs });

    // Notificar início de nova sessão (admin)
    const message = this.formatSessionStartMessage(bikeId, sessionId, sessionData);
    this.sendNotification(message);

    // N04: "histórico" ancorado no INGEST do servidor — a comparação
    // anterior (Date.now() - ts_ms do boot) misturava relógios e marcava
    // TODOS os scans como históricos
    const ingestTs = Number(sessionData.ingest_ts) || 0;
    const sessionIsHistorical = ingestTs > 0 &&
      (Date.now() / 1000 - ingestTs) > (HISTORICAL_SCAN_WINDOW_MS / 1000);

    // Escutar scans desta sessão (A05: uma assinatura ativa por bike)
    firebaseService.listenToSessionScans(bikeId, sessionId, (scanSnapshot) => {
      this.handleNewScan(bikeId, sessionId, scanSnapshot.val(), sessionIsHistorical);
    });

    return rideId;
  }

  async handleNewScan(bikeId, sessionId, scanData, sessionIsHistorical = false) {
    // A02: normaliza qualquer formato conhecido para o DTO interno
    const scan = normalizeScan(scanData);
    const { timestamp, networks } = scan;

    // Verificar se é um novo scan (dedupe inclui a SESSÃO — A05/N10: scan de
    // sessão nova com timestamp menor não era recusado pelo da anterior)
    const lastScanTime = this.lastNotifications.get(`scan_${bikeId}_${sessionId}`) || 0;
    const scanMs = rideCalculator.fromV2Ms(timestamp);
    if (scanMs <= lastScanTime) return;

    this.lastNotifications.set(`scan_${bikeId}_${sessionId}`, scanMs);

    // A05/N04: instante do evento vs instante da ingestão — decidido pela
    // âncora do servidor (ingest_ts), não por mistura de relógios
    const isHistorical = sessionIsHistorical;

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
    // N04: session_start_ms é relativo ao boot da bike — exibir como data
    // absoluta (new Date) inventava horário; permanece com rótulo relativo
    const startLabel = (sessionData.session_start_ms !== undefined &&
                        sessionData.session_start_ms !== null)
      ? `relativo ao boot (${sessionData.session_start_ms} ms)`
      : 'origem desconhecida';

    let message = `🚴*Bike ${bikeId.toUpperCase()}*\n`;
    message += `🎆 *NOVA SESSÃO NORMALIZADA*\n`;
    message += `📅 ${startLabel}\n`;
    message += `🏷️ Sessão: ${sessionId}\n`;
    message += `⚙️ Esquema: v${sessionData.schema_version || 1}\n`;
    if (sessionData.resumed) {
      message += `⚠️ Sessão retomada entre boots (dados de origens mistas)\n`;
    }

    return message;
  }

  formatScanMessage(bikeId, networks, location, timestampMs, isHistorical = false) {
    // N04: ts_ms é relativo ao boot — rótulo relativo, não data inventada
    const date = `boot+${timestampMs} ms${isHistorical ? ' _(dado histórico)_' : ''}`;
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

      // Datas (N04: session_start_ms é relativo ao boot — rótulo relativo)
      if (startMs !== undefined && startMs !== null) {
        message += `🚀 Início: relativo ao boot (${startMs} ms)\n`;
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

      // N04: timestamps dos scans são relativos ao boot da bike — exibidos
      // como decorrido, sem inventar data absoluta
      message += `\n🚀 *Início:*\n`;
      message += `   boot+${rideCalculator.fromV2Ms(firstPoint.timestamp)} ms\n`;
      message += `   ${firstPoint.latitude.toFixed(6)}, ${firstPoint.longitude.toFixed(6)}\n`;

      message += `\n🏁 *Fim:*\n`;
      message += `   boot+${rideCalculator.fromV2Ms(lastPoint.timestamp)} ms\n`;
      message += `   ${lastPoint.latitude.toFixed(6)}, ${lastPoint.longitude.toFixed(6)}\n`;
    }

    return message;
  }

  // Detectar chegada na base (quando sessão termina)
  async handleBikeArrival(bikeId, stationId, arrivalMs = null) {
    // N04: unidade única — chegada vem do relógio do servidor (ms absolutos)
    const arrivalTime = arrivalMs !== null ? rideCalculator.fromV2Ms(arrivalMs) : Date.now();

    // Finalizar viagem ativa.
    // Item 2 (ID-06): a estação entra como PROVENIÊNCIA da entrega
    // (delivery_bases na viagem) — a mesma viagem entregue por outra
    // estação continua sendo a MESMA viagem
    const rideData = await rideCalculator.finishRide(bikeId, arrivalTime, { stationId });

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
            message += `⏱️ Sessão iniciada em boot+${data.timestamp} ms\n\n`;
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
              // Item 2: referência estável da viagem (rideId canônico)
              const ref = data.rideData.rideId || data.rideData.start_ts;
              message += `\n🗺️ [Ver rota completa](https://prarodar.org/ride/${bikeId}/${ref})`;
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
            message += `⏱️ Leitura em boot+${data.timestamp} ms`;
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