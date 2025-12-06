class ChannelPublisher {
  constructor(bot) {
    this.bot = bot;
    this.publicChannelId = process.env.PUBLIC_CHANNEL_ID; // @prarodar_updates
  }

  // Publicar chegada de bike na estação
  async publishBikeArrival(bikeId, stationId, rideData = null) {
    if (!this.publicChannelId) return;

    let message = `🏠 *Bike chegou na estação*\n\n`;
    message += `🚲 Bike: ${bikeId.toUpperCase()}\n`;
    message += `🏢 Estação: ${stationId}\n`;
    message += `⏰ ${new Date().toLocaleString('pt-BR')}\n`;

    if (rideData) {
      message += `\n📊 *Viagem concluída:*\n`;
      message += `📏 Distância: ${rideData.km} km\n`;
      message += `🌱 CO₂ economizado: ${rideData.co2_saved_g}g\n`;
      message += `⏱️ Duração: ${rideData.duration_min} min\n`;
      
      if (rideData.points_count > 0) {
        message += `📍 Pontos coletados: ${rideData.points_count}\n`;
        // Link para visualizar rota (a ser implementado)
        message += `🗺️ [Ver rota](https://prarodar.org/ride/${bikeId}/${rideData.start_ts})\n`;
      }
    }

    try {
      await this.bot.telegram.sendMessage(this.publicChannelId, message, {
        parse_mode: 'Markdown',
        disable_web_page_preview: true
      });
    } catch (error) {
      console.error('Erro ao publicar chegada no canal:', error);
    }
  }

  // Publicar saída de bike da estação
  async publishBikeDeparture(bikeId, stationId) {
    if (!this.publicChannelId) return;

    const message = `🚀 *Bike saiu da estação*\n\n` +
      `🚲 Bike: ${bikeId.toUpperCase()}\n` +
      `🏢 Estação: ${stationId}\n` +
      `⏰ ${new Date().toLocaleString('pt-BR')}\n\n` +
      `📡 Coletando dados da viagem...`;

    try {
      await this.bot.telegram.sendMessage(this.publicChannelId, message, {
        parse_mode: 'Markdown'
      });
    } catch (error) {
      console.error('Erro ao publicar saída no canal:', error);
    }
  }

  // Publicar estatísticas diárias
  async publishDailyStats(stats) {
    if (!this.publicChannelId) return;

    const message = `📊 *Resumo do dia*\n\n` +
      `🚲 Viagens: ${stats.total_rides_today || 0}\n` +
      `📏 Distância total: ${stats.km_today || 0} km\n` +
      `🌱 CO₂ economizado: ${Math.round((stats.co2_saved_today_g || 0) / 1000)} kg\n` +
      `🔋 Bikes ativas: ${stats.bikes_active || 0}\n\n` +
      `💚 Obrigado por usar o sistema Pra Rodar!`;

    try {
      await this.bot.telegram.sendMessage(this.publicChannelId, message, {
        parse_mode: 'Markdown'
      });
    } catch (error) {
      console.error('Erro ao publicar estatísticas no canal:', error);
    }
  }

  // Publicar bike em movimento (tempo real)
  async publishBikeInMotion(bikeId, location) {
    if (!this.publicChannelId) return;

    // Publicar apenas a cada 10 minutos para não spammar
    const lastPublish = this.lastMotionPublish?.get(bikeId) || 0;
    const now = Date.now();
    
    if (now - lastPublish < 10 * 60 * 1000) return;

    if (!this.lastMotionPublish) {
      this.lastMotionPublish = new Map();
    }
    this.lastMotionPublish.set(bikeId, now);

    const message = `🚴 *Bike em movimento*\n\n` +
      `🚲 ${bikeId.toUpperCase()} está rodando agora\n` +
      `📍 Região: ${location.latitude.toFixed(4)}, ${location.longitude.toFixed(4)}\n` +
      `📡 Precisão: ±${location.accuracy}m\n` +
      `⏰ ${new Date().toLocaleString('pt-BR')}`;

    try {
      await this.bot.telegram.sendMessage(this.publicChannelId, message, {
        parse_mode: 'Markdown'
      });
    } catch (error) {
      console.error('Erro ao publicar movimento no canal:', error);
    }
  }

  // Publicar alerta de sistema
  async publishSystemAlert(type, message) {
    if (!this.publicChannelId) return;

    const icons = {
      maintenance: '🔧',
      update: '🆙',
      issue: '⚠️',
      info: 'ℹ️'
    };

    const alertMessage = `${icons[type] || 'ℹ️'} *Aviso do Sistema*\n\n${message}`;

    try {
      await this.bot.telegram.sendMessage(this.publicChannelId, alertMessage, {
        parse_mode: 'Markdown'
      });
    } catch (error) {
      console.error('Erro ao publicar alerta no canal:', error);
    }
  }
}

module.exports = ChannelPublisher;