#!/bin/bash

echo "📋 Enviando comando de menu..."

# Detectar porta automaticamente
PORT=$(pio device list | grep -E "(ttyUSB|ttyACM)" | head -1 | awk '{print $1}')

if [ -z "$PORT" ]; then
    echo "❌ Nenhum dispositivo encontrado"
    exit 1
fi

echo "📡 Usando porta: $PORT"

# Enviar comando 'm' via echo
echo "m" > $PORT

echo "✅ Comando 'm' enviado!"
echo "💡 Use 'pio device monitor' para ver o menu"