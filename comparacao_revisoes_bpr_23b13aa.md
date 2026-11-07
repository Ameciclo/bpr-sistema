# BPR — Comparação das duas revisões técnicas

**Data:** 7 de novembro de 2026, America/Recife.  
**Fonte conferida:** [Ameciclo/bpr-sistema, commit 23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2](https://github.com/Ameciclo/bpr-sistema/tree/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2), branch `fixes/november`.  
**Texto comparado:** anexo `Texto colado(8).txt`, 17.230 bytes, SHA-256 `6ca5d81ed42a7978480b39467cdff7498827b9ca41d74990a7c342edba7bb1c6`. O texto não informa o SHA analisado por sua autora. As conclusões desta comparação valem para o SHA acima.  
**Referência anterior:** [revisao_bpr_fixes_november.md, revisão 2](https://chatgpt.com/library?file_id=libfile_f06cbc8b1e24819198e51c1d48a31e3a&account_id=personal) e [PRD de confiabilidade, proposta 1.1](https://chatgpt.com/library?file_id=libfile_c1f2740853c48191a64413e3a22ccbe6&account_id=personal).  
**Entrega:** análise e requisitos em Markdown. Nenhum fonte do aplicativo foi alterado; nenhum commit, deploy, envio de mensagem ou teste em placa foi feito.

## 1. Parecer

**A outra análise complementa a revisão anterior com problemas reais, sobretudo na leitura de bateria, na duração de AT_BASE, na liberação da bike em processamento e nas descobertas feitas sem NTP. Também contém afirmações incorretas ou mais fortes que a evidência disponível.**

O bloqueio de pilha é o mesmo N01 já demonstrado no ELF. A não atomicidade, a corrupção do spool, a recuperação temporal e o limite de fragmentos também coincidem com achados anteriores. A contagem de cinquenta scans não é o comportamento do produtor atual: ele para em dez por N02, ausente no texto comparado.

A conferência adicional de memória revelou outro bloqueio de entrega: o documento JSON da bike tem capacidade de 652 bytes, enquanto o fragmento final com dez leituras de bateria exige pelo menos 656 bytes só em slots no ESP32-C3. Chunks com seis redes exigem ao menos 688 bytes. Esse é um defeito concreto de orçamento, relacionado à preocupação C6, mas com uma causa diferente da hipótese de cabeçalho maior que 512 bytes.

Não se deve importar os doze itens “críticos” como doze crashes comprovados. Alguns são condicionados à configuração, ao MTU ou ao escalonamento; outros são lacunas de implementação de um comportamento descrito apenas nos documentos. Todos precisam de critérios de aceite proporcionais à sua evidência.

### Evidência utilizada

- **E1:** saída do compilador, tipo no ELF ou implementação da biblioteca efetivamente instalada.
- **E1-S:** métodos dos fontes reais executados com os adaptadores de host já existentes no projeto. Não representa ADC, flash ou rádio físicos.
- **E2:** análise de fluxo e de orçamento, sem execução do cenário completo nesta comparação.
- Foram reutilizados os builds, testes e binários identificados na revisão 2. Não foi necessário repetir os builds sem mudança de fontes.
- Nesta rodada foram executados novos experimentos de bateria e configuração com a biblioteca nativa produzida anteriormente a partir dos fontes e verificadores fornecidos. Nenhum novo fonte C++ foi criado.
- Foram conferidos o core Arduino-ESP32 3.3.12, o SDK obtido pelo build, NimBLE 1.4.3 e ArduinoJson 6.21.6. As observações sobre defaults dessas dependências não são generalizações para qualquer versão.

## 2. Conferência dos doze itens críticos do anexo

| Item | Resultado da comparação | O que permanece como achado |
|---|---|---|
| C1 — sessão na pilha | Confirmado, já N01 | A estrutura tem 54.328 B; o frame compilado de load é 54.432 B para uma tarefa de 8.192 B. A incompatibilidade é demonstrada; o sintoma físico exato não foi observado. [Fonte][bike-buffer] |
| C2 — cache inicial de bateria | Confirmado, com condição | Antes de 5 s, a leitura inicial devolve o default 4,0 V sem consultar o ADC. Não é correto afirmar que o teste do boot nunca dispara: setup pode terminar depois dessa janela e os limiares são configuráveis. X01. [Fonte][power] |
| C3 — ADC baixo significa USB | Confirmado | ADC 0 e 1499 foram reportados como 4,0 V, 100% e carregando. A relação com tensão física exige o divisor/calibração reais; a classificação lógica errada foi reproduzida. X01. [Fonte][power] |
| C4 — AT_BASE interminável | Parcialmente confirmado | Sem dados não há saída por idle; busy ou rejeição persistentes não têm orçamento global. Entretanto, transferência concluída com ACK final desconecta e retorna a SCANNING nas linhas 286–293. Não é um deadlock de toda transferência. X02. [Fonte][at-base] |
| C5 — falta sleep por agenda | Confirmado como divergência de implementação | O código dorme por bateria, sem implementar o ciclo periódico de repouso descrito nos documentos. A autonomia de 35 dias não foi comprovada; um valor exato de consumo ou duração exige bancada. X02 e política de energia. [Fonte][bike-main] |
| C6 — fragmento venenoso por cabeçalho | Hipótese específica não demonstrada; outro defeito identificado | HEADER_BYTES é estimado e merece dimensionamento, mas isso não prova mensagem maior que 512 B. A arena JSON insuficiente é demonstrável pelo tamanho dos slots no alvo: X06. [Fonte][fragmentos] |
| C7 — currentBike não é liberado | Confirmado em parte | Sucesso e desconexão não limpam currentBike; a liberação ocorre após timeout de 30 s desde o último fragmento. Porém isSafeToExit retorna true imediatamente quando o buffer global está cheio. A frase sobre impedir sync mesmo cheio está errada. X03. [Fontes][pairing] |
| C8 — iteração para e informa sucesso | Confirmado, já N08 | Além de ocultar itens posteriores, o retorno true pode autorizar reescrita/remoção que descarta o restante do buffer operacional. A análise anterior descreve esse efeito adicional. [Fonte][iterator] |
| C9 — header inválido bloqueia append | Confirmado | A bike recebe busy e mantém os dados; o arquivo problemático não é reparado nem recebe caminho automático de quarentena/reconstrução nesse fluxo. Reparo deve preservar os registros recuperáveis, não simplesmente apagar. X05. [Fonte][append] |
| C10 — configuração não cabe no MTU | Confirmado como limitação condicionada | Há recusa explícita, sem fragmentação central→bike. MTU 185, por exemplo, só admite 182 B por notificação. Não foi medido MTU 185 nestas placas. Retry/estado de entrega também precisam ser fechados. X08. [Fonte][notify] |
| C11 — onDisconnect concorre com loop | Risco plausível, E2 | Callback modifica flags e ponteiros que o loop consulta/usa. Não foi reproduzido crash ou uso após liberação. Adicionar volatile não resolveria sincronização de acesso nem vida útil dos objetos. X09. [Fonte][disconnect] |
| C12 — processamento em callback | Confirmado em parte, já observado na revisão 2 | JSON e identificação ainda rodam na task BLE; ConfigCallbacks consulta BikeManager enquanto o loop pode alterá-lo. Os mapas de conexão do servidor têm mutex, portanto não é correto dizer que todos estão sem proteção. X09. [Fonte][callbacks] |

## 3. Adendos que devem entrar na lista de correções

Os IDs X01–X10 são exclusivos deste comparativo. Alguns ampliam achados anteriores; não representam dez defeitos independentes inteiramente novos.

### X01 — P1 — Bateria: primeira leitura fictícia e ADC baixo classificado como carga

**Evidência: E1-S**, métodos reais de PowerManager, com ADC e millis controlados pelos stubs existentes. [power_manager.cpp][power].

| Entrada do experimento | Tensão devolvida | Nível | Carregando |
|---|---:|---:|---|
| Boot em 100 ms, ADC 1800 | 4,00000 V | 100% | true |
| ADC 0, cache expirado | 4,00000 V | 100% | true |
| ADC 1499, cache expirado | 4,00000 V | 100% | true |
| ADC 1500, cache expirado | 2,41758 V | 0% | false |
| ADC 2419, cache expirado | 3,89875 V | 69% | false |
| ADC 2420, cache expirado | 3,90037 V | 100% | true |

Isso confirma três problemas lógicos: ausência de amostra real no primeiro uso, retorno de tensão saudável para ADC muito baixo e salto de 69% para 100% ao cruzar 3,9 V. Tensão alta isolada também não comprova carregador conectado. A calibração do circuito continua pendente, mas não é necessário conhecer o resistor exato para identificar esses atalhos no código.

**Aceite:** primeira leitura válida ou estado desconhecido explícito; ADC anômalo não vira USB/100%; carga identificada por evidência compatível com o circuito; continuidade/faixa da curva testada. A proteção deve continuar funcionando com dev_mode false.

### X02 — P1 — AT_BASE precisa de prazo global e política de repouso

**Evidência: E2.** [update e notificações][at-base]; [exceção de bateria no loop][bike-main].

O caminho de sucesso já desconecta. O problema está na conexão sem dados e nas falhas persistentes. Não existe prazo global para encerrar a visita; busy/rejeição pode reiniciar a tentativa. Enquanto transferActive está true, o loop suprime a transição de bateria crítica para sleep. Uma espera busy pode conservar esse estado sem transferência útil.

Ficar na base pode ser parte do comportamento desejado, mas precisa de uma política explícita de energia, limite de tentativa e forma de detectar saída. O fluxo de check-in com repouso dos documentos não está implementado no SHA revisado. Isso amplia as pendências de energia já registradas, sem provar uma autonomia específica.

**Aceite:** idle e rejeições repetidas têm comportamento finito e observável; proteção energética não fica suspensa por tempo ilimitado; dados permanecem na bike sem ACK final; perfil de repouso e contato é medido no hardware.

### X03 — P1 — Bike processada ocupa a vaga até o timeout

**Evidência: E2.** [processDataFromBike, processDataQueue e processBikeDisconnected][pairing].

processDataFromBike atribui currentBike e renova requestTimeout a cada fragmento. Nem o ACK final nem processBikeDisconnected chamam finishCurrentBike. Outras bikes entram na priorityQueue e só são atendidas depois de a vaga ser liberada pelo timeout de 30 s. A bike que aguarda ACK tem janela nominal de aproximadamente 15 s antes de desistir, criando um desencontro de prazos.

Isso prejudica vazão e pode gerar reenvios desnecessários. Não implica impedir toda sincronização: buffer global cheio força a saída, e o status de pairing tem outro timeout configurável. Tampouco significa que todos os dados de uma sessão de um fragmento demorariam 30 s para receber o primeiro ACK; o atraso é da liberação para a próxima bike.

**Aceite:** sucesso, abandono e desconexão liberam a vaga correta; sessões concorrentes progridem dentro de um orçamento compatível com ACK/retry; itens enfileirados de conexões encerradas têm reconciliação definida.

### X04 — P1 — Pending descoberta sem NTP continua excluída mesmo após o relógio voltar

**Evidência: E2.** [cadastro e seleção de pending][pending]; [visita posterior][pending-visit].

addPendingBike grava created_at=0 quando não há relógio válido. getPendingBikesForUpload exige relógio atual válido **e** created_at>0. Depois da sincronização, recordPendingVisit atualiza apenas last_contacts. Portanto a descoberta permanece fora do upload, mesmo com contatos posteriores, salvo reparo ou atualização externa do cadastro.

Esse caso não estava explicitado na revisão anterior. O teste de pending antiga com horário válido não cobre uma descoberta criada com tempo desconhecido.

**Aceite:** publicar identidade e descoberta com first_seen desconhecido/qualidade explícita; recuperar o contato confiável posterior sem inventar o horário original; reboot e NTP tardio não escondem a bike da aprovação remota.

### X05 — P1 — Recuperação precisa preservar dados antes de formatar ou bloquear indefinidamente

**Evidência: E2.** [mount da bike][mount-bike], [mount da central][mount-central] e [header do spool][append].

A bike usa begin(true) e ainda tenta format explicitamente se falhar. A central também habilita formatação ao falhar o mount. A falha de montagem pode levar ao descarte de todos os arquivos sem exportação ou decisão operacional. Separadamente, um único header inválido pode bloquear novos appends daquela bike, sem ação de recuperação útil.

Isso amplia N03/N07/N08 e o requisito de manutenção. Não afirma que toda falha de mount é recuperável nem que houve perda física neste ensaio. O problema é tratar a formatação destrutiva como recuperação normal sem preservar evidência ou pendências.

**Aceite:** modo de recuperação distinguível de instalação virgem; preservar/exportar o que for recuperável; quarentena e diagnóstico por arquivo; reinícios sucessivos não formatam silenciosamente dados cuja custódia foi assumida.

### X06 — P1 — A arena JSON da bike não comporta todos os fragmentos que o particionador admite

**Evidência: E1 para o tamanho do tipo no alvo; E2 para o fluxo e orçamento.** [serializeFragment e buildChunks][fragmentos].

O documento é criado com MAX_BLE_FRAGMENT_BYTES+192 = **652 B**. O DWARF do ELF ESP32-C3 informa **sizeof(VariantSlot)=16 B**. Na biblioteca instalada, BasicJsonDocument arredonda a capacidade por sizeof(void*), quatro bytes nesse alvo: 652 continua 652.

O limite de bytes do JSON transmitido e o espaço da árvore interna são orçamentos diferentes:

| Fragmento | Slots mínimos | Memória mínima de slots | Arena |
|---|---:|---:|---:|
| Final, dez leituras de bateria | 11 campos na raiz + 10 × (um array + dois valores) = 41 | 656 B | 652 B |
| Dados, seis redes em um chunk | 9 campos na raiz + um elemento de scans + três campos do scan + 6 × (um array + quatro valores) = 43 | 688 B | 652 B |

São limites inferiores, sem depender de cópias adicionais de strings. SSIDs curtos podem fazer buildChunks agrupar seis ou sete redes, embora o documento não comporte a árvore correspondente. Com dez leituras de bateria, até o fragmento final ultrapassa a arena. O código testa doc.overflowed e devolve false; a bike pode repetir uma serialização que continuará falhando.

Não foi executado esse caminho no ESP32 físico. A incompatibilidade de orçamento no alvo foi derivada do fonte atual, do layout no ELF e da política real de alocação da biblioteca. Os tipos da biblioteca no host têm outro tamanho; um resultado nativo não deve ser apresentado como medição dessa arena no ESP32.

A hipótese C6 de um cabeçalho estimado, sozinha, não demonstrou fragmento maior que 512 B. Esse novo cálculo identifica uma causa verificável de fragmento não serializável e deve ser tratado junto de N06, que limita a quantidade total de fragmentos na central.

**Aceite:** dimensionar e conferir árvore interna e payload serializado; testar chunks de uma a sete redes com SSIDs curtos/escapados e final com zero a dez leituras. Todo fragmento admitido pelo particionador deve ser serializável no alvo e caber no MTU efetivo.

### X07 — P1 — Load binário não aplica a validação completa da configuração

**Evidência: E2.** [load e validadores da bike][bike-config].

O anexo acerta ao apontar validação incompleta no boot. É necessário distinguir arquivo persistido de push: applyUpdate recusa timeout Wi-Fi menor que 100, scan BLE menor que um e sleep menor que 60. Já load não verifica todos esses campos; um arquivo do tamanho correto pode conter scan BLE zero, que a variante bloqueante de start da NimBLE interpreta como duração ilimitada. isValid apenas gera um aviso no setup e não estabelece um modo recuperável obrigatório.

Isso amplia N09. max_networks acima de vinte também pode ser confirmado pelo push e recusado no reboot. O consumidor de scan tem um break no limite de vinte, portanto não há evidência aqui de overflow desse array por esse campo; há configuração declarada/aplicada inconsistente.

**Aceite:** a mesma validação integral vale para download, load e aplicação; strings terminadas, limites e relações verificados; arquivo inválido mantém identidade e retorna a estado seguro com caminho de recuperação.

### X08 — P1 condicionado — Recusa por MTU e pedido único não completam configuração

**Evidência: E2**, além da limitação já registrada em R23/N09. [envio central][notify]; [pedido da bike][at-base]; [marcação de envio][config-request].

O runtime passou a impedir notificações maiores que o MTU útil, o que evita truncamento silencioso. A funcionalidade ainda depende de um envelope que caiba e de um caminho de recuperação. A bike ignora o resultado de requestConfig e ativa configRequested; a central marca config como enviada após enfileirar, embora pushConfigToBike/sendConfigToHandle não devolvam sucesso de entrega.

Não é correto concluir que não há qualquer nova oportunidade: heartbeats podem disparar auto-config enquanto a versão continuar divergente e uma reconexão permite novo pedido. Isso não garante progresso com MTU persistentemente insuficiente, assinatura de notificações falha ou versões/conteúdos equivalentes do N09.

**Aceite:** MTUs previstos têm contrato e fallback explícitos; erro de envio mantém pendência observável; pedido perdido ou assinatura falha provoca retry/recuperação; apenas ACK da revisão aplicada encerra a configuração.

### X09 — P1 condicionado — Serialização de callbacks e vida útil dos ponteiros ainda precisam fechar

**Evidência: E2.** [callback da bike][disconnect]; [callbacks da central][callbacks].

A transferência de notificações da bike e eventos da central para filas foi um avanço. Ainda há estado compartilhado fora desse desenho: onDisconnect altera ponteiros/flags diretamente; o loop os usa para escrever; ConfigCallbacks consulta autorização fora do contexto que a modifica. Os mapas de conexão da central têm mutex, que não cobre automaticamente BikeManager.

Não foi observado UAF, null dereference ou corrida em placa. O aceite deve avaliar propriedade e vida útil dos objetos, não apenas marcar bool como volatile ou acrescentar um lock sem revisar chamadas bloqueantes.

**Aceite:** desconectar durante descoberta, assinatura, escrita e processamento não causa acesso inválido; alterações compartilhadas têm um dono ou sincronização comprovada; filas têm limite, diagnóstico e comportamento de recuperação.

### X10 — P1/P2 — Sync monolítico pode atrasar controle e downloads por falha de observabilidade

**Evidência: E2.** [ordem do sync][sync]; [loop de lotes][uploads].

uploadHeartbeat retornando false encerra a tentativa antes de baixar config/registry. Um defeito em observabilidade pode impedir uma atualização necessária para corrigir o próprio funcionamento. uploadBufferData percorre muitos lotes em uma chamada; há limite de iterações, mas não um orçamento global de duração demonstrado.

A biblioteca HTTP possui timeout padrão de cinco segundos para conexão e leitura; isso não é deadline da operação completa nem do sync. Duzentos lotes, reconciliação, backups e espera Wi-Fi/NTP podem consumir tempo relevante. Não foi medido tempo de rádio/flash nesta rodada. A infraestrutura de watchdog habilitada no SDK também não comprova supervisão específica de todo esse fluxo.

**Aceite:** falhas de dados, observabilidade e controle têm política explícita; configuração/registry não ficam indefinidamente bloqueados por heartbeat; cada etapa e ciclo respeitam orçamento; retorno ao pairing e backlog são observáveis e recuperáveis.

## 4. Afirmações que não devem ser incorporadas como estão

| Afirmação do texto comparado | Conferência no SHA revisado | Tratamento correto |
|---|---|---|
| `.is<float>()` rejeita JSON double; tensões nunca mudam | Incorreta. Converter de tipos de ponto flutuante usa data->isFloat, que reconhece número. A função real aceitou 3,3/3,6 V, avançou para revisão 3 e salvou esses valores. | Retirar esse diagnóstico. Validação de valores e relações continua pertinente em N09/X07. |
| HTTP não tem timeout porque não há setTimeout | Incorreta como afirmação absoluta. HTTPClient.h define HTTPCLIENT_DEFAULT_TCP_TIMEOUT=5000 e inicializa conexão/leitura com ele. | Definir deadline e orçamento do produto; não confundir ausência de override com ausência de timeout. |
| Qualquer ID com prefixo bpr é autorizado a enviar dados | Incorreta. Identificação por prefixo e autorização são etapas distintas; processBikeData exige BikeManager::canConnect/isAllowed. | A autorização local existe. Prova criptográfica de identidade, se exigida para produção, é uma pendência distinta. |
| Nenhum lado inclui common/bpr_*.h | Incorreta para a central. include/constants.h inclui bpr_protocol.h e bpr_types.h. A bike mantém constantes duplicadas. | Há definições antigas/duplicadas, mas não ausência total de uso. [Fonte][central-constants] |
| Fragmentos nunca são remontados; bot só lê sessions antigo | Desatualizada/incompleta para este SHA. onBatchUploaded escreve raw_fragments e sessions_v2; os listeners de sessões/scans do bot usam v2. O listener de conexões ainda usa o antigo. | Corrigir N05 e N10; não reimplementar uma arquitetura já parcialmente existente. [Fontes][normalizer], [consumers] |
| data_decoded objeto ou data string torna o fluxo incompatível | Os dois formatos são aceitos por parseFragment no normalizador atual. | Testar ambos e fechar contrato; não tratar o fallback como perda inevitável. [Fonte][normalizer] |
| ntpInitialized faz a central nunca mais sincronizar | Não demonstrada. configTime inicia SNTP em modo POLL; o SDK obtido define atualização de 10.800.000 ms, três horas. | O latch impede repetir init, mas não desativa o SNTP. Frescor/qualidade e uso de forceResync ainda precisam de teste. |
| Buffer cheio não permite sync porque currentBike está preso | Incorreta: isSafeToExit retorna true antes das demais condições quando isFull. | Manter o problema de liberação/vazão X03, retirando esse efeito específico. [Fonte][safe-exit] |
| Zero em timeout/scan/sleep passa livremente pelo push | Incorreta para os campos citados: applyUpdate possui filtros. A rejeição pode ser silenciosa/parcial e load é insuficiente. | Separar N09 da configuração persistida inválida X07. [Fonte][bike-config] |
| Falha de deep sleep retorna e cria busy-loop normal em STATE_SLEEP | Não demonstrada. esp_deep_sleep_start é void com atributo noreturn no SDK; quem retorna esp_err_t é a configuração do wakeup. | Validar duração e resultado da configuração do wakeup; não afirmar um retorno normal inexistente nessa API. |
| Firmware está sem watchdog | Excessiva. Não há supervisão específica do loop demonstrada nos fontes; o SDK habilita inicialização e panic do task watchdog, com timeout de cinco segundos. | Conferir tarefas realmente inscritas e testar travamento. Não concluir proteção completa só do SDK, nem ausência total só dos fontes do app. |
| Não há coredump | O SDK está compilado com coredump em flash, mas a partição atual não reserva área coredump e o produto não tem fluxo de coleta/diagnóstico demonstrado. | Tratar como ausência de capacidade operacional qualificada; não como ausência de suporte no core. |
| AP usa BPR_Hub_Config | Incorreta no SHA atual. AP_SSID e enter usam BPR Central. | Atualizar a descrição operacional. [Fontes][central-constants], [ap] |
| Dois timeouts do AP efetuam duas transições concorrentes | Há dois valores, cinco e quinze minutos por padrão, mas ConfigAP::update apenas retorna ao expirar; main é quem faz a transição efetiva. | Unificar relógio/valor exibido e controlador. Não alegar uma segunda mudança de estado que a função não faz. [Fontes][central-main], [ap] |
| Ausência de file.sync prova falta de persistência | Conclusão insuficiente. File oferece flush/close; close chama fclose na implementação obtida. A garantia de flash precisa ser qualificada no filesystem. | N03 tem uma janela concreta remove→rename. Não recomendar cegamente um método sync que não integra a API File conferida. |
| SSID com vírgula quebra o parser do cadastro remoto | Esse caminho recebe IDs/status/MAC/timestamps; a config central é um array numérico. SSID não integra esses schemas. | Há fragilidade e falta de validação estrutural no parser; usar entradas realmente admitidas pelo contrato para reproduzir. [Fonte][registry-download] |

## 5. Outros pontos altos e médios do anexo

| Tema | Resultado / consequência |
|---|---|
| remove seguido de rename | Confirmado, já N03. Fechar arquivo temporário não elimina a janela entre remover o original e instalar a nova cópia. |
| Primeiro scan após 300 s e custo de um intervalo em falha | Confirmado pelo scheduler. O valor de 25 s é documental. A capacidade atual para antes de cinquenta scans por N02. [Fonte][scan] |
| Timeout por canal usado como prazo global | Confirmado como pendência, já seção 6 da revisão 2. Parametrizar/qualificar duração real e cancelamento. [Fonte][scan] |
| Scan BLE bloqueante concorre com Wi-Fi | Há bloqueio de aproximadamente cinco segundos no mesmo loop, relevante para rádio/latência. Não prova abortar todo scan Wi-Fi: performWiFiScan consulta scanComplete antes de aplicar deadline ao caso ainda RUNNING. [Fontes][scan], [at-base] |
| Upload de quatro itens e muitas leituras/cópias | Confirmado. O campo batch_size=8000 não governa esse loop; unidades também precisam ser definidas. A ausência de yield explícito não prova que o SDK nunca cede CPU. [Fonte][uploads] |
| Backups podem consumir a flash | Confirmado como risco de espaço, já seção 6 da revisão 2. São cópias do conjunto a cada confirmação, com limpeza no begin. O momento de saturação depende do backlog; três snapshots não cabem em todo perfil. [Fonte][backup] |
| Filas ilimitadas e perda de eventos | PriorityQueue não tem limite. EventQueue de dezesseis e pushQueue de oito descartam com log; a fila de quatro notificações da bike não confere o retorno de xQueueSend. A afirmação de descarte silencioso é excessiva para todas as filas, mas falta retorno/controle de fluxo. [Fontes][pairing], [push], [at-base] |
| Anúncio por nome e substring OFF/BUSY | Comportamento confirmado. OFFICE seria recusado, mas a central atual anuncia nomes fixos sem esse sufixo arbitrário. É fragilidade de contrato/descoberta; não foi reproduzida falha no nome padrão. [Fontes][at-base], [notify] |
| Notificações alcançam todas as bikes | Confirmado, já observado como limitação da NimBLE 1.4.3. O filtro por bike_id existe, porém ocorre depois de ocupar a fila de notificações. Avaliar saturação com vários assinantes. |
| ACK de cerca de quinze segundos versus fila de trinta | Confirmado como desencontro de orçamento, X03. Não é justificativa para aceitar ACK sem persistência ou aumentar todos os tempos sem testar vazão. |
| BLE sem autenticação criptográfica | Não há vínculo criptográfico implementado nos fontes conferidos. A lista de autorização por ID existe. Isso é diferente dos anúncios públicos por SSID do módulo de busca proposto, que são deliberadamente observáveis por celulares comuns. |
| Boot recuperado e millis entre reboots | Confirmado, já N04. Índice interno de scan não chega ao JSON como identidade da observação. |
| Salt/boot de dezesseis bits e wrap de millis | Riscos condicionados reais. Uma colisão não é garantida em cada reboot; comparações diretas de deadlines precisam teste de wrap. |
| Alocações/rebuild por fragmento | Confirmado: fragmentCount e serializeFragment reconstroem chunks. Custo/fragmentação de heap precisa de medição; X06 identifica antes um limite concreto de arena. |
| getSize retorna sempre tamanho da struct | Confirmado: não mede bytes úteis pendentes. O campo de heartbeat pode parecer haver 54.328 B mesmo com sessão vazia. Definir se se deseja capacidade ou carga útil e nomear de acordo. [Fonte][bike-buffer] |
| max_networks acima de vinte | Confirmação e reboot ficam inconsistentes, já N09/X07. O break em vinte limita o array da coleta; não foi demonstrado overflow por essa configuração. |
| Log em todo update de AT_BASE | Confirmado. É ruído/custo de diagnóstico, sem prova isolada de travamento. |
| Scan sem redes não vira registro | Confirmado. Apenas savedCount>0 grava scan e bateria. Pesquisa/diagnóstico não consegue diferenciar esse caso de ausência de amostra só pelos dados da sessão. [Fonte][scan] |
| LED/botão e JSON da bike | GPIOs são configurados, mas não há comportamento de LED/botão nesses fontes da bike. data/config.json não é lido pelo caminho de ConfigManager; inclui parâmetros não implementados e dev_mode divergente. Corrigir expectativa/documentação ou implementar explicitamente. |
| Credenciais binárias e migração | Formato bruto depende do layout; já entra no requisito de versão/migração/manutenção. malloc por si só não demonstra defeito; arquivo sem versão e falha de recuperação são questões mais relevantes. |
| Manifesto sem checagem integral | Confirmado, já N07. Precisa conferir conteúdo, gravação e recuperação das flags; readManifest também não valida todo o shape. |
| time(nullptr) usado sem qualidade em alguns metadados | Confirmado como necessidade de contrato, já N04/F20. Nem todo timestamp interno equivale a hora de coleta; preservar desconhecido sem tratá-lo como data Unix útil. X04 demonstra um efeito funcional particular. |
| cleanup sem limpeza física | A função apenas zera contador/log. O sistema tem cleanupOldBackups separado, chamado em begin. Não concluir que nenhuma limpeza existe. |
| Reset de filas durante sync/transição | Confirmado que descarta eventos. Fragmento sem ACK permanece na bike, portanto isso não demonstra sozinho perda de dados já confirmados. Pedido de config sem retry e perda de diagnósticos exigem tratamento. |
| portMAX_DELAY no mutex BLE | É espera sem prazo, mas não prova deadlock por si só. Revisar o que executa segurando o lock e medir atraso; há locks curtos corretos que usam essa política. |
| OTA, placa, UUIDs e arquivos ausentes | Divergências documentais confirmadas: alvo configurado XIAO ESP32-C3, app factory único, sem implementação OTA; README cita outro hardware, UUIDs e arquivos removidos. Atualizar documentos para o comportamento real. |

## 6. Como os escopos se complementam

| Tema | Contribuição das revisões |
|---|---|
| Memória no boot | Ambas localizam a candidata automática; a revisão anterior mediu o frame e a pilha no ELF. |
| Coleta efetiva | A revisão anterior executou ScanningState e demonstrou a parada em dez scans, que não aparece no anexo. |
| Entrega máxima | O anexo suspeita da quantidade de fragmentos; a revisão anterior produziu 501 com entradas válidas. Esta comparação acrescenta a arena insuficiente X06. |
| Bateria e permanência na base | O anexo detalha caminhos que mereciam aprofundamento; esta comparação confirma o comportamento do ADC/cache e delimita os cenários de AT_BASE. |
| Descoberta sem tempo | O anexo identificou o filtro created_at>0; X04 confirma por fluxo que visita posterior não repara a origem zero. |
| Remontagem, replay e métricas | O anexo foi apresentado como revisão de firmware. A revisão anterior também avaliou Functions/bot e reproduziu divisão de um scan em vários, apagamento de coordenadas, conversão dupla de tempo e contagem duplicada de viagem. Esses achados continuam válidos. |
| Persistência | Ambas apontam problemas. A revisão anterior detalha a exclusão do restante após CRC inválido e a relação manifesto/flags/retomada. |
| Implementação versus documentos | O anexo amplia o inventário de divergências. É necessário atualizar o contrato real, preservando como proposta os recursos ainda não implementados. |

A concordância entre textos ajuda a direcionar a conferência, mas não substitui evidência. Um ponto demonstrado por código/binário continua relevante mesmo que a outra revisão não o mencione; duas descrições iguais de uma hipótese também não a transformam em reprodução.

## 7. Adendo de PRD para os pontos adicionais

Este adendo complementa T01–T44 do PRD existente. Não declara os novos critérios aprovados e não exige implementação de código nesta entrega.

| Teste | Requisito / cenário de aceite | Evidência exigida | Ticket / achado |
|---|---|---|---|
| T45 | Primeira consulta de bateria, ADC zero/baixo, fronteira 3,9 V, bateria e USB reais. | Amostra inicial real ou desconhecido explícito; nenhum ADC anômalo vira carga confirmada; curva/limiares e fonte de carga qualificados no circuito. | BPR-11/RF13; X01. |
| T46 | AT_BASE vazio, busy repetido, falha persistente e bateria crítica durante espera/transferência. | Prazo e política de repouso definidos; proteção crítica não é suspensa ilimitadamente; dados só limpos após recibo exigido. | BPR-04/BPR-11; X02. |
| T47 | Duas ou três bikes entregam sessões curtas; uma desconecta após o final e outra aguarda. | Liberação correta da vaga; nenhuma espera de trinta segundos desnecessária; ACK/retry compatíveis com o pior atendimento homologado e fila limitada. | BPR-07; X03. |
| T48 | Descoberta com created_at zero; NTP torna-se válido; nova visita e reboot. | Pending é publicada com qualidade temporal explícita e pode ser aprovada; o filtro não a elimina por ausência do primeiro horário. | BPR-08; X04. |
| T49 | Mount falha ou header por bike está inválido, havendo pendências previamente aceitas. | Sem formatação destrutiva automática como recuperação normal; diagnóstico e recuperação/quarentena/exportação preservam dados recuperáveis. | BPR-03/BPR-11; X05 e N03/N08. |
| T50 | Serializar no alvo chunks com uma a sete redes de SSIDs curtos/escapados e finais com zero a dez baterias. | Árvore cabe na arena sem overflow; todo chunk produzido é serializável e payload cabe no MTU; cobertura inclui exatamente seis redes e dez baterias. | BPR-04; X06/N06. |
| T51 | Arquivo de config do tamanho correto com scan BLE zero, timeout/sleep inválidos e string sem terminador. | Validação integral restaura estado seguro e recuperável, preservando identidade; push e boot obedecem ao mesmo contrato. | BPR-06; X07/N09. |
| T52 | Configuração em MTUs 23, 185 e 512; falha de subscribe, pedido e notificação. | Comportamento previsto por perfil, fallback/erro explícito, retry e pendência preservada; ACK da aplicação continua sendo a conclusão. | BPR-06/BPR-07; X08. |
| T53 | Disconexão em descoberta, subscribe e escrita, com alterações de cadastro simultâneas. | Sem acesso inválido ou corrida observável; propriedade/sincronização documentada; filas e descartes diagnosticáveis. | BPR-07; X09. |
| T54 | Falha HTTP de heartbeat com downloads disponíveis; backlog grande e rede lenta. | Política permite progresso do controle necessário; prazo global medido, retorno ao pairing conhecido, watchdog efetivamente verificado e pendências reconciliáveis. | BPR-05/BPR-17; X10. |

### Ordem recomendada

1. Manter **N01 como primeiro bloqueio**: boot com orçamento de pilha válido.
2. Tratar **X06, N02 e N06 em conjunto**: serialização interna, quantidade de scans/baterias e capacidade do transporte precisam concordar.
3. Fechar **X01/X02**: a proteção energética não pode depender de tensão fictícia nem de espera ilimitada.
4. Fechar persistência/recuperação **N03/N07/N08/X05** antes de assumir custódia em campo.
5. Completar cadastro, configuração, filas e prazos **X03/X04/X07/X08/X09/X10**, mantendo os achados de tempo/normalização/consumo da revisão anterior.
6. Atualizar a documentação real e executar os cenários físicos pertinentes. Não reabrir como defeito uma afirmação refutada, como a rejeição de double por is<float>.

## 8. Limites desta comparação

Não foram medidos consumo real, tensão física, MTU negociado, atraso de rádio, tempo de flash, falhas de alimentação nem implantação do backend. O teste de bateria e o de configuração usaram os métodos reais com adaptadores locais; os novos demais cenários são leituras de fluxo ou dimensionamento explicitamente identificados. O orçamento X06 usa layout do alvo, não o sizeof dos tipos no host.

O diagnóstico não depende de saber qual modelo produziu cada texto. A lista de trabalho útil reúne os comportamentos demonstráveis, delimita hipóteses e exige um resultado verificável para encerrar cada correção.

## 9. Fontes fixadas

Além dos fontes do repositório abaixo, foram conferidos estes arquivos das dependências obtidas pelo build. Os caminhos são relativos à biblioteca/pacote indicado, para permitir a conferência em outra máquina com as mesmas versões.

| Dependência / evidência | Arquivo e ponto conferido |
|---|---|
| ArduinoJson 6.21.6 — números | `src/ArduinoJson/Variant/ConverterImpl.hpp`, conversor de tipos de ponto flutuante; `VariantData.hpp:122`, isFloat reconhece NUMBER_BIT. |
| ArduinoJson 6.21.6 — arena | `src/ArduinoJson/Document/BasicJsonDocument.hpp:137`, allocPool; `Memory/Alignment.hpp:21`, arredondamento por ponteiro; `Variant/VariantSlot.hpp:16`, layout do slot. |
| Binário ESP32-C3 da bike | DWARF de `firmware/bici/.pio/build/seeed_xiao_esp32c3/firmware.elf`: classe VariantSlot com DW_AT_byte_size=16; o mesmo alvo é o usado na revisão 2. |
| Arduino-ESP32 3.3.12 — HTTP | `libraries/HTTPClient/src/HTTPClient.h:44`, default 5000; linhas 297/299 inicializam timeouts de conexão/leitura. |
| Arduino-ESP32 3.3.12 — filesystem | `libraries/FS/src/FS.h:57`, flush; `libraries/FS/src/vfs_api.cpp:337`, close chama fclose. |
| Arduino-ESP32 3.3.12 — SNTP | `cores/esp32/esp32-hal-time.c:52`, configTime inicia SNTP em modo POLL. |
| SDK ESP32-C3 obtido, perfil qio_qspi | `include/sdkconfig.h:384`, pilha 8192; linhas 756–759, opções do task watchdog; linha 985, SNTP_UPDATE_DELAY=10800000; linhas 817/821, coredump em flash habilitado. |
| API de sleep do SDK obtido | `include/esp_hw_support/include/esp_sleep.h:192`, timer retorna esp_err_t; linha 638, esp_deep_sleep_start é noreturn. |
| NimBLE-Arduino 1.4.3 | `src/NimBLEScan.cpp:310` e `:399`, duração zero e espera bloqueante da variante usada pela bike. |

[bike-buffer]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/bici/src/buffer_manager.cpp#L154
[power]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/bici/src/power_manager.cpp#L5
[bike-main]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/bici/src/main.cpp#L119
[at-base]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/bici/src/at_base.cpp#L216
[disconnect]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/bici/src/at_base.cpp#L299
[fragmentos]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/bici/src/buffer_manager.cpp#L246
[pairing]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/src/bike_pairing.cpp#L329
[safe-exit]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/src/bike_pairing.cpp#L234
[iterator]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/src/buffer_manager.cpp#L346
[append]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/src/buffer_manager.cpp#L226
[notify]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/src/ble_server.cpp#L418
[callbacks]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/src/ble_server.cpp#L110
[pending]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/src/bike_manager.cpp#L311
[pending-visit]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/src/bike_manager.cpp#L409
[mount-bike]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/bici/src/main.cpp#L54
[mount-central]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/src/main.cpp#L199
[bike-config]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/bici/src/config_manager.cpp#L39
[config-request]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/src/bike_pairing.cpp#L423
[sync]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/src/cloud_sync.cpp#L56
[uploads]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/src/cloud_sync.cpp#L484
[central-constants]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/include/constants.h#L3
[normalizer]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/bot/functions/src/index.ts#L169
[consumers]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/bot/src/config/firebase.js#L33
[registry-download]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/src/cloud_sync.cpp#L353
[ap]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/src/config_ap.cpp#L81
[central-main]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/src/main.cpp#L297
[scan]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/bici/src/scanning.cpp#L11
[push]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/src/ble_server.cpp#L376
[backup]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/src/buffer_manager.cpp#L796
