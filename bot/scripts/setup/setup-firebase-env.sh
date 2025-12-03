#!/bin/bash
source .env

echo "🔧 Configurando variáveis no Firebase Functions..."

firebase functions:config:set \
  telegram.bot_token="$TELEGRAM_BOT_TOKEN" \
  admin.chat_id="$ADMIN_CHAT_ID" \
  google.api_key="$GOOGLE_GEOLOCATION_API_KEY"

echo "✅ Variáveis configuradas!"
echo "📋 Para verificar: firebase functions:config:get"