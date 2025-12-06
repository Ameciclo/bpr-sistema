require('dotenv').config();
const { Telegraf } = require('telegraf');
const BikeMonitorService = require('./services/bikeMonitor');
const subscriptionManager = require('./services/subscriptionManager');
const StationMonitor = require('./services/stationMonitor');
const firebaseService = require('./config/firebase');

// Verificar variáveis de ambiente obrigatórias
const requiredEnvVars = [
  'TELEGRAM_BOT_TOKEN',
  'FIREBASE_PROJECT_ID',
  'FIREBASE_DATABASE_URL',
  'FIREBASE_PRIVATE_KEY',
  'FIREBASE_CLIENT_EMAIL'
];

const missingVars = requiredEnvVars.filter(varName => !process.env[varName]);
if (missingVars.length > 0) {
  console.error('❌ Variáveis de ambiente obrigatórias não configuradas:');
  missingVars.forEach(varName => console.error(`   - ${varName}`));
  process.exit(1);
}

// Inicializar bot
const bot = new Telegraf(process.env.TELEGRAM_BOT_TOKEN);
const bikeMonitor = new BikeMonitorService(bot);
const stationMonitor = new StationMonitor(bot);

// Comandos do bot
bot.start((ctx) => {
  const welcomeMessage = `
🚴 *Bot Pra Rodar*

Bem-vindo ao sistema de monitoramento de bicicletas compartilhadas!

🎯 *O que posso fazer:*
• 📊 Mostrar bikes disponíveis
• 📍 Acompanhar viagens em tempo real
• 📱 Enviar notificações personalizadas
• 🌱 Calcular CO₂ economizado
• 🗺️ Gerar mapas de rotas

🚀 *Começar:*
1. Use /bikes para ver bikes disponíveis
2. Use /seguir [bike] para receber notificações
3. Acompanhe suas viagens automaticamente!

📡 *Canal público:* @prarodar_updates
🆘 *Ajuda:* /help
  `;
  
  ctx.replyWithMarkdown(welcomeMessage);
});

bot.help((ctx) => {
  const helpMessage = `
🤖 *Comandos do Bot*

*📊 Consultas:*
/bikes - Lista bikes disponíveis
/status [bike] - Status de uma bike
/rota [bike] - Última rota percorrida
/estacao [id] - Status de uma estação

*📱 Notificações:*
/seguir [bike/estacao/sistema] - Receber alertas
/parar [bike/estacao/sistema] - Parar alertas
/minhas - Ver suas assinaturas

*🔧 Utilitários:*
/ping - Testar funcionamento
/help - Mostrar esta ajuda

*📡 Canal Público:*
Siga @prarodar_updates para acompanhar todas as atividades!

*Exemplos:*
\`/seguir intenso\` - Seguir bike específica
\`/seguir estacao_base01\` - Seguir estação
\`/seguir sistema\` - Seguir tudo
  `;
  
  ctx.replyWithMarkdown(helpMessage);
});

bot.command('ping', (ctx) => {
  ctx.reply('🏓 Pong! Bot funcionando normalmente.');
});

bot.command('status', async (ctx) => {
  const args = ctx.message.text.split(' ');
  const bikeId = args[1];
  
  if (!bikeId) {
    return ctx.reply('❌ Por favor, especifique o ID da bike.\nExemplo: /status intenso');
  }
  
  try {
    const summary = await bikeMonitor.getBikeSummary(bikeId.toLowerCase());
    ctx.replyWithMarkdown(summary);
  } catch (error) {
    console.error('Erro ao buscar status:', error);
    ctx.reply('❌ Erro ao buscar status da bike. Tente novamente.');
  }
});

bot.command('rota', async (ctx) => {
  const args = ctx.message.text.split(' ');
  const bikeId = args[1];
  
  if (!bikeId) {
    return ctx.reply('❌ Por favor, especifique o ID da bike.\nExemplo: /rota intenso');
  }
  
  try {
    ctx.reply('🔄 Calculando rota... Isso pode levar alguns segundos.');
    const route = await bikeMonitor.getLastRoute(bikeId.toLowerCase());
    ctx.replyWithMarkdown(route);
  } catch (error) {
    console.error('Erro ao calcular rota:', error);
    ctx.reply('❌ Erro ao calcular rota. Tente novamente.');
  }
});

