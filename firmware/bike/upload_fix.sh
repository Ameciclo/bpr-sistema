#!/bin/bash

echo "🔧 Corrigindo problemas de upload ESP32-C3"
echo "=========================================="

# Tentar com velocidade mais baixa
echo "1. Tentando upload com velocidade reduzida..."
pio run --target upload --upload-port /dev/ttyACM1

if [ $? -ne 0 ]; then
    echo ""
    echo "2. Tentando forçar modo boot..."
    echo "   INSTRUÇÕES:"
    echo "   1. Mantenha o botão BOOT pressionado"
    echo "   2. Pressione e solte RESET"
    echo "   3. Solte BOOT"
    echo "   4. Pressione ENTER para continuar"
    read -p "   Pronto? "
    
    pio run --target upload --upload-port /dev/ttyACM1
fi

if [ $? -ne 0 ]; then
    echo ""
    echo "3. Tentando com velocidade ainda menor..."
    sed -i 's/upload_speed = 115200/upload_speed = 57600/' platformio.ini
    
    echo "   Mantenha BOOT pressionado novamente e pressione ENTER"
    read -p "   Pronto? "
    
    pio run --target upload --upload-port /dev/ttyACM1
    
    # Restaurar velocidade
    sed -i 's/upload_speed = 57600/upload_speed = 115200/' platformio.ini
fi

if [ $? -eq 0 ]; then
    echo ""
    echo "✅ Upload bem-sucedido!"
    echo "🔍 Para monitorar:"
    echo "   pio device monitor --port /dev/ttyACM1 --baud 115200"
else
    echo ""
    echo "❌ Upload falhou. Tente:"
    echo "   1. Verificar cabo USB (deve transmitir dados)"
    echo "   2. Verificar se ESP32 está bem conectado"
    echo "   3. Tentar outra porta USB"
    echo "   4. Usar modo boot manual (BOOT + RESET)"
fi