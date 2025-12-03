const dotenv = require('dotenv');
dotenv.config();

console.log('🔧 Verificando variáveis de ambiente:\n');

const vars = [
  'TELEGRAM_BOT_TOKEN',
  'FIREBASE_PROJECT_ID', 
  'FIREBASE_DATABASE_URL',
  'FIREBASE_PRIVATE_KEY',
  'FIREBASE_CLIENT_EMAIL',
  'GOOGLE_GEOLOCATION_API_KEY',
  'ADMIN_CHAT_ID'
];

vars.forEach(varName => {
  const value = process.env[varName];
  const status = value ? '✅' : '❌';
  const preview = value ? (value.length > 50 ? value.substring(0, 50) + '...' : value) : 'MISSING';
  console.log(`${status} ${varName}: ${preview}`);
});