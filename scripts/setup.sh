#!/bin/bash

# Setup inicial do projeto BPR Sistema

echo "🚀 Configurando BPR Sistema..."

# Verificar dependências
echo "📋 Verificando dependências..."

# PlatformIO
if ! command -v pio &> /dev/null; then
    echo "❌ PlatformIO não encontrado. Instale: https://platformio.org/install"
    exit 1
fi

# Node.js
if ! command -v node &> /dev/null; then
    echo "❌ Node.js não encontrado. Instale: https://nodejs.org"
    exit 1
fi

echo "✅ Dependências OK"

# Setup firmware
echo "🔧 Configurando firmware..."
cd firmware/bike
if [ ! -f "data/config.txt" ]; then
    cp data-example/config.txt data/config.txt
    echo "📝 Arquivo data/config.txt criado. Configure antes do upload!"
fi
cd ../..

# Setup shared configs
echo "📋 Configurando arquivos compartilhados..."
if [ ! -f "shared/config/firebase.json" ]; then
    cp shared/config/firebase.example.json shared/config/firebase.json
    echo "📝 Arquivo firebase.json criado. Configure suas credenciais!"
fi

# Criar .env principal
if [ ! -f ".env" ]; then
    cat > .env << EOF
# Configurações do BPR Sistema

# Firebase
FIREBASE_URL=https://seu-projeto-default-rtdb.firebaseio.com
FIREBASE_API_KEY=AIzaSyA...SuaChaveAqui

# Telegram Bot
TELEGRAM_BOT_TOKEN=123456:ABC-DEF1234ghIkl-zyx57W2v1u123ew11
TELEGRAM_CHAT_ID=-1001234567890

# Desenvolvimento
NODE_ENV=development
EOF
    echo "📝 Arquivo .env criado. Configure suas variáveis!"
fi

echo "✅ Setup concluído!"
echo ""
echo "📋 Próximos passos:"
echo "1. Configure data/config.txt no firmware"
echo "2. Configure shared/config/firebase.json"
echo "3. Configure .env na raiz"
echo "4. Execute: ./scripts/dev-firmware.sh"