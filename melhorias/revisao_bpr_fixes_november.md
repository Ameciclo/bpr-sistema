# Revisão técnica do BPR — branch fixes/november

**Data:** 7 de novembro de 2026, America/Recife.  
**Repositório efetivamente revisado:** [Ameciclo/bpr-sistema](https://github.com/Ameciclo/bpr-sistema).  
**Branch e commit:** `fixes/november`, `09dc9b150a1d60648d4cb440b4ec89189af3f3e2`.  
**Base de comparação:** `main`, `d341674a599fba150e9048b4d81124472092a3e1`.  
**Escopo:** correções F01–F25, firmware bike/central, BLE, persistência, sincronização, portal local, bot, Functions e estrutura do dashboard.  
**Entregável complementar:** `prd_bpr_confiabilidade_e_operacao.md`.  
**Modalidade:** revisão e documentação. Nenhum fonte, configuração ou dependência declarada do aplicativo foi alterado; nenhum commit, PR ou deploy foi criado.

## 1. Parecer

**A branch melhora a base anterior, mas ainda não deve ser considerada pronta para operação confiável.** A saída de BOOT foi corrigida; identificação no heartbeat, assinatura de notificações, integração do cadastro e seleção de partições estão presentes. Isso não encerra o funcionamento de ponta a ponta.

O bloqueio mais imediato está na coleta: o firmware inicia um scan Wi-Fi assíncrono e trata o retorno de “em andamento” como quantidade de redes. Na entrega, dez scans com uma rede já geram onze fragmentos, enquanto a central armazena apenas dez por bicicleta e exige todos para confirmar a sessão. Além disso, há perda de dados sob falha de escrita, retomada incorreta de uploads após reinício, deduplicação instável e confirmação de configuração sem durabilidade garantida.

**Recompilei e executei os verificadores nativos existentes sobre os fontes desta branch: 23 verificações reproduziram os comportamentos registrados.** Dois casos detectaram erros de memória com sanitizadores. Ambos os firmwares também concluíram build ESP32, confirmando 39,2% e 59,0% da partição app. O projeto de Functions foi verificado com as dependências do seu lockfile: TypeScript terminou com nove diagnósticos. As verificações nativas não executam rádio, flash física ou FreeRTOS.

A interface web continua sendo uma estrutura inicial, e o firmware grava lotes em um caminho diferente daquele consumido pelo bot. Portanto, corrigir apenas os firmwares ainda não entrega um aplicativo integrado.

### Qual repositório corresponde ao pedido

Foram consultadas as referências remotas dos dois nomes informados no histórico:

| Repositório | Referência observada | Interpretação |
|---|---|---|
| `Ameciclo/bpr-sistema` | `fixes/november` → `09dc9b1`; `main` → `d341674` | Contém exatamente o conjunto de correções descrito e a base da auditoria anterior. É o objeto desta revisão. |
| `Ameciclo/bpr-embarcado` | Somente `main` na consulta, em `efa7dbce625a181716c8e07049d177bc07bb39cb` | É outro código, identificado no README como WiFi Range Scanner para ESP8266. Não foi confundido com a branch revisada. |

O diff remoto confirmado tem **39 arquivos, +2504/−1543**, incluindo o exemplo de credenciais. Na árvore Git desse commit não existe uma pasta `melhorias/`; não é possível atestar, a partir da branch publicada, a afirmação de que essa pasta foi preservada. Os verificadores reutilizados vieram do ambiente da revisão anterior, fora do repositório.

## 2. Como interpretar a evidência

| Marca | Significado | Limite |
|---|---|---|
| **E1** | Comportamento reproduzido localmente nos fontes, ou diagnóstico de compilador. | Não equivale a teste em ESP32 físico. |
| **E1-L** | Reprodução de contrato da biblioteca com funções originais extraídas e tipos periféricos reduzidos. | Não instancia a pilha BLE completa. |
| **E2** | Conclusão sustentada pela leitura do código e do fluxo de chamadas. | Cenário não executado nesta rodada. |
| **Decisão** | Comportamento de produto ainda sem contrato ou política definidos. | A recomendação não é uma funcionalidade já existente. |

**P0:** impede o fluxo básico. **P1:** compromete integridade, recuperação, configuração, memória ou uso operacional. **P2:** manutenção, observabilidade ou fluxo auxiliar. A prioridade considera o efeito no produto; não é uma pontuação de vulnerabilidade.

Os links de código ao final apontam para o commit completo, permitindo revisar a mesma versão depois de a branch avançar. As linhas mencionadas são dessa versão.

## 3. Revalidação dos 25 achados originais

“Alteração constatada” significa que a mudança está no código. O encerramento exige os critérios de aceite e, quando aplicável, validação na placa.

| ID original | Situação | Evidência e pendência |
|---|---|---|
| F01 — transição de estados | Alteração constatada | Handlers retornam o próximo estado e o loop aplica a transição. R01 é outro bloqueio, agora na coleta. |
| F02 — identificação/heartbeat | Alteração constatada, integração parcial | `type`, `bike_id`, versão e pendências presentes; telemetria não é integralmente preservada até os consumidores: A04. |
| F03 — cadastro/autorização | Parcial | Merge é chamado no boot e no download; R15 mantém lacunas no provisionamento e na reconciliação. |
| F04 — fragmentação | Parcial | Envelope e fragmentos presentes; R02/R12/R23 e observação O01 impedem considerar o contrato encerrado. |
| F05 — ACK e limpeza | Aberto | R02–R06/R13; rota legada `proceed` ainda permite limpeza sem correlação. |
| F06 — configuração BLE | Parcial | Assinatura e envelope presentes; R07–R09/R23 impedem confirmação confiável. |
| F07 — ordem de boot/config | Alteração constatada | Load ocorre antes de gerar ID; R09/R20/R21 ainda afetam recuperação e migração. |
| F08 — recuperação da sessão | Parcial | Header e checkpoint presentes; R04/R16/R22 mantêm falhas de durabilidade, tempo e integridade. |
| F09 — agrupamento do scan | Aberto | Um registro agrupa as redes, mas R01 impede obter o resultado assíncrono. |
| F10 — handle e chegada | Parcial | Zero é handle válido e chegada é emitida uma vez; associação de identidade ainda falha em R18. |
| F11 — destinatário das respostas | Parcial | Config/data_ack incluem destinatário; `busy` não inclui. Broadcast lógico não implementa confidencialidade. |
| F12 — ciclo de vida BLE | Parcial | Start idempotente e cliente reutilizado; stop contém R11. |
| F13 — configurações por bike | Aberto | IDs reais são usados; versão remota é descartada em R07. |
| F14 — retry da nuvem | Alteração constatada | Contagem por tentativa e rearme explícito presentes. R13 é um defeito separado, no retry da bike. |
| F15 — associação dos buffers | Parcial | Arquivos por bike presentes; perda sob falha de escrita permanece em R04/R05. |
| F16 — confirmação da persistência | Parcial | Escrita nova tem retorno conferido, mas o arquivo anterior não é protegido: R04. |
| F17 — validação binária/limites | Parcial | Central valida magic/versão/contagem/CRC; R12 introduz acesso fora do array. A sessão da bike não tem CRC: R22. |
| F18 — montagem de lotes | Parcial | Há seleção e checagem; R05/R10/O01 mantêm problemas de estado e dimensionamento. |
| F19 — histórico/consumidores | Aberto | Subcaminho por lote evita sobrescrever todo o nó, mas R06 refuta a chave estável e A01 mantém a falta do normalizador. |
| F20 — tempo/versões | Parcial | Flags de tempo inválido foram acrescentadas; R07/R16/R20 e A03/A08 continuam relevantes. |
| F21 — autenticação por dispositivo | Adiado | Renomear o campo/documentação não implementa autenticação. A07/A09 registram outras lacunas de acesso no aplicativo. |
| F22 — JSON e sucesso do portal | Alteração constatada por leitura | Aceita plano/aninhado e valida/salva antes do HTTP 200. Não foi executado teste HTTP do portal nesta rodada; R20 registra aplicação parcial em RAM. |
| F23 — limites/segredos do portal | Parcial | Cópias limitadas, escape HTML e campos sem segredos pré-preenchidos presentes. O02 registra token em log fora do portal. |
| F24 — energia | Parcial, bancada necessária | Agenda de busca e prioridade foram alteradas; ADC/carga/autonomia não foram medidos. R13 pode prolongar transferência e adiar sleep. |
| F25 — build/partições | Build/tamanho corrigidos, execução em placa pendente | Dependências pinadas, tabela de 4 MiB selecionada e ambos os builds ESP32 aprovados nesta rodada. Migração, OTA e aviso de MTU são assuntos distintos: R21/R23. |

## 4. Falhas no firmware e no protocolo

### R01 — P0 — Scan assíncrono é consumido como resultado pronto

**Evidência:** E1/E2. [scanning.cpp:32][r01]; contrato da [API Wi-Fi da Espressif][wifi-api] e implementação 3.3.12 consultada.

O primeiro argumento de `scanNetworks` é `true`, solicitando execução assíncrona. No início bem-sucedido, a API retorna `WIFI_SCAN_RUNNING` (−1). O firmware usa imediatamente esse retorno no limite do loop e não chama `scanComplete()` para obter o resultado posteriormente. Assim, não percorre as redes nem salva o scan.

**Reprodução:** `scan_async=1 collected=0`, com o fonte da branch e adaptador que oferece três redes quando o resultado está disponível. O quarto argumento é duração máxima por canal, não timeout global; tratar 5000 ms como prazo total também é incorreto.

**Requisito:** distinguir início, operação pendente, conclusão, falha e prazo global. Liberar resultados somente depois de consumidos.

**Aceite:** três redes resultam em um scan com três redes; estado pendente não é tratado como zero redes; uma falha gera diagnóstico e recuperação limitada.

### R02 — P1 — Capacidade da central não permite confirmar sessões legítimas

**Evidência:** E1. [fragmentação da bike:200][r02b]; [capacidade central:47][r02c]; [aceitação do buffer:174][r02d].

A bike admite 50 scans e produz um ou mais fragmentos por scan, mais o fragmento final. A central mantém apenas dez itens por bike e exige os fragmentos da sessão no arquivo para autorizar a limpeza.

**Reprodução:** dez scans de uma rede produzem onze fragmentos; a central recusa o décimo primeiro. `eleventh_fragment_stored=0 complete=0`.

Subir parte dos fragmentos à nuvem não soluciona a completude: a remoção deles elimina também a evidência local, pois não há recibo durável separado do buffer de upload.

**Requisito:** dimensionar sessão, janela de recepção, armazenamento e comprovantes de entrega como um contrato único.

**Aceite:** sessão máxima suportada termina com confirmação, inclusive com uploads intermediários, perdas de conexão e reinício da central.

### R03 — P1 — Timestamp de recepção rompe a deduplicação

**Evidência:** E1. [addBikeData:120 e addData:155][r03].

A central adiciona seu horário antes de serializar e calcular o CRC. A deduplicação exige sessão, salt, sequência e esse CRC. Reenviar o mesmo fragmento em outro segundo altera o conteúdo usado na comparação.

**Reprodução:** fragmento idêntico recebido seis segundos depois resulta em dois itens persistidos: `same_fragment_success=1,1 persisted_items=2`.

**Requisito:** separar a identidade/conteúdo da bike dos metadados da central. Identidade repetida com o mesmo conteúdo é reenvio; com conteúdo diferente é conflito explícito.

**Aceite:** perder ACK e repetir depois de segundos ou reinício mantém um único fragmento lógico.

### R04 — P1 — Escrita malsucedida elimina o arquivo anterior

**Evidência:** E1. [saveBikeBuffer:511–546][r04c]; [checkpoint da bike:68][r04b].

Os arquivos canônicos são abertos com modo de truncamento e regravados por inteiro. Na falha, a implementação remove o arquivo. Verificar o retorno evita confirmar o item novo, mas não preserva itens antigos para os quais `addBikeData` já retornou sucesso e o fluxo podia emitir ACK.

**Reproduções:** na central, acrescentar B com escrita curta elimina A: `second_write_success=0 previously_acked_remaining=0`. Na bike, falhar no checkpoint seguinte impede recuperar a sessão previamente salva: `failed_checkpoint=1 recovered_previous=0`.

**Requisito:** gravação recuperável com última versão íntegra preservada; validar a estratégia de commit/rename ou journal no LittleFS real. Propagar falha do checkpoint antes de dormir.

**Aceite:** interrupção em diferentes offsets, falta de espaço e erro de I/O preservam os dados anteriormente confirmados.

### R05 — P1 — Flag de upload persiste antes do HTTP e fica esquecida na retomada

**Evidência:** E1. [preparo:353][r05p]; [seleção:259][r05s]; [confirmação:370][r10]; [fluxo HTTP:445][r06].

Preparar um lote grava `uploaded=true` antes da requisição. Após interrupção nesse ponto, a seleção seguinte ignora os itens. Não existe reconciliação de lotes inconclusos no boot. A confirmação global posterior remove todos os itens marcados, incluindo os de uma tentativa anterior sem entrega comprovada.

**Reprodução:** depois do preparo, o item permanece armazenado com zero candidatos a upload. Acrescentar outro item, preparar e confirmar o novo lote remove ambos. O verificador relê os arquivos, reproduzindo o estado persistido; não reinicia um ESP32 físico.

**Requisito:** separar pendente, preparado, enviado e confirmado; manter manifesto de lote e recuperação após reboot; confirmar por identidade exata dos itens/lote.

**Aceite:** reinício antes do HTTP, durante a requisição e após resposta perdida não esquece nem apaga um item sem prova de entrega.

### R06 — P1 — Identidade do lote muda entre tentativas

**Evidência:** E1 por função extraída literalmente. [computeBatchKey:431][r06].

A chave inclui o horário atual e o CRC do JSON; o próprio lote ganha timestamp na montagem. Outra tentativa do mesmo conjunto pode criar outra chave.

**Reprodução:** mesmo payload, relógio seis segundos depois → `same_batch_key_after_retry=0`.

**Requisito:** identidade e conteúdo do lote permanecem estáveis até sua confirmação. Reprocessamento remoto também deve deduplicar os fragmentos, independentemente de qual central os reenvie.

**Aceite:** requisição aceita com resposta perdida resulta em um único lote lógico e uma única sessão normalizada.

### R07 — P1 — Versão remota é perdida na central e incrementada pela bike no reenvio

**Evidência:** E1. [downloader:658–692][r07c]; [aplicação na bike:189][r07b].

`remoteVersion` é lida em um bloco do downloader, mas não é passada a `updateBikeConfigFromCSV`; prevalece o argumento padrão zero. A bike, ao receber uma versão igual ou menor, incrementa sua própria versão em vez de tratar reenvio ou atualização antiga.

**Reproduções:** baixar versão 42 armazena versão zero; reaplicar versão 42 na bike produz versão 43.

**Requisito:** uma mesma revisão atravessa nuvem, central, bike e ACK sem mudança local. Versão igual/conteúdo igual é idempotente; conteúdo diferente ou versão antiga seguem política explícita.

**Aceite:** versão 42 continua 42 em todos os participantes após retry e reconexão.

### R08 — P1 — ACK de configuração compara endereços e encerra pendência no erro

**Evidência:** E1. [interpretação do ACK:346][r08p]; [markConfigAcked:442][r08m].

A expressão que compara o valor desserializado a `"ok"` usa `const char*` e compara endereços, não o texto. O verificador retorna `ack_ok_comparison=0` para status ok. Além disso, `markConfigAcked` limpa a flag de pendência mesmo recebendo versão zero por erro, sem conferir a revisão esperada.

**Reprodução:** `config_pending_after_failed_ack=0`.

**Requisito:** comparar conteúdo e validar destinatário, status, versão e configuração aguardada antes de encerrar a pendência.

**Aceite:** ACK correto conclui somente a revisão esperada; NACK, versão antiga e ACK de outra revisão mantêm a pendência com erro visível.

### R09 — P1 — Bike confirma configuração mesmo quando não a persiste

**Evidência:** E1. [save/processUpdate:63–203][r09]; [envio do ACK:364][r09a].

`save()` retorna void. `processUpdate()` retorna sucesso mesmo que abrir/gravar o arquivo falhe; o chamador emite `config_ack` de sucesso. Uma aplicação só em RAM é apresentada como concluída.

**Reprodução:** escrita interrompida → `config_update_success=1 persistence_valid=0 ram_interval=42 reboot_interval=300`.

**Requisito:** validar em uma cópia, persistir de forma recuperável e só então confirmar. Falha deve preservar configuração anterior e pendência remota.

**Aceite:** filesystem cheio/escrita curta não gera ACK de sucesso; reboot recupera a última configuração confirmada.

### R10 — P1 — Confirmação de upload não libera o mutex

**Evidência:** E1. [markAsConfirmed:370–401][r10]; [lock:21][r10l].

O método adquire o mutex e termina sem liberá-lo. A situação não se limita a espera: `lock()` imprime timeout e permite continuar sem ter obtido o recurso, e os chamadores podem liberar um mutex cuja aquisição falhou.

**Reprodução:** `mutex_still_taken_after_confirm=1`, com adaptador do contrato do semáforo.

**Requisito:** aquisição com resultado obrigatório e liberação em todas as saídas; não executar região protegida se a aquisição falhar.

**Aceite:** confirmação seguida de nova recepção/upload funciona; disputa entre callback e loop não acessa buffers sem exclusão válida.

### R11 — P1 — Stop transfere indevidamente a propriedade de callback estático

**Evidência:** E1-L/E2. [instalação do callback:190][r11]; [API NimBLE 1.4.3][nimble-h]; [destrutor][nimble-d]; [deinit][nimble-deinit].

O callback do servidor é estático e instalado sem desabilitar a destruição automática. Na 1.4.3, o argumento de propriedade é true por padrão. `deinit(true)` destrói o servidor, que tenta executar delete sobre esse objeto estático.

**Reprodução de contrato:** ASan detectou `attempting free on address which was not malloc()-ed`. Foram usadas as funções originais da biblioteca com tipos periféricos reduzidos; não foi executada a pilha BLE inteira.

**Requisito:** propriedade e tempo de vida compatíveis com a API, sem liberar objeto estático nem acumular callbacks/serviços.

**Aceite:** repetidos ciclos pairing → AP → pairing e parada/reinício não causam falha ou crescimento persistente de recursos.

### R12 — P1 — Verificação de completude indexa fora de seen

**Evidência:** E1. [hasCompleteSession:219–225][r12].

`seen` tem dez posições. A verificação limita a sequência pelo total informado, mas não pelo tamanho do array. Um arquivo pode ter um único item com sequência 10; o limite de quantidade de itens não limita o valor da sequência.

**Reprodução:** chamada do verificador com item seq 10/total 11 → UBSan indica índice 10 fora de `bool[10]`; ASan detecta `stack-buffer-overflow` no fonte real. O teste chama diretamente a função de completude.

**Requisito:** limites de seq/total e estrutura de completude coerentes com o protocolo. Corrigir junto com R02, sem simplesmente inviabilizar sessões legítimas maiores.

**Aceite:** totais grandes, lacunas, duplicatas, fragmentos fora de ordem e índices inválidos têm resposta controlada.

### R13 — P1 — Retry de fragmento não termina; busy não impõe espera

**Evidência:** E1/E2. [envio/timeout:251–274][r13]; [busy e proceed:371–384][r13n]; [resposta busy:50][r13h].

O timeout incrementa o contador, mas uma nova escrita GATT bem-sucedida zera esse contador antes de receber ACK. Sucesso de transporte não comprova confirmação da aplicação.

**Reprodução:** vinte updates espaçados por seis segundos causam dez envios sem atingir a saída por três tentativas: `missing_ack_data_writes=10 disconnects=0 connected=1`.

Ao receber busy, a bike aborta a transferência; o mesmo update pode iniciá-la novamente porque ainda há dados. `retry_after_sec` é ignorado. A resposta busy não traz `bike_id`, embora a característica notifique todos os assinantes.

Também permanece `proceed` com limpeza direta sem sessão/seq/ACK final: `data_after_unscoped_proceed=0`. Não foi encontrado emissor ativo desse tipo na central atual; trata-se de uma rota legada de limpeza a restringir/remover.

**Requisito:** contador por fragmento até ACK; pausa real com backoff; respostas sempre correlacionadas; limpeza apenas por recibo final válido.

**Aceite:** ausência persistente de ACK encerra a tentativa; busy de 30 s impede novo envio por esse período; busy de uma bike não altera outra; mensagem legada não apaga dados.

### R14 — P1 — Configuração aceita divisores de LED iguais a zero

**Evidência:** E1/E2. [validação:94][r14c]; [consumo dos divisores:46][r14l]; [padrão de contagem:140][r14b].

A validação nova só cobre `count_pause_ms`. `ble_ms`, `sync_ms`, `error_ms` e `count_ms` podem ser zero e são usados em módulo ou no cálculo de um ciclo usado como divisor.

**Reprodução:** os quatro campos zerados são aceitos: `zero_led_durations_accepted=1`. O verificador testa a aceitação; a falha aritmética é identificada por leitura dos consumidores.

**Requisito:** validar todos os divisores e suas faixas antes da aplicação; tratar valor inválido também no consumidor.

**Aceite:** configurações com zero, valor negativo ou fora da faixa não substituem a anterior nem produzem divisão por zero.

### R15 — P1 — Cadastro local não fecha o fluxo de provisionamento

**Evidência:** E2. [mergeFromRegistry:134–197][r15]; [pending/upload:249–334][r15p]; [sync ativo:73–125][r15s].

Há três lacunas distintas no ciclo do cadastro:

1. Uma bike desconhecida pode ser gravada como pending local, mas o produtor `getPendingBikesForUpload()` não é chamado pelo fluxo ativo. O heartbeat enviado à nuvem contém contagem, não o catálogo de pendentes. Não foi encontrado um caminho completo de envio dessas descobertas para gestão remota.
2. A lista operacional tem dez posições compartilhadas por allowed/pending/blocked. Se dez pendentes locais ocuparem as posições, o merge para ao tentar inserir uma nova bike autorizada, e ainda retorna true. O cadastro remoto não consegue estabelecer essa autorização nesse estado.
3. O merge só acrescenta/atualiza. IDs ausentes do snapshot novo permanecem com o status anterior. **Se remoção do cadastro significar revogação, ela não é aplicada**; atualizar explicitamente o status para blocked é outro caminho e não deve ser confundido com remoção.

Mesmo se o uploader de pendentes fosse ligado, a função filtra criação com menos de cinco minutos e `created_at>0`; descoberta sem NTP ou atraso prolongado pode excluir o item. Isso ainda não é um fluxo durável de publicação.

**Requisito:** separar autorização de descobertas locais, reconciliar snapshot com política de exclusão/tombstone e manter publicação pendente até confirmação remota.

**Aceite:** descoberta antes do NTP aparece para aprovação depois da recuperação; dez pendentes não impedem aplicar um allowed remoto; revogação segue a política escolhida e fica auditável.

### R16 — P1 — Sessão recuperada mistura relógios de boots diferentes

**Evidência:** E2. [load/start da sessão:79 e 166][r16m]; [timestamp de scan:50][r16s]; [identidade/estrutura:65][r16t].

A sessão recuperada continua ativa e preserva o início e o salt antigos. Novos scans usam o `millis()` do novo boot. Não existe `boot_id` por amostra nem ajuste para uma origem temporal contínua. Os timestamps podem recuar ou colidir com os antigos; a serialização procura o scan pelo timestamp e usa o primeiro correspondente.

Isso pode confundir a ordenação temporal e, havendo timestamp repetido, a associação de redes na fragmentação. Marcar o horário de recepção da central como válido não recupera o instante real de coleta.

**Requisito:** preservar origem monotônica por boot, identidade própria do scan e qualidade do horário absoluto; uma retomada não pode misturar origens silenciosamente.

**Aceite:** coletar, reiniciar offline e coletar novamente preserva todas as amostras distintas, em ordem explicável, sem inventar uma data Unix.

### R17 — P1 — Coleta para ao atingir 50 scans, sem rotação ou política operacional

**Evidência:** E2, visível após reparar R01. [limite/addScan:34][r17]; [loop de coleta][r01]; [isFull:294][r17f].

O armazenamento da bike contém uma única sessão de 50 scans. Quando cheio, `addScan()` apenas recusa a amostra; o fluxo ativo da bike não usa `isFull()` para rotacionar, enfileirar sessões ou alterar sua política. No intervalo padrão de 300 s, o limite é alcançado em aproximadamente 4 h 10 min de coletas nominais, com variação pelo tempo das operações.

O sistema pode continuar ligado e procurando uma base enquanto deixa de registrar o restante do percurso. Uma viagem longa ou dias sem retorno não têm política de retenção definida.

**Requisito:** orçamento explícito de armazenamento e retenção, múltiplas unidades recuperáveis de coleta ou política de saturação declarada e diagnosticável. A opção não pode apagar registros já confirmados por outra camada.

**Aceite:** coleta que ultrapassa a capacidade é tratada conforme a política aprovada; o operador distingue saturação de ausência de movimento/rede.

### R18 — P1 — Identidade não é fixada à conexão para dados e configuração

**Evidência:** E2. [DataCallbacks:121–137][r18d]; [ConfigCallbacks:150–166][r18c]; [onConfigRequest:326][r08p].

O callback de dados substitui o ID associado ao handle a cada mensagem; uma mudança não passa novamente pelo evento inicial. O callback de configuração recebe o descritor, mas não o usa para conferir o `bike_id` do payload, nem exige a autorização usada no caminho de dados.

Assim, conhecer o handle de origem não basta para garantir que pedido, ACK e configuração correspondam à identidade previamente aceita. Também não há política explícita para duas conexões declarando o mesmo ID. Não foi executada tentativa contra dispositivos reais.

**Requisito:** vínculo de identidade durante a conexão; consistência e autorização para todas as características e tipos de mensagem; tratamento de identidade duplicada.

**Aceite:** ID divergente em uma conexão é rejeitado; conexão não autorizada não confirma configuração de outra bike; duas conexões de mesmo ID não se confundem. Isso é separado da autenticação REST adiada em F21.

### R19 — P1 — Fila de recepção e mapas têm acesso concorrente sem coordenação suficiente

**Evidência:** E2. [callbacks BLE][r18d]; [fila/estado pairing:17–72][r19q]; [consumo da fila:365][r19p]; [expiração busy:432][r19b].

A fila de pushes protege parte do envio de notificações, mas callbacks de recepção ainda fazem parsing, chamam gestão/persistência e alteram estado e `priorityQueue`. O loop também consulta/remove da fila e percorre mapas alterados pelos callbacks. O mutex de buffer não cobre essas estruturas.

A fila de recepção não tem limite explícito nem deduplicação de retries. Além disso, busy pode expirar por horário enquanto o estado principal continua CLOUD_SYNC, reabrindo recepção durante operações de sincronização. Não foi reproduzido um crash por corrida nem medido crescimento de heap; são ausências de coordenação observáveis no código.

**Requisito:** recepção com fila limitada e operações compartilhadas serializadas, ou sincronização demonstravelmente correta; admissão de dados vinculada ao estado real, não só a um prazo.

**Aceite:** várias bikes, retries e sync lento mantêm heap/fila limitados, origem correta e ausência de acesso concorrente não protegido.

### R20 — P1 — Rejeição e sucesso de configuração ainda podem deixar estado incoerente

**Evidência:** E2. [load da bike:42][r20b]; [load e update da central:59 e 115][r20c]; [boot da central:242][r20m]; [download central:246–298][r20s]; [portal:308–369][r20p].

O fluxo ainda não é transacional:

- O load da bike lê direto no objeto ativo. Na leitura curta retorna false sem restaurar os padrões; o setup pode então gerar ID e salvar esse objeto parcialmente sobrescrito.
- Na central, um load de tamanho/conteúdo inválido também deixa o objeto alterado. O setup anuncia que salvará padrões, mas não reconstrói esses padrões antes de `saveConfig()`.
- `updateFromCSV()` aceita contagem parcial de campos e conversões antes de validar; ignora o retorno de `saveConfig()`. O wrapper de atualização é void e `downloadCentralConfig()` retorna true mesmo se aplicar a configuração tiver falhado.
- O downloader por bike trata HTTP 200 com corpo `null` na versão como zero e pode decidir que está atualizado mesmo sem slot/config local. Falta diferenciar versão ausente de uma versão válida.
- O portal usa referências ao objeto ativo. Se um campo tardio excede o limite ou falha na validação, campos anteriores já podem ter sido aplicados em RAM, apesar do HTTP 400. A proteção de cópia é útil, mas não oferece rollback.

Também permanece fallback de versão para uptime na central quando a versão remota é zero. Números convertidos para tipos pequenos precisam ser validados antes da conversão, incluindo relações entre limites de bateria.

**Requisito:** decodificar/validar uma configuração candidata completa; validar schema, tipos, faixas, versão e consistência; persistir e trocar o estado ativo somente no sucesso. Ausência de versão é erro de contrato ou política explícita de primeiro provisionamento.

**Aceite:** arquivo truncado, CSV incompleto, `null`, faixa inválida, campo longo e erro de escrita preservam a última configuração válida; nenhuma camada relata sucesso indevido.

### R21 — P1 — Mudança de IDs, binários e partições não tem plano de migração

**Evidência:** E2/Decisão. [geração de ID:11][r21id]; [conversão binária:66][r21bin]; [novo layout][r21part]; [formatação no boot da bike:56][r16m]; [formatação da central:197][r21fs].

O gerador passou de decimal para hexadecimal. Porém, a bike preserva um ID já salvo; a central passa a interpretar os dígitos como hex. Arquivos antigos de status/registry com inteiros decimais e o mesmo layout não têm um identificador de formato que permita essa distinção. Exemplo: o inteiro legado 123456 vira `bpr-01e240`, enquanto o texto legado `bpr-123456` é interpretado como outro inteiro.

Os novos layouts de sessão/buffer/config também não possuem um migrador ativo. O novo filesystem começa em `0x310000`; preservar dados de placas existentes depende da tabela que foi realmente gravada antes. Ambos os boots têm caminho de autoformatação quando a montagem falha.

Não foi identificada a tabela nem o conteúdo das placas instaladas; portanto, não afirmo que a atualização necessariamente apagará todas elas. A conclusão é que **não existe uma atualização preservadora demonstrada**.

**Requisito:** inventário das versões instaladas, aliases de identidade, exportação/recuperação dos dados e migração verificável; formatação precisa ser uma ação de recuperação deliberada.

**Aceite:** atualizar uma placa com configuração e pendências legadas preserva associação e dados, ou exporta tudo antes de uma intervenção explicitamente destrutiva. Boot com falha transitória de montagem não apaga automaticamente as pendências.

### R22 — P1 — Sessão da bike não valida a integridade do conteúdo

**Evidência:** E2. [header e estruturas][r16t]; [load da sessão:101][r22].

O header contém magic, versão e tamanho; o load valida também contagens e terminador do ID. Não há checksum do payload da sessão, nem verificação de terminadores de SSID/BSSID usados posteriormente como strings C. Uma alteração de conteúdo com tamanho e contagens preservados pode ser aceita; strings corrompidas podem ultrapassar seu campo ao serem lidas. O CRC introduzido no buffer da central não protege esse arquivo da bike.

**Requisito:** integridade e validação estrutural de todo o registro persistido, com preservação/quarentena da última versão recuperável. Header não deve ser apresentado como proteção integral do conteúdo.

**Aceite:** alteração de um byte de payload é detectada; strings sem terminador e contagens inválidas são rejeitadas sem acesso fora dos limites e sem eliminar uma cópia íntegra disponível.

### R23 — P1 — Flag de MTU 512 é sobrescrita; configuração pode chegar truncada

**Evidência:** E1 para o aviso do build e E2 para o efeito no transporte. [inicialização da central:187][r23c]; [envio por notificação:325][r23s]; [NimBLE notify:467][nimble-notify]; [MTU inicial da pilha][nimble-att]; [mapeamento ESP32][nimble-cfg].

O build independente mostrou `sdkconfig.h:492` redefinindo `CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU` para **256**, sobre a flag 512 do projeto. A inspeção do ELF produzido da central confirmou `ble_att_init` inicializando `ble_att_preferred_mtu_val` com 256. A bike chama `setMTU(512)` em runtime; a central não faz o equivalente. Portanto, a preferência de 512 na bike não comprova um MTU negociado de 512, limitado também pelo outro participante.

Com MTU 256, uma notificação comporta 253 bytes de valor. A configuração padrão formada por `getConfigForBike`, com ID `bpr-123456`, valores `300,5000,20,-85,3600,5,0` e versão zero, tem **254 bytes em JSON compacto**; uma versão de dez dígitos leva esse exemplo a 263 bytes. Esse tamanho foi calculado a partir dos campos do envelope, sem executar rádio. A biblioteca avisa sobre excesso e a camada ATT limita a notificação ao MTU; o JSON pode chegar incompleto e ser rejeitado pela bike.

Escritas da bike para a central têm outro caminho: a biblioteca pode usar long write. Não se deve concluir a partir disso que notificações da central também fragmentam/reconstituem o JSON automaticamente.

**Requisito:** definir MTU efetivo em ambos os papéis, observar a negociação e dimensionar/fragmentar também o caminho central → bike. Aceitar um envelope de até 512 no código não garante que caiba na notificação.

**Aceite:** configuração padrão e máxima válida chegam completas, com revisão correta, nos MTUs suportados; o teste registra MTU negociado e tamanho transmitido/recebido. Uma mensagem maior não vira tentativa anunciada como sucesso.

## 5. Integração e novas falhas no aplicativo

Estes componentes não mudaram no commit. Os achados abaixo ampliam o escopo ao aplicativo completo; não atribuem essas falhas às correções do firmware.

### A01 — P1 — Falta o consumidor de lotes; dashboard não implementa a operação

**Evidência:** E2. [endpoint dos lotes:57][a01e]; [listeners do bot:31][a01b]; [triggers Functions:161][a01f]; [root do dashboard][a01w].

O firmware grava em `/bases/{baseId}/data/{batchId}`. Os consumidores leem `/bikes/{bikeId}/sessions/...`. Não foi encontrado código ativo que reúna fragmentos, valide completude, reconcilie retries e publique sessões nesses caminhos. Os conversores existentes tratam outros formatos ou operações manuais, não esse novo protocolo de entrada.

`web/app/root.jsx` contém título e Outlet; não há rotas de gestão, autenticação, consulta Firebase ou acompanhamento de entrega. Isso é uma lacuna de implementação, não uma interface funcional cuja integração tenha sido aprovada.

**Requisito:** contrato e normalizador versionados; projeções derivadas reconstruíveis; painel mínimo para demonstrar coleta → persistência → normalização.

**Aceite:** um lote real aparece como uma única sessão completa no consumidor, e um replay não duplica sessões, viagens, alertas ou métricas.

### A02 — P1 — Consumidores de geolocalização usam formatos incompatíveis entre si

**Evidência:** E2. [handleNewScan:120–135 e getLastRoute:275][a02b]; [formatWifiData:10 e calculateRoute:69][a02g].

`handleNewScan()` transforma as redes em objetos `{ssid,bssid,rssi,channel}` e os entrega a uma função que espera arrays e lê índices 1/2/3. A requisição resultante perde os campos esperados de BSSID/RSSI/canal. Na rota, o chamador entrega scans em objetos com `timestamp/networks`, mas `calculateRoute()` lê `scan[2]`, descarta os registros e pode devolver zero pontos. Outros leitores esperam scans de dois ou três elementos.

**Requisito:** um DTO interno de scan/rede e adaptadores de entrada explicitamente versionados; fixtures compartilhadas entre produtor e consumidores.

**Aceite:** a mesma amostra normalizada é aceita pelo bot, Functions, cálculo de rota e exportação; duas localizações válidas geram rota sem depender de campos indefinidos.

### A03 — P1 — Monitor de estações compara segundos com milissegundos

**Evidência:** E2. [uploadHeartbeat:512–520][a03c]; [checkStationHeartbeat:41–42][a03b].

O caminho `last_heartbeat` coincide entre produtor e consumidor, mas a unidade não. O firmware grava Unix em segundos; o monitor subtrai esse número de `Date.now()`, em milissegundos. Um heartbeat recente é interpretado como antigo. `timestamp_valid=false` não é usado para apresentar um estado de horário desconhecido.

O status inicial do painel do bot ainda trata ausência de um false explícito como online. Os campos de disponibilidade por bike exigidos pelo monitor também dependem de uma projeção que o firmware não mantém diretamente.

**Requisito:** unidade única nas fronteiras, validação de horário e estados distintos para online/offline/sem evidência.

**Aceite:** heartbeat de agora é online; timeout efetivo produz uma única transição offline; horário desconhecido não inventa conectividade.

### A04 — P1 — Contrato de bateria mistura percentual e tensão

**Evidência:** E2. [BatteryData e armazenamento][r16t]; [leitor de bateria:176][a04f]; [cálculo por tensão:56][a04b]; [heartbeat da bike:89][a04p].

A estrutura de sessão da bike armazena `[timestamp_millis, percent]`; um leitor do bot interpreta `[timestamp, voltage, charging]`. Um percentual como 80 pode ser tratado como 80 V e convertido novamente para percentual. Outros trechos do próprio bot tratam a segunda posição como nível percentual.

Além disso, não há chamada ativa a `addBattery()` no firmware da bike. O heartbeat contém tensão/percentual/carga, mas a central atual preserva apenas parte dessa informação e o heartbeat remoto não publica a telemetria individual completa.

**Requisito:** separar tensão em V/mV, percentual, carregamento e validade da leitura; publicar a telemetria que os consumidores efetivamente usam.

**Aceite:** uma leitura conhecida atravessa o fluxo sem troca de unidade; valor ausente é desconhecido; histórico de bateria tem produtor ativo e não depende de conversão presumida.

### A05 — P1 — Processamento de sessões depende de RAM, ordem de eventos e chegada tardia

**Evidência:** E2. [listeners:31–76][a01b]; [início/scan:90–147][a05b]; [chegada:308][a05a]; [viagens ativas:11][a05r]; [publicação:79][a05p].

O listener observa `child_changed` de bikes e escolhe a última chave disponível, em vez de consumir eventos identificados de sessão. Não há captura completa de sessões existentes no início por esse caminho. O bot deduplica em RAM, registra listeners de scans sem removê-los e guarda o último timestamp por bike, sem incluir a sessão. Uma nova sessão com relógio menor pode ter seus scans descartados.

Scans de um upload histórico podem gerar mensagens “está rodando agora” e “tempo real”. O fluxo Wi-Fi → bike offline → central → nuvem só observa esses dados após entrega. Início de sessão de transporte não prova saída física de estação, e callbacks assíncronos não garantem concluir a rota antes do evento de chegada finalizar a viagem.

**Requisito:** processamento por identidade persistente, ordenação explícita, retomada/replay e separação entre instante do evento e da ingestão. Viagem e sessão de coleta são entidades diferentes.

**Aceite:** reiniciar o bot e reprocessar dados antigos produz o mesmo histórico, sem alertas duplicados ou afirmação de movimento atual; eventos fora de ordem não perdem pontos.

### A06 — P1 — Projeto Functions não passa na verificação TypeScript

**Evidência:** E1. [configuração TypeScript][a06t]; [referência indefinida:265][a06i]; [package das Functions][a06p].

Foi executado `tsc --noEmit --pretty false` após instalação pelo lockfile, com scripts de instalação desabilitados. Resultado: saída 2 e **nove diagnósticos**:

| Local | Diagnóstico |
|---|---|
| `src/index.ts:92` | Dois acessos a `error` do tipo unknown. |
| `src/index.ts:257`, `259`, `263` | Acesso a valores do tipo unknown. |
| `src/index.ts:260` | Desestruturação de unknown sem garantia de iteração. |
| `src/index.ts:265` | `GeolocationService` não existe no escopo. |
| `src/local-bot.ts:1`, `src/local.ts:1` | Módulo/tipos `dotenv` ausentes nesse projeto. |

Há também dois handlers para o comando converter no mesmo arquivo. A configuração inclui todos os arquivos de `src`, portanto os módulos locais entram no build. Na raiz, `build:bot` chama um script que não existe em `bot/package.json`; scripts `setup/dev:firmware/deploy:all` apontam para uma pasta `scripts/` ausente na árvore publicada.

**Requisito:** escolher e documentar os entrypoints suportados; verificar cada projeto com instalação limpa; corrigir erros de tipos/importação e comandos de operação.

**Aceite:** instalação pelo lockfile seguida da verificação/build previsto termina com sucesso, sem iniciar Telegram nem acessar banco de produção.

### A07 — P1 — Regras versionadas liberam leitura e escrita em bikes

**Evidência:** E2/condicional. [database.rules.json][a07]; [configuração que seleciona essas regras][a07f]; [semântica oficial das regras][firebase-rules].

As regras publicadas concedem `.read=true` e `.write=true` sob `/bikes/{bikeId}`. A validação de alguns campos de sessão não substitui autorização. Caso essas regras sejam implantadas, dados e rotas nesse ramo não exigem identificação para leitura/escrita. Não foi acessado o banco de produção e não foi verificado quais regras estão implantadas.

**Requisito:** autorização por função/dispositivo/estação, escrita da ingestão por caminho apropriado e visão pública separada de rotas e registros brutos. Tratar isso junto à decisão de F21, sem presumir que OAuth sozinho resolva regras permissivas.

**Aceite:** emulador comprova as operações permitidas e negadas para visitante, operador e dispositivo; usuário de uma base não altera outra; visão pública não contém trajetos individuais por acidente.

### A08 — P1/P2 — Duração, contadores e indicadores de impacto não são reprocessáveis

**Evidência:** E2. [chegada com Date.now:310][a05a]; [duração/métricas:84–141][a08].

O início da viagem usa `sessionData.start`, tratado em outros leitores como segundos; o fim é passado com `Date.now()`, em milissegundos. A duração subtrai os dois e divide por 60, misturando unidades. Métricas agregadas leem um total e depois escrevem a soma, sem transação/idempotência; concorrência ou replay pode perder incremento ou duplicá-lo. Falhas na atualização de métricas são capturadas sem impedir que a finalização pareça concluída. Campos mensais não possuem chave de mês nem rotação ativa encontrada nesse fluxo.

O fator fixo de CO₂ pressupõe substituição por automóvel em toda viagem. **Essa é uma decisão metodológica pendente**, não um fato observável do percurso. Distância estimada por Wi-Fi também não deve ser apresentada como medição exata sem informação de qualidade.

**Requisito:** tempos com unidade validada, agregados derivados de viagens únicas e reconstruíveis, período explícito, método e cenário publicados para indicadores ambientais.

**Aceite:** reprocessar uma viagem não altera o total; duas viagens simultâneas somam corretamente; duração é coerente; resumo mensal muda de período; impacto distingue estimativa e hipótese de substituição modal.

### A09 — P1/Decisão — Rotas, assinaturas, conversão paga e webhook não têm política completa de acesso

**Evidência:** E2. [comandos do bot][a09b]; [assinaturas:30][a09s]; [converter Functions:235][a06i]; [webhook:485][a09w]; [canal com localização:93][a05p].

Não foi encontrada autorização por função para consultar trajetos, seguir uma bike ou acionar conversões no conjunto examinado. A implementação Functions recebe updates no webhook sem conferir um segredo de origem no handler. O canal público pode publicar coordenadas de movimentos individuais; o comando de conversão pode gerar chamadas a um serviço cobrado sem limite de uso específico observado.

Tornar parte do bot pública pode ser intencional. O problema de produto é não haver uma política explícita separando disponibilidade pública, operação, trajeto individual e operações com custo. Nenhuma chamada a esses serviços foi feita nesta revisão.

**Requisito:** matriz de acesso, validação do webhook, limites de conversão e opção pública por agregação; decisões de publicação conscientes.

**Aceite:** visitante recebe apenas a visão pública; operação e trajetos obedecem à permissão aprovada; update sem origem válida é recusado; conversões respeitam cota e não se repetem por replay.

## 6. Observações e melhorias de arquitetura

| ID | Observação | Próximo passo |
|---|---|---|
| O01 — payload e memória | O estimador usa comprimento do SSID sem escape JSON; o limite final é 512, embora o orçamento anunciado seja 460. Metadados da central aumentam o payload. O orçamento de bytes do upload não equivale à memória de objetos ArduinoJson, e falha de montagem retorna zero candidatos, indistinguível de “sem dados”. | Validar tamanhos de serialização e pool no ESP32 com SSIDs escapados, valores máximos, ausência de NTP e documento denso. Conferir MTU efetivo por conexão; não apenas a flag de build. |
| O02 — segredos nos logs | Em erro de upload, `cloud_sync.cpp:476` imprime a URL montada com `?auth=` em `endpoints.cpp:8`. | Redigir token em todos os logs; manter correlação por lote/dispositivo sem credencial. Não foram copiados tokens reais para este relatório. |
| O03 — armazenamento auxiliar | `addConfigData("central", ...)` pode criar `central.bin`, mas a enumeração só aceita nomes com prefixo `bpr-`. Não foi encontrado chamador ativo de `saveSystemHeartbeat()`. | Ligar explicitamente a telemetria auxiliar ou remover o fluxo morto; não contar esse mecanismo como upload funcionando. |
| O04 — backup | Backup consolidado usa documento finito, não verifica overflow/bytes escritos, sobrescreve um único arquivo e não tem recuperação ativa encontrada. | Definir exportação/recuperação verificável; backup não substitui commit atômico nem recibo durável. |
| O05 — energia | Inferência de USB/carga pela tensão/ADC continua heurística; dev_mode padrão é true. A transferência com retry sem fim pode adiar a proteção de bateria. | Medir circuito real, brownout/reset reason, ADC e corrente; separar leitura inválida, bateria cheia e carregamento. F24 segue aberto. |
| O06 — estados e parâmetros | LOST não tem entrada no fluxo normal; encontra anúncio sem estabelecer conexão. Diversos getters de pendências retornam zero e parâmetros remotos não comandam os limites reais. O SDK obtido define máximo de três conexões NimBLE, enquanto o cadastro tem dez posições. | Publicar só capacidades implementadas; distinguir cadastro de conexões simultâneas e medir o limite; remover estados mortos ou definir seus consumidores. |
| O07 — atualização | A partição nova tem um único app factory. Não há dois slots para atualização OTA com rollback. | Escolher manutenção USB ou outra capacidade/estratégia de atualização antes de prometer OTA; qualificar migração e recuperação. |
| O08 — proveniência | O modelo que escreveu a correção não comprova nem invalida sua qualidade. Comentários “Fxx corrigido” e build aprovado também não encerram um critério funcional. | Vincular cada alteração à invariável, cenário de falha e evidência de aceite. |

**Melhorias de produto recomendadas:** painel de saúde e custódia dos dados; gestão de pendentes e revisão de configurações; pesquisa/exportação com qualidade de localização; alertas por mudança de estado; diagnóstico de energia; documentação de instalação/migração; orçamento de geolocalização; indicadores públicos agregados. O PRD complementar transforma essas propostas em requisitos e backlog, sem implementar código.

## 7. Validação executada nesta rodada

### 7.1 Fontes e ambiente

- Branch conferida contra referências remotas; HEAD revisado coincide com `origin/fixes/november`.
- Cópia isolada para revisão; `git diff --quiet`, `git status --porcelain` e `git diff --check` não indicaram alterações nos itens versionados.
- GCC 13.3.0, Linux, ArduinoJson **6.21.6** (`0b03321ed33fa6965114424fbb023260120c856b`).
- NimBLE-Arduino **1.4.3** (`e2b40749ef1e1aae9b1f5b2647d2bbadc5abb44f`) para leitura do contrato e reprodução reduzida de propriedade.
- Verificadores C++ preexistentes recompilados contra os fontes atuais. Não foram criados novos fontes de teste, correções ou patches do aplicativo. O dimensionamento de 254/263 bytes de R23 é um cálculo adicional do envelope, não uma das 23 verificações nem uma medição de rádio.
- Adaptadores locais de I/O, tempo, Wi-Fi, cliente BLE, fila e semáforo; compatibilidade de host com `f_str=c_str`.
- Instalação limpa do lockfile de `bot/functions`, sem scripts de instalação; verificação TypeScript com `--noEmit`.
- Nenhuma placa, conta de Telegram, API de geolocalização ou base Firebase de produção foi acessada.

### 7.2 Resultados dos verificadores nativos

Cada linha corresponde a uma das 23 verificações existentes. Resultado “falha reproduzida” não significa que um teste de aceite tenha passado.

| Nº | Verificação | Resultado observado |
|---|---|---|
| 1 | Scan com API assíncrona | `scan_async=1 collected=0` |
| 2 | Dez scans com uma rede | `ten_scans_fragments=11 central_capacity=10` |
| 3 | Falha no checkpoint | `failed_checkpoint=1 recovered_previous=0` |
| 4 | Configuração com escrita interrompida | `config_update_success=1 persistence_valid=0 ram_interval=42 reboot_interval=300` |
| 5 | Reaplicar mesma versão | `same_version_update=1 applied_version=43` |
| 6 | Perda persistente de ACK | `missing_ack_data_writes=10 disconnects=0 connected=1` |
| 7 | Limpeza legada sem correlação | `data_after_unscoped_proceed=0` |
| 8 | Fragmento idêntico após seis segundos | `same_fragment_success=1,1 persisted_items=2` |
| 9 | Décimo primeiro fragmento | `eleventh_fragment_stored=0 complete=0` |
| 10 | Escrita curta na central | `second_write_success=0 previously_acked_remaining=0` |
| 11 | Preparar um lote | `prepared_before_reset=1` |
| 12 | Pendências após preparo | `pending_after_preparation=0 stored_after_preparation=1` |
| 13 | Reler arquivos após preparo inconcluso | `next_boot_upload_candidates=0` |
| 14 | Acrescentar outro item | `prepared_after_new_data=1 total_stored=2` |
| 15 | Confirmar e verificar mutex | `mutex_still_taken_after_confirm=1` |
| 16 | Confirmar globalmente | `stored_after_global_confirm=0` |
| 17 | Baixar versão 42 | `config_download_success=1 downloaded_remote_version=42 stored_version=0` |
| 18 | Comparar status ok | `ack_ok_comparison=0` |
| 19 | Tratar ACK como erro | `config_pending_after_failed_ack=0` |
| 20 | Aceitar quatro divisores zero | `zero_led_durations_accepted=1` |
| 21 | Repetir identidade de lote após seis segundos | `same_batch_key_after_retry=0` |
| 22 | Verificador de completude com seq 10/total 11 | ASan/UBSan: acesso fora de `bool[10]`, `buffer_manager.cpp:224`. |
| 23 | Destruir callback estático conforme contrato 1.4.3 | ASan: liberação de endereço não alocado. |

### 7.3 Build ESP32 e partições

Os INIs fixam `espressif32@8.0.0`, NimBLE 1.4.3 e ArduinoJson 6.21.6. A tabela selecionada reserva app de `0x300000` bytes e LittleFS de `0xF0000`, terminando em 4 MiB. Isso corrige a seleção de layout e oferece mais espaço para aplicação; não valida, por si, migração nem execução.

Os dois projetos concluíram com **SUCCESS**, geraram `firmware.bin` e passaram na checagem de tamanho, usando PlatformIO Core 6.2.0 com telemetria desativada, plataforma 8.0.0, framework Arduino-ESP32 3.3.12 e os INIs da branch. Os percentuais informados no resumo das correções foram confirmados independentemente. Retries na obtenção de dependências não impediram os builds finais.

| Projeto | RAM estática indicada | Aplicação / partição | Percentual | Resultado |
|---|---|---|---|---|
| Bike | 96.564 / 327.680 bytes | 1.232.473 / 3.145.728 bytes | 39,2% | SUCCESS |
| Central | 45.812 / 327.680 bytes | 1.855.979 / 3.145.728 bytes | 59,0% | SUCCESS |

O build emitiu avisos de redefinição de macros de core/NimBLE, incluindo MTU preferido sobrescrito para 256. Esse aviso foi investigado em R23, inclusive no ELF da central. O SDK também define `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=3`; dez entradas de cadastro não comprovam dez conexões simultâneas. Não foram gravadas placas.

RAM estática e tamanho do binário não representam pico de heap/stack, qualidade do rádio ou autonomia. Testes nativos usam tamanhos de ponteiro/struct de host e não servem para medir esses limites do ESP32.

### 7.4 O que ainda exige execução

| Cenário | Evidência de aceite necessária |
|---|---|
| Coleta e rádio reais | Uma e várias redes, conclusão assíncrona, duração por canal/prazo total, reconexão e MTU observado. |
| Custódia | Cortes de energia durante gravação, ACK, preparo de lote, HTTP e confirmação; nenhuma perda de dado confirmado. |
| Capacidade | Sessões densas, backlog offline, central cheia, backpressure e coleta além de 50 scans. |
| Concorrência | Uma bike, depois várias até o limite efetivamente suportado pelo SDK; identidade e fila corretas. |
| Configuração | Duas bikes/revisões distintas, NACK, ACK antigo, retry, reboot e flash cheia. |
| Migração | IDs e arquivos legados; partição realmente instalada; exportação/recuperação sem formatação automática. |
| Energia | ADC contra multímetro, tensão sob pico de rádio, reset reason, corrente e carregamento. |
| Aplicativo | Normalizador com replay, build Functions, regras no emulador, unidades, rota e alertas históricos. |
| Endurance | Vários dias, heap/stack/fila limitados, contadores de perdas explícitos e reconciliação final. |

## 8. Ordem de correção recomendada

1. **Restabelecer coleta e execução:** R01, R10–R14 e R19. Sem isso, os demais testes ficam contaminados por falhas básicas.
2. **Garantir custódia de dados:** R02–R06, R12, R16/R17/R22. Limpar o buffer da bike só é aceitável quando a próxima camada pode sobreviver à falha.
3. **Concluir configuração/provisionamento:** R07–R09, R15, R18/R20/R23 e contrato de versões.
4. **Planejar atualização das placas:** R21 e F24; build/partições, identificação dos circuitos e manutenção.
5. **Integrar o aplicativo:** A01–A08; normalizador, contratos de tempo/bateria/rede, Functions e consumidores reprocessáveis.
6. **Qualificar acesso e operação:** A07/A09, limites de API, publicação pública e painel mínimo.

Um piloto controlado precisa de build reprodutível, sessão completa entregue, recuperação sob falha, configuração comprovadamente persistida e visibilidade do dado no consumidor. A liberação ampla depende também de migração e ensaio em placa. O PRD complementar define esses marcos e seus requisitos rastreáveis.

## 9. Referências de código e documentação

[r01]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/bici/src/scanning.cpp#L32
[r02b]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/bici/src/buffer_manager.cpp#L200
[r02c]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/include/constants.h#L47
[r02d]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/buffer_manager.cpp#L174
[r03]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/buffer_manager.cpp#L120
[r04c]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/buffer_manager.cpp#L511
[r04b]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/bici/src/buffer_manager.cpp#L68
[r05p]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/buffer_manager.cpp#L353
[r05s]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/buffer_manager.cpp#L259
[r06]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/cloud_sync.cpp#L431
[r07c]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/bike_manager.cpp#L658
[r07b]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/bici/src/config_manager.cpp#L189
[r08p]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/bike_pairing.cpp#L326
[r08m]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/bike_manager.cpp#L442
[r09]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/bici/src/config_manager.cpp#L63
[r09a]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/bici/src/at_base.cpp#L364
[r10]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/buffer_manager.cpp#L370
[r10l]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/buffer_manager.cpp#L21
[r11]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/ble_server.cpp#L190
[r12]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/buffer_manager.cpp#L219
[r13]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/bici/src/at_base.cpp#L251
[r13n]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/bici/src/at_base.cpp#L371
[r13h]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/bpr_json_helper.cpp#L50
[r14c]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/config_manager.cpp#L94
[r14l]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/led_controller.cpp#L46
[r14b]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/led_controller.cpp#L140
[r15]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/bike_manager.cpp#L134
[r15p]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/bike_manager.cpp#L249
[r15s]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/cloud_sync.cpp#L73
[r16m]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/bici/src/main.cpp#L56
[r16s]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/bici/src/scanning.cpp#L60
[r16t]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/bici/include/constants.h#L46
[r17]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/bici/src/buffer_manager.cpp#L34
[r17f]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/bici/src/buffer_manager.cpp#L294
[r18d]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/ble_server.cpp#L93
[r18c]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/ble_server.cpp#L150
[r19q]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/bike_pairing.cpp#L17
[r19p]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/bike_pairing.cpp#L365
[r19b]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/ble_server.cpp#L432
[r20b]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/bici/src/config_manager.cpp#L42
[r20c]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/config_manager.cpp#L59
[r20m]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/main.cpp#L242
[r20s]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/cloud_sync.cpp#L246
[r20p]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/config_ap.cpp#L308
[r21id]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/bici/src/utils.cpp#L11
[r21bin]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/include/binary_structs.h#L66
[r21part]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/partitions.csv
[r21fs]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/main.cpp#L197
[r22]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/bici/src/buffer_manager.cpp#L101
[r23c]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/ble_server.cpp#L187
[r23s]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/ble_server.cpp#L325
[a01e]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/endpoints.cpp#L57
[a01b]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/bot/src/config/firebase.js#L31
[a01f]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/bot/functions/src/index.ts#L161
[a01w]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/web/app/root.jsx
[a02b]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/bot/src/services/bikeMonitor.js#L120
[a02g]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/bot/src/services/geolocation.js#L10
[a03c]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/central/src/cloud_sync.cpp#L512
[a03b]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/bot/src/services/stationMonitor.js#L41
[a04f]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/bot/src/config/firebase.js#L176
[a04b]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/bot/src/services/bikeMonitor.js#L56
[a04p]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/firmware/bici/src/power_manager.cpp#L89
[a05b]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/bot/src/services/bikeMonitor.js#L90
[a05a]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/bot/src/services/bikeMonitor.js#L308
[a05r]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/bot/src/services/rideCalculator.js#L11
[a05p]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/bot/src/services/channelPublisher.js#L79
[a06t]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/bot/functions/tsconfig.json
[a06i]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/bot/functions/src/index.ts#L235
[a06p]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/bot/functions/package.json
[a07]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/bot/database.rules.json
[a07f]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/bot/firebase.json
[a08]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/bot/src/services/rideCalculator.js#L84
[a09b]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/bot/src/index.js#L104
[a09s]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/bot/src/services/subscriptionManager.js#L30
[a09w]: https://github.com/Ameciclo/bpr-sistema/blob/09dc9b150a1d60648d4cb440b4ec89189af3f3e2/bot/functions/src/index.ts#L485
[wifi-api]: https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html
[firebase-rules]: https://firebase.google.com/docs/database/security
[nimble-h]: https://github.com/h2zero/NimBLE-Arduino/blob/1.4.3/src/NimBLEServer.h#L53
[nimble-d]: https://github.com/h2zero/NimBLE-Arduino/blob/1.4.3/src/NimBLEServer.cpp#L56
[nimble-deinit]: https://github.com/h2zero/NimBLE-Arduino/blob/1.4.3/src/NimBLEDevice.cpp#L939
[nimble-notify]: https://github.com/h2zero/NimBLE-Arduino/blob/1.4.3/src/NimBLECharacteristic.cpp#L467
[nimble-att]: https://github.com/h2zero/NimBLE-Arduino/blob/1.4.3/src/nimble/nimble/host/src/ble_att.c#L589
[nimble-cfg]: https://github.com/h2zero/NimBLE-Arduino/blob/1.4.3/src/nimble/esp_port/port/include/esp_nimble_cfg.h#L282
