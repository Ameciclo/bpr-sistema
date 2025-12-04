#!/bin/bash

echo "🚲 BPR Bike Upload Script"
echo "========================"

# Verificar se PlatformIO está instalado
if ! command -v pio &> /dev/null; then
    echo "❌ PlatformIO não encontrado. Instalando..."
    pip install platformio
fi

# Detectar porta serial
PORTS=$(pio device list | grep -E "(ttyUSB|ttyACM|cu\.)" | awk '{print $1}')

if [ -z "$PORTS" ]; then
    echo "❌ Nenhuma porta serial encontrada"
    echo "   Conecte o ESP32-C3 via USB"
    exit 1
fi

echo "📱 Portas disponíveis:"
echo "$PORTS" | nl -v0

if [ $(echo "$PORTS" | wc -l) -gt 1 ]; then
    echo -n "Escolha a porta (0-$(($(echo "$PORTS" | wc -l)-1))): "
    read PORT_INDEX
    SELECTED_PORT=$(echo "$PORTS" | sed -n "$((PORT_INDEX+1))p")
else
    SELECTED_PORT=$(echo "$PORTS" | head -1)
fi

echo "✅ Usando porta: $SELECTED_PORT"

# Atualizar platformio.ini com a porta
sed -i "s|^upload_port.*|upload_port = $SELECTED_PORT|" platformio.ini
sed -i "s|^monitor_port.*|monitor_port = $SELECTED_PORT|" platformio.ini

# Se não existir, adicionar
if ! grep -q "upload_port" platformio.ini; then
    echo "upload_port = $SELECTED_PORT" >> platformio.ini
fi
if ! grep -q "monitor_port" platformio.ini; then
    echo "monitor_port = $SELECTED_PORT" >> platformio.ini
fi

echo "🔧 Compilando..."
pio run

if [ $? -ne 0 ]; then
    echo "❌ Falha na compilação"
    exit 1
fi

echo "📁 Uploading filesystem (config.json)..."
pio run --target uploadfs

if [ $? -ne 0 ]; then
    echo "⚠️ Falha no upload do filesystem (continuando...)"
fi

echo "⬆️ Uploading firmware..."
pio run --target upload

if [ $? -eq 0 ]; then
    echo "✅ Upload concluído!"
    echo ""
    echo "🔍 Para monitorar:"
    echo "   pio device monitor --port $SELECTED_PORT --baud 115200"
    echo ""
    echo "🔄 Para reconectar:"
    echo "   ./upload.sh"
else
    echo "❌ Falha no upload do firmware"
    exit 1
fi