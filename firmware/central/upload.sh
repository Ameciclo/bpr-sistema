#!/bin/bash

# 🚲 BPR Central - Script de Upload e Configuração
# Configura uma nova central do zero

set -e

echo "🚲 BPR Central - Setup Completo"
echo "================================"

# Verificar se PlatformIO está instalado
if ! command -v pio &> /dev/null; then
    echo "❌ PlatformIO não encontrado!"
    echo "💡 Instale com: pip install platformio"
    exit 1
fi

# Verificar se está no diretório correto
if [ ! -f "platformio.ini" ]; then
    echo "❌ Execute este script no diretório firmware/central/"
    exit 1
fi

# Solicitar informações da central
echo ""
echo "📝 Configuração da Central:"
read -p "🆔 ID da Base (ex: ameciclo, cepas): " BASE_ID
read -p "📍 Nome da Base (ex: Ameciclo, CEPAS): " BASE_NAME
read -p "📶 WiFi SSID: " WIFI_SSID
read -s -p "🔑 WiFi Password: " WIFI_PASSWORD
echo ""

# Validar entrada
if [ -z "$BASE_ID" ] || [ -z "$BASE_NAME" ] || [ -z "$WIFI_SSID" ] || [ -z "$WIFI_PASSWORD" ]; then
    echo "❌ Todos os campos são obrigatórios!"
    exit 1
fi

echo ""
echo "⚙️ Configurações:"
echo "  Base ID: $BASE_ID"
echo "  Nome: $BASE_NAME"
echo "  WiFi: $WIFI_SSID"
echo ""

# Confirmar
read -p "✅ Confirma as configurações? (y/N): " CONFIRM
if [ "$CONFIRM" != "y" ] && [ "$CONFIRM" != "Y" ]; then
    echo "❌ Cancelado pelo usuário"
    exit 1
fi

# Criar configuração básica
echo "📝 Criando configuração básica..."
mkdir -p data

cat > data/config.json << EOF
{
  "base_id": "$BASE_ID",
  "wifi": {
    "ssid": "$WIFI_SSID",
    "password": "$WIFI_PASSWORD"
  },
  "firebase": {
    "database_url": "https://botaprarodar-routes-default-rtdb.firebaseio.com",
    "api_key": "AIzaSyBOf0iB1PE3byamxPaPnxRdjZHT-Wx5mKs"
  }
}
EOF

# Criar firebase_config.json para compatibilidade
cat > data/firebase_config.json << EOF
{
  "firebase_host": "botaprarodar-routes-default-rtdb.firebaseio.com",
  "firebase_auth": "AIzaSyBOf0iB1PE3byamxPaPnxRdjZHT-Wx5mKs",
  "base_id": "$BASE_ID",
  "base_name": "$BASE_NAME",
  "wifi_ssid": "$WIFI_SSID",
  "wifi_password": "$WIFI_PASSWORD"
}
EOF

echo "✅ Configurações criadas em data/"

# Build do projeto
echo ""
echo "🔨 Compilando firmware..."
pio run

if [ $? -ne 0 ]; then
    echo "❌ Falha na compilação!"
    exit 1
fi

echo "✅ Compilação OK"

# Upload do filesystem
echo ""
echo "📁 Enviando configurações para ESP32..."
pio run --target uploadfs

if [ $? -ne 0 ]; then
    echo "❌ Falha no upload do filesystem!"
    echo "💡 Verifique se o ESP32 está conectado"
    exit 1
fi

echo "✅ Configurações enviadas"

# Upload do firmware
echo ""
echo "⬆️ Enviando firmware para ESP32..."
pio run --target upload

if [ $? -ne 0 ]; then
    echo "❌ Falha no upload do firmware!"
    echo "💡 Verifique se o ESP32 está conectado"
    exit 1
fi

echo ""
echo "🎉 CENTRAL CONFIGURADA COM SUCESSO!"
echo "=================================="
echo ""
echo "📡 A central irá:"
echo "  1. Conectar no WiFi: $WIFI_SSID"
echo "  2. Anunciar como: BPR_BASE_$BASE_ID"
echo "  3. Baixar/criar configurações no Firebase"
echo "  4. Detectar bikes novas automaticamente"
echo ""
echo "💡 Próximos passos:"
echo "  • Abra o Serial Monitor: pio device monitor"
echo "  • Verifique logs de conexão"
echo "  • Configure dashboard para aprovar bikes"
echo ""
echo "🚲 Central $BASE_NAME pronta para uso!"