bot.command('bikes', async (ctx) => {
  try {
    const stations = await stationMonitor.getAllStations();
    
    let message = `🚴 *Bikes Disponíveis*\n\n`;
    
    for (const station of stations) {
      message += `🏢 *${station.name}*\n`;
      message += `🔄 Status: ${station.isOnline ? '✅ Online' : '❌ Offline'}\n`;
      message += `🚲 Bikes: ${station.availableBikes}/${station.maxBikes}\n`;
      
      if (station.bikes.length > 0) {
        message += `\n🔋 *Bikes disponíveis:*\n`;
        station.bikes.forEach(bike => {
          const batteryIcon = bike.battery > 3.7 ? '🔋' : bike.battery > 3.5 ? '🔋' : '🪫';
          message += `• ${bike.id.toUpperCase()} ${batteryIcon} ${bike.battery.toFixed(1)}V\n`;
        });
      } else {
        message += `\n⚠️ Nenhuma bike disponível\n`;
      }
      
      message += `\n`;
    }
    
    message += `\n*Comandos:*\n`;
    message += `\`/status [bike]\` - Status de uma bike\n`;
    message += `\`/estacao [id]\` - Status de uma estação\n`;
    message += `\`/seguir [bike/estacao]\` - Receber notificações`;
    
    ctx.replyWithMarkdown(message);
  } catch (error) {
    console.error('Erro ao listar bikes:', error);
    ctx.reply('❌ Erro ao buscar informações das bikes.');
  }
});

// Middleware para log de mensagens
bot.use((ctx, next) => {
  const user = ctx.from;
  const message = ctx.message?.text || ctx.callbackQuery?.data || 'N/A';
  console.log(`📱 ${user.first_name} (${user.id}): ${message}`);
  return next();
});

// Tratamento de erros
bot.catch((err, ctx) => {
  console.error('❌ Erro no bot:', err);
  ctx.reply('❌ Ocorreu um erro interno. Tente novamente em alguns instantes.');
});

// Iniciar bot
bot.launch()
  .then(() => {
    console.log('🤖 Bot Pra Rodar iniciado com sucesso!');
    console.log('📱 Aguardando mensagens...');
    console.log('📡 Canal público:', process.env.PUBLIC_CHANNEL_ID || 'Não configurado');
    console.log('👨‍💼 Admin chat:', process.env.ADMIN_CHAT_ID || 'Não configurado');
  })
  .catch((error) => {
    console.error('❌ Erro ao iniciar bot:', error);
    process.exit(1);
  });

// Novos comandos de assinatura
bot.command('seguir', async (ctx) => {
  const args = ctx.message.text.split(' ');
  const target = args[1];
  const userId = ctx.from.id.toString();
  
  if (!target) {
    return ctx.reply('❌ Especifique o que seguir:\n/seguir [bike_id] - Seguir bike específica\n/seguir estacao_[id] - Seguir estação\n/seguir sistema - Seguir sistema inteiro');
  }
  
  try {
    let success = false;
    let message = '';
    
    if (target === 'sistema') {
      success = await subscriptionManager.subscribeToSystem(userId);
      message = success ? '✅ Você agora segue o sistema inteiro!' : '⚠️ Você já segue o sistema.';
    } else if (target.startsWith('estacao_')) {
      const stationId = target.replace('estacao_', '');
      success = await subscriptionManager.subscribeToStation(userId, stationId);
      message = success ? `✅ Você agora segue a estação ${stationId}!` : `⚠️ Você já segue esta estação.`;
    } else {
      success = await subscriptionManager.subscribeToBike(userId, target.toLowerCase());
      message = success ? `✅ Você agora segue a bike ${target.toUpperCase()}!` : `⚠️ Você já segue esta bike.`;
    }
    
    ctx.reply(message);
  } catch (error) {
    console.error('Erro ao criar assinatura:', error);
    ctx.reply('❌ Erro ao processar assinatura.');
  }
});

