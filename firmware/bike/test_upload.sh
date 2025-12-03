#!/bin/bash

echo "🚀 COMPILANDO E FAZENDO UPLOAD DO CÓDIGO CORRIGIDO"
echo "=================================================="

# Compilar
echo "📦 Compilando..."
pio run

if [ $? -eq 0 ]; then
    echo "✅ Compilação OK!"
    
    # Upload do código
    echo "⬆️ Fazendo upload do código..."
    pio run --target upload
    
    if [ $? -eq 0 ]; then
        echo "✅ Upload OK!"
        
        # Monitor serial
        echo "📺 Iniciando monitor serial..."
        echo "💡 Digite 'd' para diagnóstico completo"
        echo "💡 Digite 't' para teste de armazenamento"
        echo "💡 Digite 'm' para menu"
        echo "=================================================="
        pio device monitor --baud 115200
    else
        echo "❌ Falha no upload"
    fi
else
    echo "❌ Falha na compilação"
fi