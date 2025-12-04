#!/bin/bash

# 🧹 BPR Central - Apagar Memória ESP32
# Remove TUDO da memória flash

set -e

echo "🧹 Apagando memória do ESP32..."
echo "==============================="

# Verificar PlatformIO
if ! command -v pio &> /dev/null; then
    echo "❌ PlatformIO não encontrado!"
    exit 1
fi

# Confirmar ação destrutiva
echo "⚠️  ATENÇÃO: Isso vai apagar TODA a memória!"
echo "   • Firmware atual"
echo "   • Configurações salvas"
echo "   • Sistema de arquivos"
echo ""
read -p "🗑️  Confirma apagar tudo? (y/N): " CONFIRM

if [ "$CONFIRM" != "y" ] && [ "$CONFIRM" != "Y" ]; then
    echo "❌ Cancelado"
    exit 1
fi

echo ""
echo "🔥 Apagando flash completa..."

# Apagar tudo usando esptool
pio run --target erase

if [ $? -eq 0 ]; then
    echo "✅ Memória apagada!"
    echo ""
    echo "💡 Agora use: ./upload.sh"
else
    echo "❌ Erro ao apagar memória"
    exit 1
fi