bot.command('parar', async (ctx) => {
  const args = ctx.message.text.split(' ');
  const target = args[1];
  const userId = ctx.from.id.toString();
  
  if (!target) {
    return ctx.reply('❌ Especifique o que parar de seguir:\n/parar [bike_id]\n/parar estacao_[id]\n/parar sistema');
  }
  
  try {
    let success = false;
    
    if (target === 'sistema') {
      success = await subscriptionManager.unsubscribe(userId, 'system');
    } else if (target.startsWith('estacao_')) {
      const stationId = target.replace('estacao_', '');
      success = await subscriptionManager.unsubscribe(userId, 'station', stationId);
    } else {
      success = await subscriptionManager.unsubscribe(userId, 'bike', target.toLowerCase());
    }
    
    const message = success ? '✅ Assinatura removida!' : '⚠️ Você não seguia isso.';
    ctx.reply(message);
  } catch (error) {
    console.error('Erro ao remover assinatura:', error);
    ctx.reply('❌ Erro ao processar solicitação.');
  }
});

bot.command('minhas', (ctx) => {
  const userId = ctx.from.id.toString();
  const subs = subscriptionManager.getUserSubscriptions(userId);
  
  let message = `📱 *Suas Assinaturas*\n\n`;
  
  if (subs.system) {
    message += `✅ Sistema completo\n`;
  }
  
  if (subs.bikes.length > 0) {
    message += `\n🚲 *Bikes:*\n`;
    subs.bikes.forEach(bike => {
      message += `• ${bike.toUpperCase()}\n`;
    });
  }
  
  if (subs.stations.length > 0) {
    message += `\n🏢 *Estações:*\n`;
    subs.stations.forEach(station => {
      message += `• ${station}\n`;
    });
  }
  
  if (!subs.system && subs.bikes.length === 0 && subs.stations.length === 0) {
    message += `⚠️ Você não segue nada ainda.\n\n`;
    message += `Use /seguir para começar!`;
  }
  
  ctx.replyWithMarkdown(message);
});

bot.command('estacao', async (ctx) => {
  const args = ctx.message.text.split(' ');
  const stationId = args[1];
  
  if (!stationId) {
    return ctx.reply('❌ Especifique o ID da estação.\nExemplo: /estacao base01');
  }
  
  try {
    const station = await stationMonitor.getStationStatus(stationId);
    
    if (!station) {
      return ctx.reply('❌ Estação não encontrada.');
    }
    
    let message = `🏢 *${station.name}*\n\n`;
    message += `🔄 Status: ${station.isOnline ? '✅ Online' : '❌ Offline'}\n`;
    message += `🚲 Bikes disponíveis: ${station.availableBikes}/${station.maxBikes}\n`;
    
    if (station.location) {
      message += `📍 Localização: ${station.location.lat}, ${station.location.lng}\n`;
    }
    
    if (station.bikes.length > 0) {
      message += `\n🔋 *Bikes:*\n`;
      station.bikes.forEach(bike => {
        const batteryIcon = bike.battery > 3.7 ? '🔋' : bike.battery > 3.5 ? '🔋' : '🪫';
        const lastContact = new Date(bike.lastContact).toLocaleString('pt-BR');
        message += `• ${bike.id.toUpperCase()} ${batteryIcon} ${bike.battery.toFixed(1)}V\n`;
        message += `  Último contato: ${lastContact}\n`;
      });
    }
    
    ctx.replyWithMarkdown(message);
  } catch (error) {
    console.error('Erro ao buscar estação:', error);
    ctx.reply('❌ Erro ao buscar informações da estação.');
  }
});

// Graceful shutdown
process.once('SIGINT', () => bot.stop('SIGINT'));
process.once('SIGTERM', () => bot.stop('SIGTERM'));