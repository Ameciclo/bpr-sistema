# 🚀 Como Fazer Upload do Firmware

## 🎯 Método Rápido (Recomendado)

```bash
cd firmware/bike
./upload.sh
```

O script fará **tudo automaticamente**:
- ✅ Detecta portas seriais disponíveis
- ✅ Compila o firmware  
- ✅ Upload do filesystem (config.json)
- ✅ Upload do firmware
- ✅ Configura porta no platformio.ini

## 🔧 Método Manual

### 1. Verificar Porta
```bash
pio device list
```

Procure por:
- **Linux**: `/dev/ttyUSB0`, `/dev/ttyACM0`, `/dev/ttyACM1`
- **macOS**: `/dev/cu.usbserial-*`, `/dev/cu.usbmodem*`
- **Windows**: `COM3`, `COM4`, etc.

### 2. Configurar Porta
Edite `platformio.ini`:
```ini
upload_port = /dev/ttyACM0    # Sua porta aqui
monitor_port = /dev/ttyACM0   # Mesma porta
```

### 3. Upload
```bash
# Compilar
pio run

# Upload filesystem (config.json)
pio run --target uploadfs

# Upload firmware
pio run --target upload
```

## 📱 Monitoramento

### Após Upload
```bash
pio device monitor --baud 115200
```

### Ou usando porta específica
```bash
pio device monitor --port /dev/ttyACM0 --baud 115200
```

## 🐛 Troubleshooting

### ❌ Porta não encontrada
```bash
# Verificar conexão USB
lsusb | grep -i esp

# Verificar permissões (Linux)
sudo usermod -a -G dialout $USER
# Logout/login após comando acima

# Verificar se ESP32 está em boot mode
# Pressione BOOT + RESET, solte RESET, solte BOOT
```

### ❌ Upload falha
```bash
# Tentar com velocidade menor
# Edite platformio.ini:
upload_speed = 460800  # ou 115200

# Forçar boot mode
# Mantenha BOOT pressionado durante upload
```

### ❌ Filesystem falha
```bash
# Upload só o firmware (sem config)
pio run --target upload

# Config será criada com valores padrão
```

### ❌ Compilação falha
```bash
# Limpar build
pio run --target clean

# Reinstalar dependências
pio pkg install

# Tentar novamente
pio run
```

## 📋 Checklist Pré-Upload

- [ ] ESP32-C3 conectado via USB
- [ ] Cabo USB funcional (dados, não só energia)
- [ ] Porta serial detectada (`pio device list`)
- [ ] Permissões corretas (Linux/macOS)
- [ ] PlatformIO instalado (`pip install platformio`)

## 🔄 Workflow Completo

```bash
# 1. Conectar ESP32-C3
# 2. Executar script
./upload.sh

# 3. Monitorar logs
pio device monitor

# 4. Verificar saída:
# ✅ Sistema inicializado
# 🆔 Bike ID: bike_001
# 🔋 Bateria: X.XXV
# 🔄 Estado: BOOT
```

## 💡 Dicas

- **Primeira vez**: Use `./upload.sh` sempre
- **Re-upload**: Pode usar `pio run --target upload` direto
- **Debug**: Monitor serial mostra todos os logs
- **Config**: Edite `data/config.json` e faça `uploadfs`
- **Reset**: Botão RESET no ESP32 para reiniciar