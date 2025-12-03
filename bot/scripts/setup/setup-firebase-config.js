require('dotenv').config();
const { execSync } = require('child_process');

try {
  const botToken = process.env.TELEGRAM_BOT_TOKEN;
  const adminChatId = process.env.ADMIN_CHAT_ID;
  const googleApiKey = process.env.GOOGLE_GEOLOCATION_API_KEY;

  if (!botToken || !adminChatId || !googleApiKey) {
    console.error("❌ Variáveis do .env não encontradas!");
    process.exit(1);
  }

  console.log("🔧 Configurando variáveis no Firebase Functions...");

  const command = `firebase functions:config:set telegram.bot_token="${botToken}" telegram.admin_chat_id="${adminChatId}" google.geolocation_api_key="${googleApiKey}"`;

  execSync(command, { stdio: 'inherit' });

  console.log("✅ Configuração concluída!");
  console.log("💡 Execute 'firebase deploy --only functions' para aplicar");

} catch (error) {
  console.error("❌ Erro:", error.message);
}