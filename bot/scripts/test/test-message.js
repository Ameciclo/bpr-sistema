const { Telegraf } = require('telegraf');
require('dotenv').config();

const bot = new Telegraf(process.env.TELEGRAM_BOT_TOKEN);

async function sendHelloWorld() {
  try {
    await bot.telegram.sendMessage(process.env.ADMIN_CHAT_ID, "Hello World! 🚴");
    console.log("✅ Mensagem enviada com sucesso!");
  } catch (error) {
    console.error("❌ Erro ao enviar mensagem:", error);
  }
}

sendHelloWorld();