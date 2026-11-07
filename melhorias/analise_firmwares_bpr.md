# Análise técnica dos firmwares BPR e da integração web

**Repositório:** Ameciclo/bpr-sistema  
**Versão analisada:** `d341674a599fba150e9048b4d81124472092a3e1`, branch `main`  
**Data da análise:** 5 de novembro de 2026, horário do Recife  
**Escopo:** firmware da bicicleta, firmware da central, protocolo BLE, armazenamento, configuração, sincronização Firebase, dashboard e página de configuração local.

## 1. Parecer

**O conjunto publicado não está funcional de ponta a ponta nem pronto para operação confiável.** Há uma organização inicial por módulos e responsabilidades, mas as implementações dos módulos não cumprem o mesmo contrato. Existem bloqueios determinísticos antes da comunicação, perdas de dados após tentativas de envio e incompatibilidades entre cadastro, configuração, transporte e consumidores dos dados.

A arquitetura geral — bicicleta coleta redes, central recebe por BLE e envia à nuvem — é viável. Os UUIDs BLE e os papéis cliente/servidor correspondem. Isso, porém, não garante a identificação da bicicleta, a recepção de configurações nem a entrega dos registros. Neste código, essas etapas apresentam defeitos independentes.

O dashboard Remix em `web/` ainda é uma estrutura inicial. A interface funcional existente é o portal de configuração Wi-Fi servido pelo próprio ESP32 da central. Ambos foram examinados; não foi encontrada outra branch remota com uma interface completa.

### Alcance e limites da evidência

- Leitura de todos os fontes e headers dos dois firmwares e comparação com configuração de exemplo, interface web e consumidores de dados no backend.
- Execução de testes em C++ usando os **fontes originais**, ArduinoJson 6.21.5 e substitutos locais para ESP32, BLE, Wi-Fi, HTTP e LittleFS. Os substitutos controlam retorno de falha, relógio e armazenamento, sem conectar a serviços reais.
- Teste de corrupção de arquivo com AddressSanitizer no computador.
- Tentativa de compilação PlatformIO descrita na seção 8.
- Nenhum ESP32 físico, circuito de bateria, banco de produção ou página implantada foi acessado. Portanto, alcance de rádio, estabilidade elétrica, corrente e qualidade efetiva dos scans não foram medidos.
- Uma falha de fluxo reproduzida no computador é evidência sobre a lógica C++; não é comprovação de uma conexão BLE física.
- O repositório não recebeu alterações. Os testes e o relatório ficam fora da cópia do projeto.

**Prioridades:** P0 = impede o fluxo básico ou elimina os dados centrais do produto; P1 = defeito grave de integridade, recuperação, configuração ou memória; P2 = problema de operação/manutenção ou comportamento incompleto. Não foi atribuída uma nota numérica arbitrária.

## 2. Funcionamento pretendido e bloqueios reais

| Etapa | Comportamento pretendido | Comportamento encontrado |
|---|---|---|
| Boot da bicicleta | Recuperar configuração e sessão; começar a coleta | O ID é gerado e salvo antes de carregar a configuração; o laço desfaz a saída de BOOT |
| Coleta | Agrupar as redes de cada instante | Uma entrada de scan é criada para cada rede individual |
| Descoberta BLE | Identificar uma central apta a receber | Procura uma substring no nome, aceitando também nomes BUSY/OFF; não verifica disponibilidade |
| Conexão GATT | Obter serviço e características | UUIDs coincidem; falta gestão correta de clientes e de identificação |
| Identificação/status | Enviar ID, tipo e estado da bicicleta | Heartbeat não contém `bike_id` nem `type`; central o rejeita |
| Autorização | Usar cadastro recebido do Firebase | Download escreve `bike_registry.bin`; autorização lê `bike_status.bin` |
| Configuração da bicicleta | Baixar, direcionar, aplicar e confirmar versão | Download usa IDs de exemplo; bike não assina notificações nem lê respostas |
| Upload BLE | Enviar fragmentos identificados, obter confirmação | Envia sessão inteira; ignora retorno e apaga o buffer |
| Armazenamento central | Persistir registros identificados e íntegros | Limite de 128 bytes e caminhos de arquivo incompatíveis com a busca por bike |
| Sincronização | Enviar lotes sem perder histórico | PATCH repetido no mesmo nó substitui o filho `data`; não há histórico por lote |
| Web | Monitorar e editar os mesmos contratos | Dashboard não implementa a integração; portal local tem inconsistências concretas |

As falhas são cumulativas: corrigir apenas o Bluetooth ou um UUID não resolve o sistema.

## 3. Achados críticos

### F01 — P0: bicicleta fica presa na inicialização

