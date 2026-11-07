# Reproduções da auditoria BPR

Estes testes reproduzem defeitos na versão d341674a599fba150e9048b4d81124472092a3e1.
Eles não são uma suíte de aprovação do firmware e não usam ESP32 físico, Firebase real ou rádio.

## Executar

Com Python 3, git e g++ com suporte a C++17 e AddressSanitizer, na raiz do pacote,
obtenha primeiro as versões públicas exatas:

```bash
git clone https://github.com/Ameciclo/bpr-sistema.git bpr-sistema
git -C bpr-sistema checkout d341674a599fba150e9048b4d81124472092a3e1
git clone --depth 1 --branch v6.21.5 https://github.com/bblanchon/ArduinoJson.git audit-deps/ArduinoJson
```

Depois execute:

```bash
python3 audit-tests/run_checks.py
```

O runner usa os fontes originais de firmware em bpr-sistema/firmware, sem alterações.
ArduinoJson 6.21.5 oficial deve estar em audit-deps/ArduinoJson/src.
As dependências de hardware são substituídas por stubs em audit-tests/stubs.

bici_checks usa todos os fontes originais da bicicleta. central_checks exercita os gerenciadores,
armazenamento e CloudSync originais. ble_checks chama os callbacks originais do servidor.
retry_checks usa o laço principal da central, com ConfigAP substituído por funções vazias,
permitindo verificar a contagem de falhas sem instalar um servidor HTTP.

O adaptador String do host usa std::string, e o filesystem é um mapa em memória com enumeração
por snapshot. Isso não reproduz layout de ponteiros do ESP32, LittleFS real, FreeRTOS ou propagação
BLE. A medição de payload e estrutura de sessão e os erros de estado/schema são verificáveis
independentemente de rádio. O teste de 11 itens verifica o limite lógico de array do carregador;
o ponto exato do crash pode variar com o layout do host.

A compilação native de retry usa -Df_str=c_str, pois o host não usa PROGMEM. O código fonte
original não é editado. Esse alias não altera o fluxo de estados investigado.

Os logs de PlatformIO registram a tentativa de build para ESP32; consulte o relatório para
interpretar seu resultado, versão de plataforma resolvida e limitações.

As observações de NimBLE 1.4.3 relativas a notificações e recriação de serviços foram comparadas
com a implementação oficial dessa versão; os stubs BLE não simulam essas propriedades internas.

O projeto original é licenciado sob AGPL e ArduinoJson sob MIT; consulte as licenças nos clones.
Este pacote contém apenas os testes da análise e logs; não duplica os repositórios originais,
nem contém dados de produção, credenciais reais, binários ESP32 gerados ou o histórico git.
