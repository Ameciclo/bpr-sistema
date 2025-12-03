#!/bin/bash

echo "🔍 Enviando comando de diagnóstico..."

# Detectar porta automaticamente
PORT=$(pio device list | grep -E "(ttyUSB|ttyACM)" | head -1 | awk '{print $1}')

if [ -z "$PORT" ]; then
    echo "❌ Nenhum dispositivo encontrado"
    exit 1
fi

echo "📡 Usando porta: $PORT"

# Enviar comando 'd' via echo
echo "d" > $PORT

echo "✅ Comando 'd' enviado!"
echo "💡 Use 'pio device monitor' para ver a resposta"