**Evidência:** [firmware/bici/src/main.cpp:84](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/src/main.cpp#L84); [firmware/bici/src/main.cpp:126](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/src/main.cpp#L126).

`loop()` copia `currentState` para `nextState`. Quando o estado é BOOT, `handleBoot()` altera diretamente `currentState` para SCANNING. Ao final do mesmo laço, `nextState` ainda é BOOT; a comparação detecta a diferença e escreve BOOT de volta em `currentState`.

No teste com seis chamadas ao laço original: estado final `0`/BOOT, nenhum scan Wi-Fi. A sessão é recomeçada a cada passagem. A decisão de SLEEP feita dentro de `handleBoot()` também pode ser desfeita pelo mesmo mecanismo.

**Correção:** fazer cada handler retornar o próximo estado e atribuir `currentState` em um único local. Não misturar alteração direta de estado com valor de retorno. **Aceite:** sair de BOOT exatamente uma vez, manter sessão e iniciar coleta no prazo definido.

### F02 — P0: heartbeat da bicicleta é rejeitado pela central

**Evidência:** [firmware/bici/src/at_base.cpp:72](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/src/at_base.cpp#L72); [firmware/bici/src/power_manager.cpp:89](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/src/power_manager.cpp#L89); [firmware/central/src/ble_server.cpp:72](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/ble_server.cpp#L72); [firmware/central/src/bike_pairing.cpp:273](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/bike_pairing.cpp#L273).

O payload real contém apenas `battery_percent`, `battery_voltage`, `is_charging` e `heap`. O parser da central exige `bike_id` e depois distingue o tipo `heartbeat`.

Mesmo superado F01, o status não identifica a bicicleta nem atualiza os dados de bateria. A central mantém conexões sem identificação sujeitas a timeout de dez segundos, verificado periodicamente. Se a bicicleta se conecta sem registros acumulados, não há outro pacote que possa identificá-la.

**Correção:** contrato único de heartbeat com `type`, `bike_id`, versão/configuração aplicada e estado dos dados pendentes. Validar campos, tipos e limites nas duas pontas. **Aceite:** conectar com buffer vazio e permanecer identificada, com bateria atualizada.

### F03 — P0: cadastro baixado não autoriza bicicletas

**Evidência:** [firmware/central/src/cloud_sync.cpp:283](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/cloud_sync.cpp#L283); [firmware/central/src/bike_manager.cpp:37](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/bike_manager.cpp#L37); [firmware/central/src/bike_manager.cpp:105](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/bike_manager.cpp#L105); [firmware/central/include/constants.h:13](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/include/constants.h#L13).

O download grava `BikeRegistryData` em `/bike_registry.bin`. `BikeManager` carrega `BikeStatusData` de `/bike_status.bin`, criando uma lista vazia se o arquivo não existir. Não existe uma etapa ativa que converta o cadastro baixado na lista consultada por `canConnect()`.

Reproduzido: arquivo de cadastro com bicicleta `bpr-123456` e status allowed; depois de `BikeManager::init()`, a lista operacional permanece vazia e `canConnect()` retorna falso.

As bikes pending também são recusadas por `canConnect()`, apesar dos caminhos posteriores para registrar visitas pendentes. `addPendingBike()` existe, mas não é chamado no fluxo ativo.

**Correção:** uma fonte de verdade para autorização, separando cadastro remoto de telemetria local. Recarregar ou atualizar o estado operacional após o download. **Aceite:** allowed aceita, pending segue fluxo definido e blocked rejeita, inclusive após reboot e alteração remota.

### F04 — P0: pacote Wi-Fi não cabe no armazenamento da central

**Evidência:** [firmware/bici/src/buffer_manager.cpp:110](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/src/buffer_manager.cpp#L110); [firmware/central/src/buffer_manager.cpp:90](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/buffer_manager.cpp#L90); [firmware/central/src/buffer_manager.cpp:115](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/buffer_manager.cpp#L115); [firmware/central/include/buffer_manager.h:9](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/include/buffer_manager.h#L9).

A bicicleta serializa a sessão completa em JSON. Um teste com uma única rede, SSID `rede`, produziu **134 bytes**. A central acrescenta dois campos de horário e então rejeita qualquer registro com mais de **128 bytes**. Portanto, até o pacote pequeno usado no teste já é incompatível antes do acréscimo de metadados.

Há outros limites desencontrados: característica GATT padrão de 512 bytes na NimBLE 1.4.3, filtro de entrada de 2048 bytes na central, documento de parsing de 1024 bytes e documento de saída de 8192 bytes na bike. Não há protocolo de fragmentação/recomposição. Aumentar apenas o MTU não resolve o tamanho do registro persistido.

**Correção:** definir um tamanho máximo real e usar mensagens fragmentadas com sessão, sequência, contagem, comprimento e checksum; confirmar fragmentos persistidos. Dimensionar os documentos e armazenamento para esse contrato. **Aceite:** transferir uma sessão completa com SSIDs longos e caracteres escapados, sem truncamento nem perda.

### F05 — P0: bicicleta apaga dados mesmo quando a escrita falha

**Evidência:** [firmware/bici/src/at_base.cpp:82](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/src/at_base.cpp#L82); [firmware/bici/src/at_base.cpp:126](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/src/at_base.cpp#L126); [firmware/bici/src/buffer_manager.cpp:103](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/src/buffer_manager.cpp#L103).

`writeValue()` retorna sucesso/falha, mas o resultado é descartado. O handler limpa a sessão imediatamente após tentar enviar. Não aguarda confirmação de persistência da central. O teste forçou a escrita BLE a retornar falso e confirmou que o buffer terminou vazio.

Além disso, `clear()` define `sessionActive = false`. Ao sair da base, nenhum caminho inicia outra sessão; as próximas chamadas a `addScan()` são recusadas. Assim, mesmo um primeiro envio bem-sucedido encerraria a coleta até reiniciar a placa — e o reboot possui F01/F07.

**Correção:** conservar registros até ACK de aplicação vinculado a sessão/fragmento; retirar apenas o que foi confirmado. Criar explicitamente a próxima sessão na saída da base. **Aceite:** desconectar a qualquer ponto do envio e recuperar exatamente os mesmos registros, sem duplicar dados nem interromper a viagem seguinte.

## 4. Demais falhas graves

### F06 — P1: configurações não chegam à bicicleta

**Evidência:** [firmware/bici/src/at_base.cpp:39](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/src/at_base.cpp#L39); [firmware/bici/src/at_base.cpp:90](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/src/at_base.cpp#L90); [firmware/bici/src/at_base.cpp:200](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/src/at_base.cpp#L200); [firmware/central/src/ble_server.cpp:261](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/ble_server.cpp#L261).

A central notifica a característica de configuração, mas a bicicleta nunca chama `subscribe()`, não registra um callback de notificação e não lê a característica. `requestConfig()` e `processHeartbeatResponse()` estão definidos, porém sem chamadores ativos. Este último ainda procura `config_update`, enquanto o envio principal da central usa um objeto de configuração diretamente.

Mesmo se conectado, o dispositivo não aplica as atualizações remotas. `processUpdate()` não lê `dev_mode`, `bike_name`, versão remota, timeout de conexão nem todos os demais campos anunciados. Teste: enviar `dev_mode:false` junto a um campo Wi-Fi altera o campo Wi-Fi, mas deixa dev mode verdadeiro.

**Correção:** assinar antes do handshake, desserializar envelopes versionados, validar e salvar configuração e enviar ACK com a versão realmente aplicada. Tratar explicitamente booleanos falsos e valores zero permitidos.

### F07 — P1: boot sobrescreve a configuração antes de carregá-la

**Evidência:** [firmware/bici/src/main.cpp:63](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/src/main.cpp#L63); [firmware/bici/src/config_manager.cpp:73](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/src/config_manager.cpp#L73).

`generateUniqueId()` chama `save()` incondicionalmente. Ele é executado antes de `load()`, quando os demais campos ainda têm os valores do construtor. Isso grava novamente os padrões e elimina a configuração persistida.

Teste: salvar intervalo de 42 segundos e executar a sequência de boot resulta em intervalo de 300 segundos. O arquivo `data/config.json` também não é lido pelo firmware atual, que só abre `/config.bin`.

**Correção:** carregar e validar primeiro; gerar ID apenas se necessário; preservar identidade de hardware e migrar formato explicitamente.

### F08 — P1: recuperação e salvamento de sessão estão quebrados

**Evidência:** [firmware/bici/src/main.cpp:75](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/src/main.cpp#L75); [firmware/bici/src/main.cpp:46](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/src/main.cpp#L46); [firmware/bici/src/buffer_manager.cpp:59](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/src/buffer_manager.cpp#L59); [firmware/bici/src/buffer_manager.cpp:9](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/src/buffer_manager.cpp#L9).

Há três defeitos independentes:

1. `load()` recupera a sessão, mas `handleBoot()` chama `startSession()`, que apaga a estrutura recuperada com `memset`.
2. `handleSleep()` chama `endSession()` antes de `save()`. `save()` se recusa a gravar sessões inativas. O caminho de dormir não salva os registros.
3. Não há salvamento periódico nem chamada a `save()` depois de coletar scans. Fora uma chamada explícita de teste, os dados da viagem ficam na RAM até serem apagados ou a placa desligar.

Os três efeitos foram reproduzidos ou confirmados pelo conjunto de chamadores. **Correção:** separar existência/fechamento de sessão da possibilidade de persistir; recuperar pendências antes de abrir outra sessão; checkpoint atômico com arquivo temporário, versão e integridade.

### F09 — P1: scans são fragmentados incorretamente e o buffer acaba cedo

**Evidência:** [firmware/bici/src/scanning.cpp:23](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/src/scanning.cpp#L23); [firmware/bici/src/buffer_manager.cpp:29](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/src/buffer_manager.cpp#L29); [firmware/bici/include/constants.h:19](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/include/constants.h#L19).

Dentro do laço de redes, o código cria um array com **uma** rede e chama `addScan()` uma vez por rede. Um scan com três redes resulta em três registros com uma rede cada, como reproduzido. Isso compromete a geolocalização que deveria comparar vários pontos de acesso observados juntos.

O limite é de 50 registros. Com 20 redes em cada varredura, ele se esgota durante a terceira varredura. Com o intervalo padrão de cinco minutos, isso pode ocorrer por volta de 15 minutos após a inicialização, caso não haja entrega. A estrutura reserva espaço para 20 redes em cada registro, mas quase todo esse espaço fica vazio; `sizeof(SessionData)` é 54.324 bytes.

O parâmetro `scan_timeout_ms` é impresso, mas não passado ao scanner nem usado para interrompê-lo. **Correção:** acumular as redes de uma varredura e inserir um único scan; gerenciar fila persistente e backpressure; definir o comportamento ao encher e testar o timeout real.

### F10 — P1: conexão BLE usa handle zero como inválido e perde a origem das mensagens

**Evidência:** [firmware/central/src/ble_server.cpp:72](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/ble_server.cpp#L72); [firmware/central/src/ble_server.cpp:104](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/ble_server.cpp#L104); [firmware/central/src/ble_server.cpp:280](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/ble_server.cpp#L280).

Zero é um handle válido em BLE. O código inicializa buscas com zero e testa `!=0` como sucesso. Uma conexão com handle 0 não é mapeada, não perde seu timeout e não pode receber configuração nem ser desconectada pelo caminho direcionado. O teste confirmou identificação ausente e timeout mantido.

Além disso, o callback de escrita não recebe o descritor da conexão. Ele escolhe o primeiro handle vazio ou já associado àquele ID, sem saber quem escreveu. Com duas bikes, pode associar a mensagem à conexão errada. A NimBLE 1.4.3 fornece um overload de `onWrite` com `ble_gap_conn_desc*`, que deveria ser usado.

Cada escrita identificada ainda dispara novamente o callback de chegada; duas mensagens da mesma bike produziram dois eventos de chegada no teste. **Correção:** usar o handle verdadeiro do callback, um sentinela apropriado/resultado opcional e evento de identificação apenas uma vez por conexão.

### F11 — P1: identidade e entrega direcionada não são garantidas

**Evidência:** [firmware/central/src/ble_server.cpp:246](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/ble_server.cpp#L246); [firmware/central/src/ble_server.cpp:90](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/ble_server.cpp#L90).

`sendConfigToHandle()` recebe um destino, mas chama `pConfigChar->notify()` sem direcionamento. Na NimBLE 1.4.3, essa chamada percorre os assinantes; a resposta pode alcançar todas as bicicletas inscritas. O objeto enviado não inclui necessariamente um destinatário que permita rejeitar uma configuração alheia. Hoje a falta de assinatura da bike já impede a recepção; após corrigi-la, esse segundo defeito aparece.

A autorização confia no `bike_id` declarado em JSON. Não há vínculo criptográfico com o dispositivo, verificação da MAC cadastrada ou configuração de segurança BLE no fluxo. Outro dispositivo pode declarar o ID de uma bike autorizada. O nome da central também é apenas uma substring verificável por qualquer anunciante.

**Correção:** vincular identidade à conexão autenticada; usar mecanismo de entrega por conexão disponível na versão escolhida, ou fila/leitura por cliente, além de envelope com destino. Testar simultaneamente duas bikes com configurações diferentes.

### F12 — P1: servidor e clientes BLE não têm ciclo de vida correto

**Evidência:** [firmware/central/src/bike_pairing.cpp:80](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/bike_pairing.cpp#L80); [firmware/central/src/bike_pairing.cpp:123](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/bike_pairing.cpp#L123); [firmware/central/src/ble_server.cpp:157](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/ble_server.cpp#L157); [firmware/bici/src/at_base.cpp:41](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/src/at_base.cpp#L41).

Ao sair de pairing para sincronizar, a central mantém o servidor e apenas o marca BUSY. Ao voltar, chama `start()` de novo e cria serviço e características novamente. Na biblioteca 1.4.3, `createServer()` reutiliza o servidor existente e `createService()` aceita UUID duplicado criando outra instância. Isso acumula serviços/objetos e torna a descoberta ambígua após sucessivos ciclos.

Na bicicleta, cada tentativa cria um cliente; falhas e desconexões não o reutilizam nem o removem com `deleteClient()`. `ClientCallbacks` está definido, mas nunca instalado. O timeout de conexão configurado é apenas impresso. `createClient()` tampouco é validado antes de dereferenciar o ponteiro.

**Correção:** iniciar a infraestrutura BLE uma vez, manter estados de disponibilidade ou desligar e reconstruir com cleanup completo. Reutilizar clientes, instalar callbacks e tratar exaustão/timeout. A ocorrência e o tempo até falha física exigem teste prolongado; o desequilíbrio de criação/limpeza é visível no código.

### F13 — P1: downloads de configuração são condicionados a bikes que ainda não se identificaram

**Evidência:** [firmware/central/src/cloud_sync.cpp:366](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/cloud_sync.cpp#L366); [firmware/central/src/bike_manager.cpp:537](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/bike_manager.cpp#L537); [firmware/central/src/bike_manager.cpp:593](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/bike_manager.cpp#L593).

No primeiro sync, `BikeManager::init()` ainda não executou e a contagem operacional é vazia; o download de configurações de bikes é pulado. O teste confirmou zero requisições. Quando há contatos recentes, o código tenta apenas `bpr-012345`, `bpr-123456` e `bpr-234567`, IDs de exemplo fixos.

Há mais três problemas: a busca de slot compara `String(id_numérico)` com `bpr-...`, nunca encontrando a configuração existente; o mapa `configChanged` não recebe valor verdadeiro em nenhum caminho; e o timestamp local é global para todas as bikes, podendo fazer a atualização de uma suprimir a de outra.

Teste: 11 atualizações da mesma bike ocupam os 10 slots e não sinalizam configuração pendente. **Correção:** baixar a lista real de autorizadas, indexar pelo ID canônico, usar versão por bicicleta e manter pendência até ACK real.

### F14 — P1: falha de sync é contada repetidamente sem novas tentativas

**Evidência:** [firmware/central/src/main.cpp:344](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/main.cpp#L344); [firmware/central/src/cloud_sync.cpp:42](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/cloud_sync.cpp#L42); [firmware/central/src/sync_monitor.cpp:12](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/sync_monitor.cpp#L12).

Após uma falha, `CloudSync::update()` continua retornando FAILURE. O laço registra a mesma falha novamente em cada passagem. Para tentar de novo, chama `changeState(STATE_CLOUD_SYNC)`, mas essa função retorna imediatamente se esse já é o estado atual. Assim, o handler não é reiniciado.

No teste com timeout Wi-Fi de um segundo: a primeira passagem consome 1.500 ms; as quatro seguintes consomem zero tempo simulado de conexão; a quinta coloca a central em TEMP_CONFIG_AP. Não são cinco tentativas independentes nem o backoff de 30 segundos anunciado.

**Correção:** contabilizar o resultado uma vez por tentativa, ter estado/agenda de retry e reiniciar a operação explicitamente. **Aceite:** queda e retorno do roteador sem reconfiguração manual, com número verificável de tentativas e intervalos reais.

### F15 — P1: arquivos do buffer não preservam a associação usada pelos leitores

**Evidência:** [firmware/central/src/buffer_manager.cpp:57](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/buffer_manager.cpp#L57); [firmware/central/src/buffer_manager.cpp:267](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/buffer_manager.cpp#L267); [firmware/central/src/buffer_manager.cpp:153](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/buffer_manager.cpp#L153).

O gravador cria `/buffer/scan001.bin`, `/buffer/scan002.bin` etc., sem usar o `bikeId` recebido. O carregador procura um arquivo cujo nome contenha o ID da bicicleta. Assim, a próxima adição daquela bike não recupera seu buffer anterior e o limite de dez itens por bike não atua como projetado.

Durante o preparo de upload, os nomes dos arquivos passam a ser tratados como IDs; o salvamento dos flags cria **novos** arquivos sequenciais. No teste, dois arquivos viraram quatro apenas preparando o upload. A interação com enumeração real de diretórios ainda precisa ser testada: a criação durante a iteração pode introduzir repetição adicional. Não se afirma que a iteração será infinita em toda versão de LittleFS.

**Correção:** identidade dentro de um índice persistente ou arquivos imutáveis por lote/sessão; não inferir bike a partir de nomes sem esse campo. Preparar upload não deve duplicar registros.

### F16 — P1: central confirma recebimento mesmo se a persistência falhar

**Evidência:** [firmware/central/src/buffer_manager.cpp:148](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/buffer_manager.cpp#L148); [firmware/central/src/bike_pairing.cpp:401](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/bike_pairing.cpp#L401); [firmware/central/src/bike_manager.cpp:443](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/bike_manager.cpp#L443).

`addData()` ignora o retorno de `saveBikeBuffer()` e retorna true. A camada de pairing usa isso para emitir `can_clear_buffer:true`. No teste com gravação de arquivos deliberadamente impedida, a adição ainda retornou sucesso.

Gravações de headers e itens também não verificam quantos bytes foram persistidos. **Correção:** propagar erros e só confirmar após armazenamento íntegro; atomicidade e estratégia clara para queda de energia. A bike pode apagar após persistência local confiável se essa for a política, sem precisar esperar nuvem, mas esse compromisso precisa ser cumprido.

### F17 — P1: validação de arquivos permite acesso fora dos limites de memória

**Evidência:** [firmware/central/include/buffer_manager.h:16](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/include/buffer_manager.h#L16); [firmware/central/src/buffer_manager.cpp:299](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/buffer_manager.cpp#L299); [firmware/central/src/buffer_manager.cpp:305](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/buffer_manager.cpp#L305).

O buffer em RAM tem 10 itens, mas o carregador aceita `item_count` até `MAX_BUFFER_SIZE`, definido como 8000. Um arquivo com 11 itens passa na verificação e escreve além do array. O teste com esse arquivo terminou em acesso inválido de memória sob AddressSanitizer, dentro de `loadBikeBuffer()`.

O tamanho armazenado é copiado para `size` sem exigir `<=128`; apenas o `memcpy` da leitura é limitado. Laços posteriores usam o tamanho não validado. O CRC é calculado ao gravar, mas não é verificado ao carregar; a versão do header também não é exigida. Os arquivos de status/configuração não validam adequadamente contagens e strings.

**Correção:** limites derivados dos arrays reais, validação antes de acessar, arquivo com comprimento exato, versão/magic/CRC e strings terminadas. Rejeitar arquivo danificado sem apagar as outras pendências.

### F18 — P1: estouro de documentos JSON pode ser confundido com upload completo

**Evidência:** [firmware/bici/src/buffer_manager.cpp:115](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/src/buffer_manager.cpp#L115); [firmware/central/src/cloud_sync.cpp:398](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/cloud_sync.cpp#L398); [firmware/central/src/buffer_manager.cpp:219](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/buffer_manager.cpp#L219).

Os documentos possuem capacidade fixa e recebem quantidade variável de scans/itens. Não há verificação de `overflowed()` antes do envio. A central marca **todos** os buffers como confirmados quando recebe HTTP 200, sem ligar a confirmação ao subconjunto que efetivamente coube no JSON.

Isso permite descartar registros que não foram incluídos no lote. A dimensão exata do limite de capacidade varia entre host e ESP32; o problema de não conferir inclusão/overflow independe dessa diferença.

**Correção:** formar lotes por bytes medidos e IDs; confirmar e remover exclusivamente os itens daquele lote. Testar lotes maiores que a capacidade e respostas parciais/falhas.

### F19 — P1: upload não constrói histórico compatível com o restante do projeto

**Evidência:** [firmware/central/src/endpoints.cpp:51](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/endpoints.cpp#L51); [firmware/central/src/cloud_sync.cpp:416](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/cloud_sync.cpp#L416); [bot/src/config/firebase.js:49](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/bot/src/config/firebase.js#L49); [bot/functions/src/index.ts:161](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/bot/functions/src/index.ts#L161).

O upload faz PATCH em `/bases/{base}/data.json` com campos fixos como `data`, `data_count` e `timestamp`. O filho `data` é substituído a cada lote, em vez de ser anexado ao histórico por chave estável. Isso é a semântica documentada do PATCH do Realtime Database.

O bot/Functions observa `/bikes/{bikeId}/sessions/...`. Não há no código examinado um consumidor ativo que normalize os uploads de `/bases/.../data` para essas sessões. O script `decode_hub_data.js` apenas decodifica três exemplos literais; não é um pipeline de ingestão.

O formato do scan também diverge: a bike publica `[millis, redes]`, enquanto partes do consumidor esperam `[timestamp, realTime, redes]`. Mesmo que o painel web fosse implementado em cima do contrato descrito no README, os dados não apareceriam automaticamente como prometido.

**Correção:** lotes imutáveis/idempotentes, ingestão explícita e sessões canônicas. Usar chaves de evento estáveis para que retries não dupliquem histórico.

### F20 — P1: tempo de coleta, tempo de configuração e tempo da nuvem são confundidos

**Evidência:** [firmware/bici/src/buffer_manager.cpp:36](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/src/buffer_manager.cpp#L36); [firmware/central/src/config_manager.cpp:137](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/config_manager.cpp#L137); [firmware/central/src/cloud_sync.cpp:312](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/cloud_sync.cpp#L312); [firmware/central/src/time_sync.cpp:8](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/time_sync.cpp#L8).

Os scans usam `millis()` da bike, que reinicia em cada boot. Não há handshake ativo que estabeleça a relação entre uptime da bike e horário UTC da coleta. A central acrescenta horário de **recepção**, que pode ocorrer muito depois da viagem.

A configuração central armazena uptime como `last_update`, mas o verificador remoto compara números que deveriam representar uma versão/instante comum. O cadastro grava a hora local do download em vez da versão remota. Isso pode provocar downloads repetidos ou mascarar atualizações. Campos de exemplo usam também datas humanas, que não são o inteiro esperado por `toInt()`.

O retorno de `TimeSync::init()` é ignorado; há usos de `getLocalTime()` sem conferir sucesso antes de formatar o `tm`. Um reboot de central que entra diretamente em pairing pode operar antes de sincronizar o relógio.

**Correção:** timestamps UTC com validade explícita; registrar boot/session ID, uptime de coleta e âncora de relógio; versionamento remoto monotônico separado de `updated_at`. Não fabricar data válida quando NTP falha.

### F21 — P1: autenticação Firebase usa o tipo de credencial errado na configuração oferecida

**Evidência:** [firmware/central/src/endpoints.cpp:6](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/endpoints.cpp#L6); [config_credentials.example.json:6](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/config_credentials.example.json#L6); [firmware/central/src/config_ap.cpp:148](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/config_ap.cpp#L148).

A UI e o exemplo oferecem uma Firebase Web API Key (`AIza...`), e o firmware a coloca em `?auth=` nas requisições ao Realtime Database. Esse parâmetro espera um token de autenticação apropriado; a API key pública do projeto não substitui login/token. Não existe fluxo ativo de login e renovação.

**Condição:** se o campo estiver sendo preenchido com um secret legado compatível ou outra credencial, o comportamento é diferente; o nome/exemplo/interface estão errados de qualquer forma. As credenciais reais e as regras implantadas não foram acessadas.

O arquivo `bot/database.rules.json` torna `/bikes` público para leitura e escrita, mas não concede acesso a `/bases`. Se implantado exatamente assim, não fornece a permissão de cliente esperada pelos endpoints da central. Não é evidência das regras que estão em produção.

**Correção:** identidade por dispositivo e tokens renováveis, ou serviço de ingestão apropriado. Fazer autenticação e autorização explícitas sem abrir o banco para contornar erros.

### F22 — P1: interface local JSON não salva a chave apresentada pelo próprio exemplo

**Evidência:** [firmware/central/src/config_ap.cpp:152](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/config_ap.cpp#L152); [firmware/central/src/config_ap.cpp:271](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/config_ap.cpp#L271); [config_credentials.example.json:6](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/config_credentials.example.json#L6).

O formato plano de exemplo possui `firebase_api_key`. O handler JSON lê os campos planos de Wi-Fi e URL, mas ignora a chave nesse nível; só lê `firebase.api_key` no formato aninhado. Reproduzido com o handler HTTP original em teste local: o JSON plano recebeu HTTP 200, mas a chave ficou vazia e `isCredentialsValid()` retornou false. `saveCredentials()` verifica gravação, não validade operacional, e o handler não chama `isCredentialsValid()` antes de confirmar.

`currentJson` é construído, mas não inserido na textarea. Portanto, a seção JSON não é efetivamente pré-preenchida apesar do código que monta o conteúdo. **Correção:** um schema único para importação/exportação, validação completa e sucesso apenas após aplicação íntegra. Testar copiar/colar o exemplo em uma central nova.

### F23 — P1: portal local pode corromper memória e expõe credenciais

**Evidência:** [firmware/central/src/config_ap.cpp:187](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/config_ap.cpp#L187); [firmware/central/include/config_credentials.h:5](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/include/config_credentials.h#L5); [firmware/central/src/config_ap.cpp:196](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/src/config_ap.cpp#L196).

Entradas HTTP são copiadas com `strcpy()` para arrays fixos, sem limite; por exemplo, base ID tem 32 bytes. Não há restrição equivalente no backend. Entradas longas sobrescrevem campos vizinhos e podem derrubar a central.

O portal não autentica a operação além do acesso ao AP de senha fixa pública no código. Senha Wi-Fi e chave são impressas integralmente no serial e pré-preenchidas no HTML; valores HTML não são escapados. Não é necessário supor um atacante remoto na internet para reconhecer o defeito de memória: um formulário mal preenchido já pode ativá-lo.

**Correção:** validar comprimentos e caracteres no servidor, copiar com tamanho, escapar HTML, remover logs de segredos e proteger o provisionamento por mecanismo físico/temporário e credencial individual. Não alegar comprometimento de produção sem evidência.

### F24 — P1: energia e bateria não implementam o comportamento anunciado

**Evidência:** [firmware/bici/src/power_manager.cpp:26](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/src/power_manager.cpp#L26); [firmware/bici/src/power_manager.cpp:12](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/src/power_manager.cpp#L12); [firmware/bici/src/config_manager.cpp:10](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/src/config_manager.cpp#L10); [firmware/bici/src/main.cpp:88](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/src/main.cpp#L88).

A leitura inicial do membro `lastVoltage` não é inicializada no construtor. Na instância global ele começa zerado por inicialização estática, e é devolvido nos primeiros cinco segundos; instâncias não estáticas não têm essa garantia. A rotina transforma qualquer ADC abaixo de 1500 em 4 V, mascarando leitura baixa, pino desconectado ou determinados estados de bateria. Acima de 3,9 V assume USB e 100%, sem medir presença de alimentação ou sinal do carregador.

A configuração padrão ativa dev mode e o atualizador ignora `dev_mode:false`, de modo que a proteção por tensão não pode ser habilitada pelo contrato atual. Além disso, a decisão de bateria em `nextState` é sobrescrita pelos handlers SCANNING e AT_BASE, ou pela busca de central após o handler de scanning.

O projeto compila para XIAO ESP32C3 e lê A0 como se houvesse divisor por dois. A documentação oficial da Seeed informa que a placa não disponibiliza automaticamente a tensão da bateria nesse pino: é necessário circuito externo. Sem conhecer o circuito instalado, o valor não pode ser considerado medição válida.

A bike faz busca BLE bloqueante a cada passagem em SCANNING, com cinco segundos por busca no padrão; Wi-Fi é iniciado e não há duty cycle implementado no funcionamento normal. O delay de coordenação entre rádios não equivale a desligá-los. Deep sleep depende dos caminhos defeituosos acima; não é possível estimar autonomia confiável por esse código.

**Correção:** confirmar esquema elétrico; calibrar ADC/divisor; usar sinal válido de carregamento ou estado desconhecido; inicializar cache; centralizar prioridade de estados de energia; aplicar dev mode e programar busca BLE por agenda. Medir corrente em bancada.

### F25 — P1: build atual da central excede a partição de aplicação

**Evidência:** build PlatformIO dos fontes originais, com o `platformio.ini` publicado e dependências resolvidas em 5/11/2026; [firmware/central/platformio.ini:2](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/platformio.ini#L2); [firmware/central/partitions.csv:4](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/central/partitions.csv#L4).

O código da central foi compilado e linkado, mas a etapa `checkprogsize` falhou: **1.821.119 bytes** para uma partição de aplicação de **1.310.720 bytes** (138,9%). O build terminou com status 1. A configuração não seleciona a tabela `partitions.csv` presente na pasta. Essa tabela alternativa, por sua vez, descreve um layout de 8 MiB, enquanto o alvo XIAO selecionado declara 4 MiB.

O projeto usa `platform = espressif32` sem fixar versão. O resultado acima é do ambiente atual, com plataforma 8.0.0 e Arduino core 3.3.12; não comprova que o mesmo código jamais coube com uma versão anterior. O firmware da bicicleta completou o build no mesmo ambiente.

**Correção:** fixar um conjunto validado de ferramentas e bibliotecas; escolher uma tabela que caiba na flash física e na qual o binário caiba, considerando LittleFS e a estratégia de OTA; reduzir tamanho se necessário. Não gravar a tabela de 8 MiB em uma placa de 4 MiB como solução improvisada. **Aceite:** build com checagem de tamanho aprovada e inicialização/persistência verificadas na placa correta.

## 5. Incompatibilidades com a interface web e a configuração

### Dashboard Remix

[web/app/root.jsx:26](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/web/app/root.jsx#L26) contém um título, `Outlet`, metadados e scripts. Não há rotas de dashboard, serviço Firebase, formulário de gestão, autenticação ou leitura de dados. O `web/package.json` também não fornece um SDK Firebase.

**Conclusão:** não existe integração funcional nesse dashboard para validar como correta. Se o sistema utilizado tem páginas de bikes/centrais e edição de parâmetros, essa implementação está fora da versão pública analisada.

### Contrato de dados/configuração

| Assunto | Exemplo/descrição disponível | Firmware ativo | Efeito |
|---|---|---|---|
| Configuração central | Objeto em `/bases/{base}/configs` | Array posicional de 32 valores em `/bases/{base}/config` | UI baseada no exemplo não configura o firmware |
| Versão central | `configs/last_update`, inclusive exemplo em texto | Consulta esse caminho, mas persiste uptime | Comparação sem unidade/versão consistente |
| Cadastro de bikes | `/bases/{base}/bikes/{id}` com status textual | Array plano em `/bases/{base}/bike_registry`, status numérico | Não há conversor ativo compatível |
| Registro local de acesso | Cadastro remoto esperado | `/bike_status.bin`, separado do download | Allow/block remoto não aplicado |
| Configuração por bike | Objeto global `/bike_configs/{id}` | Array de sete valores em `/bases/{base}/bike_configs/{id}` | Campos e caminhos divergem |
| Dados coletados | `/bikes/{id}/sessions` no bot e documentação web | `/bases/{base}/data`, com envelopes de lote | Telas/consumidores esperados não recebem sessões |
| Scan | Consumidores usam índice 2 para redes | Bike usa índice 1 | Redes não são lidas pelo contrato anterior |
| Heartbeat central | Alguns exemplos incluem horário humano | Upload real envia Unix timestamp, contagem, heap, uptime | Menor divergência; documentar o objeto real |
| Dev mode | Campo booleano editável | Central transmite, bike ignora | Mudança não é aplicada |
| Nome e intervalo BLE | Configuração descritiva | Intervalo de scan lido; filtro é substring do nome | Identificação e disponibilidade precisam de protocolo |

A solução deve escolher **um** contrato versionado e gerar/adaptar interfaces a partir dele. Manter simultaneamente objetos JSON, arrays posicionais, CSV e structs binárias sem testes de contrato perpetua as incompatibilidades.

## 6. Outros pontos de qualidade e operação

1. **IDs truncados e formatos concorrentes.** [firmware/bici/src/utils.cpp:11](https://github.com/Ameciclo/bpr-sistema/blob/d341674a599fba150e9048b4d81124472092a3e1/firmware/bici/src/utils.cpp#L11) usa decimal `%06u` e buffer de 12 bytes; valores de 24 bits acima de 9.999.999 excedem o espaço. Testes com 10.000.000 e 10.000.001 geraram o mesmo `bpr-1000000`. O helper comum usa seis dígitos hexadecimais, mas a central faz `toInt()` decimal e reconstitui com `%06u`. Não se deve afirmar que a bike atual sempre emite hex: ela usa o helper decimal local. Deve-se padronizar um formato e testar todo o espaço de IDs relevante, com migração dos existentes.
2. **Buffer “dinâmico” não é a capacidade efetiva.** `beginWithAvailableHeap()` calcula `maxCapacity`, mas não aloca uma estrutura que use esse tamanho; `currentUsage` não é incrementado por `addData()`. O armazenamento real continua limitado por itens fixos. `isFull()` usa 80% de 8000 itens, sem relação com limite de dez itens por buffer ou espaço da flash. O limiar remoto `config.buffer` não dirige esse fluxo.
3. **Configuração perigosa aceita.** `isConfigValid()` da central valida apenas três campos. O teste aceitou durações de LED iguais a zero, que depois entram em operações de módulo/divisão. Há ainda valores convertidos para uint8/uint16 sem faixa, configurações truncadas aceitas e vários parâmetros anunciados que não são consumidos.
4. **BUSY pode expirar antes de a sincronização acabar.** Pairing anuncia 60 segundos, mas Wi-Fi pode esperar 60 segundos, NTP outros dez e há várias chamadas HTTP. Os callbacks BLE continuam ativos; `isCentralBusy()` pode liberar entrada durante o sync. As filas, mapas e arquivos são usados entre callbacks BLE e o laço sem coordenação explícita. Há risco de concorrência e confirmação de um conjunto diferente daquele enviado. O crash por corrida precisa de teste específico; a falta de sincronização é visível.
5. **Modo AP não desliga efetivamente BLE existente.** As mensagens afirmam que BLE está desabilitado, mas `ConfigAP::enter()` não o para. Depois de pairing/sync, um servidor existente pode continuar ativo. No primeiro boot ele pode não ter sido iniciado, situação distinta.
6. **Contagem de conectadas não é a conexão real.** `BikeManager::getConnectedCount()` usa último contato em menos de 120 segundos; `BPRBLEServer` mantém contagem física separada. `updateHeartbeat()` modifica RAM sem salvar; `BikeManager::init()` recarrega arquivo ao entrar em pairing e pode perder contatos recentes. O monitoramento e a decisão de baixar configurações ficam sujeitos a esse estado.
7. **Estados e mecanismos sem fluxo ativo.** Não há transição para LOST no fluxo normal. O handler LOST, se acionado, considera encontrar o anúncio suficiente para entrar em AT_BASE, sem conectar. `UploadQueue` não é chamado pelo fluxo principal; as funções de retry apenas alteram contadores, não reenviam dados. Não contar esses módulos como funcionalidades prontas.
8. **Configuração binária sem migração robusta.** Gravar o layout bruto de structs facilita operações pequenas, mas falta header/versionamento/CRC/validação atômica em vários arquivos. Mudanças de layout e gravações interrompidas não são tratadas como um fluxo de migração recuperável. Montagem com autoformatação pode apagar pendências em caso de falha de filesystem.
9. **Documentação e alvos divergentes.** Os dois `platformio.ini` selecionam `seeed_xiao_esp32c3`, embora partes do README mencionem WROOM. Existe uma tabela `partitions.csv` que chega a 8 MiB, mas ela não é selecionada no ini; o alvo XIAO normalmente tem 4 MiB. Não afirmar que essa tabela está ativa: o problema é o arquivo incompatível deixado como referência. Confirmar modelo físico e partições antes de gravar firmware.
10. **LED não é prova de saúde.** O self-check não testa efetivamente rádio Wi-Fi ou BLE: os testes estão comentados. Escrever em GPIO e imprimir sucesso também não comprova LED físico ou comunicação. A XIAO ESP32C3 não possui um LED de usuário embarcado equivalente ao assumido em alguns comentários.

## 7. Avaliação da programação

**Aspectos aproveitáveis:** divisão entre coleta, configuração, armazenamento, energia e comunicação; definição explícita de estados; utilização de LittleFS e presença de conceitos de CRC, ACK e priorização; intenção de desacoplar coleta offline da nuvem. Os três UUIDs coincidem e bike cliente/central servidor é uma combinação coerente.

**Fragilidade principal:** mecanismos são declarados em arquivos separados, mas não estão conectados por invariantes e contratos testados. Exemplos claros são callback nunca instalado, função nunca chamada, dois arquivos de cadastro sem conversão, buffer com tamanhos diferentes em cada camada e configuração marcada como enviada sem recepção.

O resultado é código que pode imprimir mensagens de sucesso sem cumprir a operação: “enviado” sem conferir escrita, “salvo” sem propagar falha, “configuração pronta” sem validação de credencial e “central BLE desabilitada” sem desligar o servidor.

A leitura permite julgar a implementação como **protótipo em integração, com regressões graves**, e não como firmware consolidado. Não há base para atribuir autoria ou causas a partir do estilo do código ou da mensagem do commit.

## 8. Verificações executadas

### Resultados de execução nativa

Os testes exercitam os fontes C++ reais. Simulam as interfaces externas, não o rádio, a flash física nem o escalonamento FreeRTOS. O ArduinoJson é a versão 6.21.5 oficial. O host usa strings e tamanhos de ponteiro diferentes do ESP32; os resultados de estado, retorno, schema e limites lógicos aqui descritos não dependem de supor o mesmo consumo de RAM.

Para compilar o fluxo central no host, o identificador `f_str` é mapeado para `c_str`, pois o host não habilita a camada PROGMEM do Arduino. Esse ajuste é do ambiente de teste, não do repositório nem de sua lógica.

| Teste | Resultado observado |
|---|---|
| Seis chamadas ao laço da bike após setup | BOOT; zero scans |
| Gravar intervalo 42 s e executar sequência de boot | Intervalo vira 300 s |
| Uma rede em sessão serializada | Payload de 134 bytes |
| Encerrar sessão e depois salvar | Arquivo não é criado |
| Carregar sessão e chamar startSession | Registros recuperados são apagados |
| clear e depois addScan | Não coleta: sessão inativa |
| Heartbeat | Sem `bike_id` e sem `type` |
| Configuração `dev_mode:false` | Permanece true |
| Um scan Wi-Fi com três redes | Três registros, uma rede em cada |
| Escrita BLE retorna false | Buffer termina vazio |
| Dois chip IDs 10.000.000 e 10.000.001 | Mesmo ID textual truncado |
| Cadastro remoto allowed em bike_registry.bin | Autorização retorna false |
| Atualizar a mesma bike 11 vezes | Dez slots usados, nenhuma flag pendente |
| LEDs com duração zero | Configuração aceita |
| Preparar upload de dois arquivos | Quatro arquivos após preparação |
| Falha simulada ao gravar flash na central | addData retorna sucesso |
| Registro de 134 bytes na central | Rejeitado |
| Primeiro sync sem cadastro em RAM | Zero downloads de configuração por bike |
| Handle de conexão zero | Não identificado; timeout mantido |
| Duas escritas da mesma bike | Dois eventos de chegada |
| Uma falha Wi-Fi seguida de quatro laços | Cinco falhas contadas; entra em AP |
| Arquivo binário com 11 itens | Acesso inválido de memória sob ASan |
| Portal recebe JSON plano de exemplo | HTTP 200, chave não salva e credenciais inválidas |

### Compilação para ESP32

Os dois projetos foram executados com seus arquivos `platformio.ini` originais. A primeira execução foi interrompida por bloqueio de envio de telemetria do PlatformIO; o build foi retomado de forma mais restrita com **telemetria explicitamente desativada**. Não foi necessário liberar esse envio para concluir as verificações. Downloads de dependências tiveram retries por checksum/mirror, mas chegaram à etapa de compilação.

| Item | Bicicleta | Central |
|---|---|---|
| Alvo | seeed_xiao_esp32c3 | seeed_xiao_esp32c3 |
| Plataforma | espressif32 8.0.0 | espressif32 8.0.0 |
| Arduino core | 3.3.12 | 3.3.12 |
| NimBLE-Arduino | 1.4.3 | 1.4.3 |
| ArduinoJson | 6.21.6 | 6.21.6 |
| NTPClient / CRC32 | — | 3.2.1 / 2.0.1 |
| RAM estática indicada | 96.572 / 327.680 bytes (29,5%) | 45.684 / 327.680 bytes (13,9%) |
| Aplicação em flash | 1.214.597 / 1.310.720 bytes (92,7%) | 1.821.119 / 1.310.720 bytes (138,9%) |
| Resultado | Build concluído, firmware.bin criado | Falhou na checagem de tamanho da aplicação |

RAM estática não é pico de heap/stack em operação. O alto uso da partição na bike também deixa pouca margem para crescimento. Foram emitidos warnings de truncamento do ID, formatos de `printf` e redefinição de macros BLE. Na central, há redefinição de `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` para 3 pelo sdkconfig do core, conflitante com o `-D...=10` do projeto: não se deve tomar a capacidade de dez conexões como validada por essa flag. É necessário conferir a configuração efetiva da biblioteca/core e testar concorrência.

Os logs finais usados como evidência estão no pacote: `build-bici-no-telemetry.log` e `build-central-no-telemetry.log`. Os testes nativos foram executados com ArduinoJson 6.21.5, explicitamente separada da versão 6.21.6 resolvida pelo build ESP32.

A compilação bem-sucedida da bicicleta não refuta os defeitos de estado e contrato reproduzidos. Ela garante tradução/link, não a entrega de dados.

## 9. Sequência recomendada de correção

Não recomendo ajustar dezenas de detalhes diretamente nas placas sem primeiro estabelecer um caminho mínimo verificável.

1. **Fixar a base:** modelo exato das placas, versão PlatformIO/plataforma/core/NimBLE e configuração de partições; build de ambos em CI; schema canônico de identificação, tempo, configuração e dados.
2. **Fazer uma bike funcionar:** corrigir BOOT e preservação de config/sessão; produzir um scan agrupado; identificar a bike com heartbeat; central aplicar o cadastro real.
3. **Garantir entrega:** sessão e fragmentos identificados, persistência válida na central, ACK após persistência, retry idempotente e retomada após reboot; só então liberar limpeza da bike.
4. **Concluir configuração:** obter configs para as bikes reais, notificar o destinatário correto, aplicar todos os campos aceitos e confirmar a versão; nunca marcar sucesso apenas por ter tentado notificar.
5. **Concluir nuvem e web:** autenticação apropriada, histórico imutável e normalização para as telas; corrigir portal JSON e validar entradas. Implementar dashboard a partir do contrato estável.
6. **Qualificar operação:** bateria/corrente, várias bicicletas simultâneas, sinais fracos, roteador fora do ar, flash cheia e teste contínuo de vários dias.

### Contrato mínimo sugerido

Sem impor uma troca completa de arquitetura, cada mensagem deve declarar `protocol_version`, `type`, identidade e correlação. Dados precisam de `session_id`, `boot_id`, sequência e integridade. ACK deve indicar exatamente o fragmento persistido. Configuração precisa de versão remota e confirmação da versão aplicada.

Distinguir os estados “detectou anúncio”, “GATT conectado”, “identidade autorizada”, “dados persistidos na central” e “sincronizado na nuvem”. Um indicador genérico de conectado não substitui esses marcos.

O armazenamento binário é viável se tiver cabeçalho versionado e validação completa. JSON também é viável em mensagens pequenas e dimensionadas. O problema atual não exige adotar outra tecnologia; exige um único contrato e implementação coerente de ponta a ponta.

## 10. Plano de validação nas placas

| Cenário | Critério de aceite |
|---|---|
| Boot frio e reinício | Recupera config e pendências; entra em coleta uma vez |
| Primeira conexão sem scans | Heartbeat identifica e autoriza; não cai por timeout |
| Uma e várias redes Wi-Fi | Cada instante preserva o conjunto correto de BSSIDs/RSSI |
| Uma e dez bikes | Handle/ID e resposta sempre correspondem ao emissor |
| Conexão com handle zero | Tem o mesmo comportamento de qualquer outro handle |
| Configuração A e B para bikes diferentes | Cada uma aplica somente sua versão |
| Central BUSY por mais de 60 s | Bike mantém dados e respeita retry; nenhuma recepção indevida |
| Desconectar no meio do envio | Retoma sem perder ou duplicar fragmentos |
| Reiniciar a bike/central após ACK | Dados continuam recuperáveis na central |
| Cortar energia durante gravação | Recupera última versão íntegra ou pendência verificável |
| Wi-Fi/Internet/Firebase indisponíveis | Continua coletando conforme capacidade, sem falso sucesso |
| Retorno de rede | Sync recupera sem abrir AP por contagem repetida |
| Flash cheia/arquivo corrompido | Erro controlado; sem ACK falso, escrita fora do array ou perda geral |
| Lote acima do documento JSON | Divisão correta; somente itens enviados são removidos |
| Duas viagens seguidas | Segunda viagem inicia sessão e coleta normalmente |
| Horário ausente e reboot | Não apresenta uptime como data Unix real |
| Bateria alta, baixa, crítica e USB | Leituras comparadas com multímetro; estados e corrente medidos |
| Endurance por vários dias | Heap, stack, quantidade de serviços/clientes e filas permanecem limitados |
| Interface e Firebase | Alteração remota chega à bike; telemetria aparece no painel com versão e horário |

## Referências externas utilizadas

- [Firebase: autenticação de requisições REST](https://firebase.google.com/docs/database/rest/auth).
- [Firebase: gravação e semântica de PATCH/POST](https://firebase.google.com/docs/database/rest/save-data).
- [NimBLE-Arduino 1.4.3: implementação de característica, callbacks e notificações](https://github.com/h2zero/NimBLE-Arduino/blob/1.4.3/src/NimBLECharacteristic.cpp).
- [NimBLE-Arduino 1.4.3: servidor e serviços](https://github.com/h2zero/NimBLE-Arduino/blob/1.4.3/src/NimBLEServer.cpp).
- [NimBLE-Arduino 1.4.3: ciclo de vida de dispositivo/servidor](https://github.com/h2zero/NimBLE-Arduino/blob/1.4.3/src/NimBLEDevice.cpp).
- [Seeed Studio: XIAO ESP32C3 e medição de bateria](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/).

Os links de código nos achados apontam para o commit analisado, permitindo comparar cada conclusão com a implementação exata. As referências externas esclarecem a semântica das bibliotecas; não substituem teste no hardware.
