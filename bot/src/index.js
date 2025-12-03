require('dotenv').config();
const { Telegraf } = require('telegraf');
const BikeMonitorService = require('./services/bikeMonitor');

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

// Comandos do bot
bot.start((ctx) => {
  const welcomeMessage = `
🚴*Bot de Monitoramento de Bicicletas*

Bem-vindo! Este bot monitora bicicletas compartilhadas em tempo real.

*Comandos disponíveis:*
/status [bike] - Status de uma bike específica
/rota [bike] - Última rota calculada
/bikes - Listar todas as bikes
/help - Mostrar esta ajuda

*Exemplo:*
\`/status intenso\`
\`/rota intenso\`
  `;
  
  ctx.replyWithMarkdown(welcomeMessage);
});

bot.help((ctx) => {
  const helpMessage = `
🤖 *Comandos do Bot*

/start - Mensagem de boas-vindas
/status [bike] - Mostra status atual da bike
/rota [bike] - Calcula última rota percorrida
/bikes - Lista todas as bikes monitoradas
/ping - Testa se o bot está funcionando

*Monitoramento Automático:*
• ✅ Notifica quando bike chega na base
• 🚀 Notifica quando bike sai da base  
• 📡 Mostra redes WiFi coletadas
• 📍 Calcula localização estimada
• 📏 Calcula distância percorrida

*Exemplo de uso:*
\`/status intenso\`
\`/rota intenso\`
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

bot.command('bikes', (ctx) => {
  // Por enquanto, lista as bikes conhecidas
  // Futuramente pode ser dinâmico baseado no Firebase
  const message = `
🚴*Bikes Monitoradas*

• intenso - Bike de teste/desenvolvimento

*Para mais informações:*
\`/status intenso\` - Ver status atual
\`/rota intenso\` - Ver última rota
  `;
  
  ctx.replyWithMarkdown(message);
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
    console.log('🤖 Bot iniciado com sucesso!');
    console.log('📱 Aguardando mensagens...');
  })
  .catch((error) => {
    console.error('❌ Erro ao iniciar bot:', error);
    process.exit(1);
  });

// Graceful shutdown
process.once('SIGINT', () => bot.stop('SIGINT'));
process.once('SIGTERM', () => bot.stop('SIGTERM'));