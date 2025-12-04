#!/bin/bash

# 🚲 BPR Central - Upload Firmware Limpo
# Instala só o firmware, sem configuração

set -e

echo "🚲 BPR Central - Firmware Limpo"
echo "==============================="

# Verificar PlatformIO
if ! command -v pio &> /dev/null; then
    echo "❌ PlatformIO não encontrado!"
    exit 1
fi

# Verificar diretório
if [ ! -f "platformio.ini" ]; then
    echo "❌ Execute no diretório firmware/central/"
    exit 1
fi

# Limpar configs antigas
echo "🧹 Limpando configurações antigas..."
rm -rf data/

echo "🔨 Compilando firmware..."
pio run

if [ $? -ne 0 ]; then
    echo "❌ Falha na compilação!"
    exit 1
fi

echo "⬆️ Enviando firmware..."
pio run --target upload

if [ $? -eq 0 ]; then
    echo ""
    echo "✅ FIRMWARE INSTALADO!"
    echo "===================="
    echo ""
    echo "🌐 ESP32 vai criar AP: BPR_Setup"
    echo "📱 Conecte no WiFi e acesse: 192.168.4.1"
    echo "⚙️ Configure via interface web"
    echo ""
    echo "💡 Depois da configuração, ele vai:"
    echo "   • Conectar no WiFi configurado"
    echo "   • Baixar config do Firebase"
    echo "   • Criar AP para bikes"
else
    echo "❌ Erro no upload!"
    exit 1
fi