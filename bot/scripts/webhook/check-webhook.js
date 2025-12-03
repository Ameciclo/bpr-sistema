const axios = require('axios');

const BOT_TOKEN = process.env.TELEGRAM_BOT_TOKEN || 'your_bot_token_here';

async function checkWebhook() {
  try {
    const response = await axios.get(`https://api.telegram.org/bot${BOT_TOKEN}/getWebhookInfo`);
    console.log('📋 Webhook Info:', JSON.stringify(response.data, null, 2));
  } catch (error) {
    console.log('❌ Erro:', error.message);
  }
}

checkWebhook();