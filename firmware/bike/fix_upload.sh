#!/bin/bash

echo "=== SOLUCIONADOR DE PROBLEMAS DE UPLOAD ESP32-C3 ==="
echo

# 1. Verificar se o dispositivo está conectado
echo "1. Verificando dispositivos conectados..."
lsusb | grep -i "esp\|silicon\|cp210\|ch340\|ftdi"
echo

# 2. Verificar portas disponíveis
echo "2. Portas seriais disponíveis:"
ls -la /dev/tty* | grep -E "(USB|ACM)" 2>/dev/null || echo "Nenhuma porta USB/ACM encontrada"
echo

# 3. Verificar permissões
echo "3. Verificando permissões do usuário..."
groups $USER | grep -q dialout && echo "✓ Usuário no grupo dialout" || echo "✗ Usuário NÃO está no grupo dialout"
echo

# 4. Tentar diferentes velocidades de upload
echo "4. Tentativas de upload com diferentes configurações..."
echo

echo "Tentativa 1: Velocidade reduzida (115200)"
pio run --target upload --upload-port /dev/ttyACM0 --upload-speed 115200

if [ $? -ne 0 ]; then
    echo
    echo "Tentativa 2: Forçar modo boot"
    echo "INSTRUÇÕES:"
    echo "1. Mantenha o botão BOOT pressionado"
    echo "2. Pressione e solte o botão RESET"
    echo "3. Solte o botão BOOT"
    echo "4. Pressione Enter para continuar..."
    read
    
    pio run --target upload --upload-port /dev/ttyACM0 --upload-speed 115200
fi

if [ $? -ne 0 ]; then
    echo
    echo "Tentativa 3: Velocidade ainda menor (9600)"
    pio run --target upload --upload-port /dev/ttyACM0 --upload-speed 9600
fi

echo
echo "=== DICAS ADICIONAIS ==="
echo "• Se ainda não funcionar, tente desconectar e reconectar o cabo USB"
echo "• Verifique se o cabo USB suporta dados (não apenas energia)"
echo "• Tente uma porta USB diferente"
echo "• Para ESP32-C3, às vezes é necessário manter BOOT pressionado durante todo o upload"