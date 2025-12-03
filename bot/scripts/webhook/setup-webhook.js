const { Telegraf } = require('telegraf');
require('dotenv').config();

const bot = new Telegraf(process.env.TELEGRAM_BOT_TOKEN);

async function setupWebhook() {
  try {
    // Primeiro, vamos ver o webhook atual
    const webhookInfo = await bot.telegram.getWebhookInfo();
    console.log("📋 Webhook atual:", webhookInfo);
    
    // Se você já fez deploy, substitua pela URL real do Firebase Functions
    // Formato: https://REGION-PROJECT_ID.cloudfunctions.net/telegramBot
    const webhookUrl = "https://us-central1-botaprarodar-routes.cloudfunctions.net/telegramBot";
    
    console.log("🔧 Configurando webhook para:", webhookUrl);
    
    const result = await bot.telegram.setWebhook(webhookUrl);
    console.log("✅ Webhook configurado:", result);
    
    // Verificar novamente
    const newWebhookInfo = await bot.telegram.getWebhookInfo();
    console.log("📋 Novo webhook:", newWebhookInfo);
    
  } catch (error) {
    console.error("❌ Erro ao configurar webhook:", error);
  }
}

setupWebhook();