# BPR Bike Firmware

Firmware para ESP8266/ESP32 instalado nas bicicletas para scanning de redes WiFi.

## Funcionalidades

- 📡 Scanner WiFi automático
- 🔋 Monitoramento de bateria
- 📤 Upload automático para Firebase
- 🌐 Interface web para configuração
- 💾 Armazenamento local de dados

## Hardware Suportado

- NodeMCU ESP8266
- XIAO ESP32-C3
- ESP8266 genérico

## Configuração

1. Edite `data/config.txt` com suas configurações
2. Upload do sistema: `pio run --target uploadfs`
3. Upload do código: `pio run --target upload`

## Documentação Completa

Veja os arquivos de documentação na raiz do projeto para informações detalhadas sobre:
- Configuração inicial
- Monitoramento de bateria
- Estrutura de dados
- Troubleshooting

## Desenvolvimento

```bash
# Compilar
pio run

# Upload
pio run --target upload

# Monitor serial
pio device monitor --baud 115200
```