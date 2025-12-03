# BPR Central Firmware

Firmware para ESP32 instalado nas centrais/bases para coleta de dados das bicicletas.

## Funcionalidades (Planejadas)

- 📡 Ponto de acesso WiFi
- 📥 Coleta de dados das bicicletas
- 🔄 Sincronização com servidor central
- 📊 Agregação de dados locais
- 🌐 Interface web de monitoramento

## Hardware Recomendado

- ESP32 DevKit
- ESP32-S3
- Módulo com mais memória para processamento

## Status

🚧 **Em desenvolvimento** - Este firmware será implementado na próxima fase do projeto.

## Arquitetura Planejada

```
Central ESP32
├── WiFi AP para bicicletas
├── Coleta automática de dados
├── Cache local inteligente
├── Sincronização com Firebase
└── Interface web administrativa
```

## Desenvolvimento

```bash
# Quando implementado
pio run -e central
pio run -e central --target upload
```