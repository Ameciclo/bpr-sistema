const axios = require('axios');

const BOT_TOKEN = process.env.TELEGRAM_BOT_TOKEN || 'your_bot_token_here';
const WEBHOOK_URL = 'https://us-central1-botaprarodar-routes.cloudfunctions.net/telegramBot';

async function setWebhook() {
  try {
    const response = await axios.post(`https://api.telegram.org/bot${BOT_TOKEN}/setWebhook`, {
      url: WEBHOOK_URL
    });
    
    console.log('✅ Webhook configurado:', response.data);
  } catch (error) {
    console.log('❌ Erro:', error.response?.data || error.message);
  }
}

setWebhook();