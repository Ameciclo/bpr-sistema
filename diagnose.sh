#!/bin/bash

echo "🔍 BPR Sistema - Diagnóstico de Dispositivos"
echo "============================================="
echo

# 1. Verificar PlatformIO
echo "📋 1. PlatformIO Status:"
if command -v pio &> /dev/null; then
    echo "   ✅ PlatformIO instalado: $(pio --version)"
else
    echo "   ❌ PlatformIO não encontrado"
fi
echo

# 2. Verificar dispositivos USB
echo "📱 2. Dispositivos USB:"
USB_DEVICES=$(lsusb | grep -E "(CP210|CH340|FT232|ESP32|Arduino)")
if [ -n "$USB_DEVICES" ]; then
    echo "$USB_DEVICES"
else
    echo "   ❌ Nenhum dispositivo ESP32/Arduino detectado"
    echo "   💡 Dispositivos USB conectados:"
    lsusb | head -5
fi
echo

# 3. Verificar portas seriais
echo "🔌 3. Portas Seriais:"
SERIAL_PORTS=$(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null)
if [ -n "$SERIAL_PORTS" ]; then
    echo "   ✅ Portas encontradas:"
    for port in $SERIAL_PORTS; do
        echo "      - $port"
    done
else
    echo "   ❌ Nenhuma porta serial USB detectada"
fi
echo

# 4. PlatformIO device list
echo "🖥️  4. PlatformIO Devices:"
PIO_DEVICES=$(pio device list | grep -E "(ttyUSB|ttyACM|Hardware ID)" | head -10)
if [ -n "$PIO_DEVICES" ]; then
    echo "$PIO_DEVICES"
else
    echo "   ❌ Nenhum dispositivo detectado pelo PlatformIO"
fi
echo

# 5. Verificar permissões
echo "🔐 5. Permissões:"
GROUPS=$(groups | grep -E "(dialout|uucp|plugdev)")
if [ -n "$GROUPS" ]; then
    echo "   ✅ Usuário nos grupos: $GROUPS"
else
    echo "   ⚠️  Usuário pode não ter permissões para portas seriais"
    echo "   💡 Execute: sudo usermod -a -G dialout $USER"
fi
echo

# 6. Soluções
echo "🛠️  6. Soluções Possíveis:"
echo "   1. 🔌 Conectar ESP32-C3 via cabo USB"
echo "   2. 🔄 Pressionar botão RESET no ESP32-C3"
echo "   3. 🔧 Verificar cabo USB (dados, não só energia)"
echo "   4. 🖥️  Tentar outra porta USB"
echo "   5. 🔐 Adicionar usuário ao grupo dialout:"
echo "      sudo usermod -a -G dialout $USER"
echo "      (depois fazer logout/login)"
echo "   6. 📱 Verificar se ESP32-C3 está em modo bootloader"
echo "      (segurar BOOT + pressionar RESET)"
echo

# 7. Comandos úteis
echo "🚀 7. Comandos Úteis:"
echo "   - Listar dispositivos: pio device list"
echo "   - Monitor serial: pio device monitor --baud 115200"
echo "   - Upload bike: cd firmware/bike && ./upload.sh"
echo "   - Upload hub: cd firmware/hub && pio run --target upload"
echo