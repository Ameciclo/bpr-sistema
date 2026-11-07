# BPR — Revalidação das correções de novembro

**Revisão 2 — 7 de novembro de 2026, America/Recife.**  
**Repositório:** [Ameciclo/bpr-sistema](https://github.com/Ameciclo/bpr-sistema).  
**Commit verificado:** `23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2`, branch `fixes/november`.  
**Comparação:** `09dc9b150a1d60648d4cb440b4ec89189af3f3e2` → `23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2`: 92 arquivos, +5502/−1936, incluindo documentação e verificadores.  
**Escopo:** conferir as correções R01–R23/A01–A09/O01–O08, sua integração e regressões.  
**Modalidade:** análise e documentação. Nenhum arquivo versionado do aplicativo foi alterado; nenhum commit, PR, deploy ou gravação em placa foi realizado.

## 1. Parecer

**As correções avançaram bastante, mas ainda não está tudo certo. Recomendo corrigir os bloqueios abaixo antes de qualificar o conjunto para uso nas placas.**

Os dois firmwares compilam; os erros anteriores de TypeScript foram resolvidos; os verificadores fornecidos apresentam os resultados esperados nos cenários que exercitam. Foram corrigidos, entre outros, o consumo nominal do scan assíncrono, a comparação textual do ACK, a liberação do mutex na confirmação, a propriedade do callback estático e a deduplicação do mesmo fragmento recebido em outro instante.

Encontrei, entretanto, um bloqueio novo no boot: **`BufferManager::load()` da bike reserva 54.432 bytes de pilha em uma tarefa configurada com 8.192 bytes**. Isso foi conferido no ELF compilado, não estimado a partir do host. A reserva acontece antes da primeira consulta ao arquivo. O resultado em placa pode ser falha ou corrupção de memória; não foi observado um crash físico nesta revisão.

Também reproduzi a interrupção da coleta após dez scans, a geração de 501 fragmentos para uma sessão válida frente ao limite central de 256, a recuperação de amostras de boots diferentes sob o mesmo rótulo temporal, a remontagem incorreta de scans densos e a perda de coordenadas no replay. Nos agregados, duas finalizações da mesma viagem produziram uma viagem salva e contador igual a dois.

O ganho de qualidade do commit é real. A afirmação de que os achados foram integralmente encerrados ainda não se sustenta: existem critérios não cobertos pelos testes e regressões introduzidas nas próprias correções.

### Resumo das verificações independentes

| Verificação | Resultado nesta revisão |
|---|---|
| Build ESP32 da bike | SUCCESS — app 1.234.275 / 3.145.728 B, 39,2%; RAM estática 96.572 B. |
| Build ESP32 da central | SUCCESS — app 1.909.063 / 3.145.728 B, 60,7%; RAM estática 45.772 B. |
| TypeScript Functions, build e desenvolvimento | Zero diagnósticos em ambos. |
| Lint Functions | Zero erros; dez avisos em `local-bot.ts`. |
| Sintaxe JS do bot | Nove arquivos verificados, sem erro de sintaxe. |
| Verificadores C++ fornecidos | 26 saídas da bike + 26 da central; sem `BUG` e sem achados de ASan/UBSan nos cenários executados. |
| Experimentos adicionais com fontes reais | Oito observações de falha detalhadas na seção 5. |
| Inspeção do ELF da bike | Frame de 54.432 B em `load`; função de tamanho da tarefa retorna 8.192 B. |
| Placas, rádio e serviços reais | Não acessados. |

## 2. Critério de evidência e prioridade

- **E1:** reprodução local nos fontes atuais, saída do compilador ou inspeção do binário ESP32.
- **E1-S:** fonte atual executado com adaptadores locais de filesystem, SDK ou banco. Demonstra o comportamento lógico do cenário, sem representar flash/radio/Firebase reais.
- **E2:** falha ou limitação identificada pelo código e fluxo de chamadas, sem reprodução nesta rodada.
- **P0:** bloqueia o funcionamento básico ou sua qualificação. **P1:** integridade, recuperação ou comportamento operacional grave. **P2:** risco condicionado, manutenção ou diagnóstico.

Os estados “corrigido no caso verificado” abaixo não substituem teste em placa. Também não se deve interpretar uma implementação adiada por decisão de produto como uma correção já concluída.

## 3. Situação dos achados anteriores

### Firmware e protocolo

| ID | Situação em 23b13aa | Evidência / pendência |
|---|---|---|
| R01 — scan assíncrono | Correção nominal confirmada, rádio pendente | Verificador obtém um scan com três redes após quatro updates. O mesmo parâmetro ainda é usado por canal e como prazo global; `scanDelete()` não comprova cancelamento do scan físico. |
| R02 — capacidade/completude | Parcial | Onze fragmentos agora cabem e completam. O limite 256 não cobre o caso válido de 501 fragmentos: N06. Completude continua baseada no spool presente. |
| R03 — CRC com horário de recepção | Corrigido no caso verificado | Reenvio idêntico após seis segundos mantém um item lógico; a recepção não altera mais o payload. Conflito de conteúdo para uma mesma identidade ainda precisa de política. |
| R04 — perda na gravação | Aberto | Temporários protegem contra falha inicial de escrita, mas o original é removido antes do rename: N03. Há outra perda no tratamento de corrupção: N08. |
| R05 — retomada/confirmar upload | Parcial | Manifesto e confirmação exata funcionam no caminho testado. Erros de gravação do manifesto e de atualização de flags não são propagados: N07. |
| R06 — identidade do lote | Corrigido para o mesmo conjunto testado | Chave não depende mais do relógio e se repete para o mesmo conjunto. Manifesto, payload e replay ainda têm limitações de N05/N07. |
| R07 — revisão de configuração | Parcial | Mesma versão/mesmos valores e rejeição de versão antiga funcionam; a central passa a revisão. Revisão maior com valores iguais é recusada: N09. |
| R08 — comparação e ACK divergente | Correção constatada | Comparação textual e conferência da revisão esperada presentes. Verificador nativo novo não exercita o fluxo BLE/`BikeManager` completo. |
| R09 — ACK sem persistência | Parcial | Save retorna bool e falha inicial preserva a configuração no teste; a janela destrutiva de rename permanece: N03. |
| R10 — mutex | Corrigido no caso verificado | Confirmação seguida de outra operação mantém o mutex disponível. Aquisição falha agora interrompe a região protegida. |
| R11 — callback estático | Correção constatada | `setCallbacks(..., false)` está presente; build aprovado. Ciclos de rádio/AP exigem placa. |
| R12 — índice fora do array | Corrigido no caso verificado | Limites de seq/total aplicados; caso anterior não produz erro de sanitizador. Isso não resolve a capacidade máxima de N06. |
| R13 — retry/busy/proceed | Correções constatadas | Sucesso da escrita não zera retries, há espera por busy, destinatário e remoção de proceed. Cenários BLE reais ainda pendentes. |
| R14 — divisores zero | Corrigido no caso verificado | Atualização com durações zero é recusada; guardas defensivas acrescentadas ao LED. |
| R15 — cadastro/provisionamento | Parcial | Pendentes são publicados; slots pending podem ceder lugar; há política de omissão em snapshot não vazio. Lista cheia sem pending e retorno de merge ignorado continuam relevantes: seção 6. |
| R16 — tempo após reboot | Aberto | Índice interno melhora a seleção, mas não é transmitido; o boot antigo é restaurado e novos scans usam o relógio novo: N04. |
| R17 — saturação | Regressão no fluxo real | A política explícita de parar em 50 scans passou a parar em dez devido à bateria: N02. Rotação de sessões continua adiada. |
| R18 — identidade por conexão | Correções constatadas | Troca de ID e ID ocupado são rejeitados; configuração confere vínculo e autorização. Qualificação com várias conexões pendente. |
| R19 — concorrência/filas | Parcial | Fila de eventos e mutex dos mapas são melhorias. `priorityQueue` continua sem limite e há consulta de autorização no callback BLE: seção 6. |
| R20 — configuração candidata | Parcial | Leitura curta e CSV incompleto melhoraram. Load de conteúdo inválido, rejeição parcial e erro ao salvar credenciais ainda deixam inconsistências: N09. |
| R21 — migração | Planejada, não demonstrada | Autor informa que o formato v2 não foi usado em produção; não há inventário verificado. Autoformatação segue no boot. Não tratar essa afirmação documental como teste de atualização. |
| R22 — integridade da sessão | Mecanismo confirmado; nova regressão | CRC e quarentena funcionam no host, mas a candidata de 54 KB é automática e causa N01. |
| R23 — MTU | Correções constatadas | Central chama setMTU em runtime e confere peer MTU antes da notificação. MTU menor gera recusa explícita; não há fragmentação de configuração nesse caminho. Negociação real pendente. |

### Aplicativo e observações

| ID | Situação em 23b13aa | Evidência / pendência |
|---|---|---|
| A01 — normalizador/dashboard | Parcial | Trigger de lotes existe e produz `sessions_v2`; N05/N10 impedem considerar o fluxo integrado. Dashboard continua inicial. |
| A02 — DTO de redes/scans | Correção do DTO constatada | Arrays/objetos são normalizados. Remontagem da observação e qualidade temporal seguem incorretas: N04/N05. |
| A03 — heartbeat s/ms | Parcial | Conversão corrigida; estado desconhecido não substitui um estado antigo já guardado: seção 6. |
| A04 — bateria | Parcial, com regressão | Produtor passou a existir e percentual é separado de tensão. Novo produtor ativa saturação precoce N02; ADC/carga não foram qualificados. |
| A05 — replay/eventos | Aberto/parcial | Dedupe durável de alertas permanece adiado; seleção de sessão, eventos de chegada e métricas continuam com N10/N11. |
| A06 — build TS/entrypoints | Corrigido nos checks executados | Build/dev sem diagnósticos, lint sem erros e sintaxe JS válida. |
| A07 — regras | Parcial | Escrita exige autenticação, mas não há isolamento por papel/base; bikes, bases e rotas mantêm leitura pública. Regras implantadas não foram consultadas. |
| A08 — tempo/agregados | Aberto/parcial | Transações e período foram acrescentados; N04/N11 reproduzem unidades incorretas, dupla contagem e NaN no primeiro resumo. |
| A09 — acesso/custo/webhook | Parcial | Segredo do webhook é conferido quando configurado; cooldown é por instância em RAM. Matriz de acesso e orçamento global permanecem pendentes. |
| O01 — dimensionamento | Parcial | Contagem de escapes melhorou, mas o limite de fragmentos não acompanha o pior caso: N06. Memória JSON em placa ainda exige medição. |
| O02 — URLs com segredo | Correção constatada | Redação de URL foi acrescentada aos erros HTTP examinados. Não foram usados ou copiados tokens reais. |
| O03 — fluxo morto | Correção constatada | Helpers de dados da central/UploadQueue mortos foram removidos. |
| O04 — backup | Parcial | Cópia bruta confere bytes, mas retenção/limpeza ocorre no begin e não limita crescimento entre lotes; recuperação automática não demonstrada. |
| O05 — energia | Parcial | Dev mode padrão false e retry melhorado. Consumo, divisor, ADC, carregamento e autonomia exigem bancada. |
| O06 — estados/getters | Correções constatadas | LOST morto removido; getters de pendências calculam dados reais. Isso não implementa o PRD de busca por SSID. |
| O07 — OTA | Decisão mantida | Um app factory; atualização USB. Nenhum novo mecanismo OTA foi prometido nesta revisão. |
| O08 — evidências | Melhorou, com limite | Fontes de teste e resultados foram versionados. O runner não transforma valores impressos em asserções; cobertura adicional é necessária. |

## 4. Achados desta revalidação

### N01 — P0 — Recuperação da sessão excede a pilha da tarefa no próprio binário

**Evidência:** E1, fonte e ELF ESP32. [Candidata local em load:154][n01]; [chamada incondicional no setup:81][n01m].

O novo `SessionData candidate` contém a sessão inteira, aproximadamente 54 KB, e é uma variável automática. O desmonte do firmware aprovado mostra duas reduções de SP na entrada de `BufferManager::load()`: 1.184 + 53.248 = **54.432 bytes**. A reserva antecede a consulta `LittleFS.exists`, portanto não depende de já existir sessão salva.

No mesmo ELF, `getArduinoLoopTaskStackSize()` retorna `0x2000` = **8.192 bytes**. O core executa setup e loop nessa tarefa. O header do SDK também declara 8.192. A RAM estática de 29,5% não mede esse uso da pilha.

**Efeito:** escrita/leitura fora da área reservada à tarefa, com risco de panic, reset ou corrupção de outro estado já no boot. Não executei uma placa; a incompatibilidade do frame com o orçamento foi demonstrada no binário gerado.

**Aceite:** validar a sessão sem uma candidata automática desse tamanho; tratar falha de alocação quando aplicável. Inspecionar uso de pilha do alvo e iniciar com/sem arquivo, válido/corrompido, na placa real.

### N02 — P1 — O produtor de bateria faz a coleta parar após dez scans

**Evidência:** E1-S. [addBattery no caminho de scan:128][n02s]; [isFull:361][n02f]; [MAX_BATTERY:21][n02c].

Cada scan agora adiciona uma leitura de bateria. `MAX_BATTERY` continua dez e `isFull()` usa “50 scans **ou** dez leituras”. `ScanningState` deixa de varrer assim que isFull retorna true. O teste fornecido que chega a 50 chama addScan diretamente e não percorre esse produtor de bateria.

**Reprodução com o ScanningState real:** doze ciclos de coleta, com três redes oferecidas pelos stubs → `scans=10 bateria=10 cheio=1`. No intervalo padrão de cinco minutos, isso limita a coleta a aproximadamente cinquenta minutos nominais, antes de considerar o tempo das operações.

**Aceite:** armazenamento do histórico de bateria não deve encerrar prematuramente a capacidade declarada de scans. Exercitar o fluxo de coleta completo além de dez ciclos e conferir os dois contadores e a política de saturação.

### N03 — P1 — Remover o original antes do rename mantém a janela de perda

**Evidência:** E2. [sessão da bike:118][n03s]; [config da bike:98][n03b]; [config da central:106][n03c]; [reescrita do spool:718][n03d].

O temporário é escrito, mas a operação seguinte remove o arquivo canônico e só depois tenta renomear. Se faltar energia entre as operações, o arquivo canônico não existe e o boot não recupera automaticamente o temporário. Se rename falhar, o código remove também o temporário. A mensagem “previous file kept” não descreve esse caminho.

Os testes fornecidos só fazem a abertura/escrita falhar antes de remover o original; o adaptador de rename sempre funciona quando a origem existe. Por isso eles não demonstram atomicidade da substituição.

**Aceite:** substituição recuperável que preserve pelo menos uma cópia íntegra, com recuperação de temporário/journal no boot e comportamento comprovado do LittleFS. Simular falha de rename e corte entre remoção/commit, incluindo configurações e itens já confirmados à bike.

### N04 — P1 — Boot novo recebe rótulo antigo; uptime é convertido em data Unix

**Evidência:** E1-S/E2. [beginBoot e load:46/193][n04b]; [fragmento transmitido:295/327][n04f]; [projeção:327–333][n04p]; [toMs:15][n04r].

O boot é gerado no setup, mas load substitui `bootId` pelo ID da sessão recuperada. A sessão continua recebendo scans com o millis do boot novo. Há um ID por sessão, não por amostra/origem, e o índice usado internamente para selecionar o scan não é transmitido no JSON.

**Reprodução:** boot anterior 40273; novo boot gerado 8107; após recuperação, rótulo 40273. Scans de timestamps 900000 e 1000 ficam na mesma sessão/origem. A troca da busca interna por índice evita selecionar a rede do primeiro timestamp, mas não resolve a origem temporal dos dados entregues.

Na nuvem, `session_start_millis` vira `session_start_ms` e é usado como data absoluta. No bot, toMs interpreta número pequeno como segundos, embora o campo v2 declare ms; o chamador e startRide ainda podem aplicar essa conversão duas vezes.

**Reprodução da função real:** entrada de 10.000 ms → primeira conversão 10.000.000 → segunda 10.000.000.000. Mesmo sem essa multiplicação, millis desde o boot não é uma data Unix de coleta. A notificação das Functions usa diretamente new Date desse uptime.

**Aceite:** origem de boot por amostra ou sessões separadas na retomada; scan com identidade transmitida; unidade explícita sem heurística repetida. Horário relativo permanece relativo até haver ancoragem confiável, com qualidade/ausência visível.

### N05 — P1 — Normalizador não reúne chunks do scan e apaga coordenadas no replay

**Evidência:** E1-S. [remontagem/publicação:298–334][n05].

O normalizador ordena os chunks por ts/offset e os acrescenta ao array, mas não os reúne em uma observação. Um scan denso dividido em dois fragmentos vira dois scans incompletos. O scan_count declarado pode dizer um enquanto scans.length diz dois; consultas de localização passam a receber subconjuntos das redes de uma mesma coleta.

**Reprodução da função original transpilada em memória, com banco local:** um scan declarado, dois chunks com offsets 0/1 e uma rede em cada → dois registros, cada um com uma rede. Não houve chamada a Firebase ou Google reais.

Além disso, toda remontagem faz update do array scans completo. Coordenadas que o conversor gravou dentro dos scans são substituídas pela nova projeção. **Reprodução:** adicionar coordenadas ao primeiro scan e repetir o mesmo lote → `coordenadas_preservadas_apos_replay=false`.

**Aceite:** reunir todas as redes por uma identidade de scan estável, validando offsets e completude. Projeção bruta e enriquecimento devem permitir replay sem destruir coordenadas/revisões já calculadas. Uma coleta vira uma observação, independente do número de fragmentos.

### N06 — P1 — A sessão máxima pode ter 501 fragmentos, não até 256

**Evidência:** E1-S. [estimador/chunks:234–281][n06b]; [limite central e completude][n06c].

O cálculo de capacidade considera aproximadamente cinco chunks por scan. Um SSID válido com 32 aspas exige escape JSON e ocupa mais espaço; com vinte redes por scan, o próprio fragmentador gera dez chunks por scan.

**Reprodução nos fontes reais:** 50 scans × 20 redes com SSID de 32 aspas → **501 fragmentos**, incluindo o final. A central aceita no máximo 256 itens por bike e rejeita completude com total maior que 256.

O caso usa a capacidade de 50 scans declarada pelo produto e aceita por addScan. O fluxo normal atualmente para antes por N02; corrigir essa regressão não resolverá este limite de transporte.

**Aceite:** calcular capacidade pelo pior caso serializado suportado ou adotar janela com recibos duráveis. Esse perfil deve concluir a transferência, ou ser recusado previamente por uma política explícita e consistente, sem uma sessão legitimamente coletada ficar sem entrega possível.

### N07 — P1 — Manifesto e flags ainda podem ser tratados como duráveis sem gravação válida

**Evidência:** E2. [writeManifest:550–571][n07m]; [flags:540–543][n07f]; [rollback:727–760][n07r].

`writeManifest()` só confere a abertura. Não confere overflow, bytes serializados/escritos nem releitura; retorna true após a tentativa. As chamadas que marcam `uploaded` ignoram o retorno de setItemUploadedFlag. Rollback também ignora falhas de atualização e pode remover o manifesto mesmo sem restaurar as flags.

Uma escrita parcial do manifesto seguida de flags persistidas pode deixar itens marcados como enviados e um manifesto ilegível. Na retomada, readManifest falha; rollback retorna false; reconcile não tem um plano alternativo e a seleção ignora os itens enviados. A existência do arquivo não comprova um manifesto íntegro.

**Aceite:** manifesto recuperável, validado e completo antes de alterar flags; propagar cada falha; reconciliar estado inconcluso/corrompido sem deixar item fora de envio e sem eliminar o último comprovante necessário.

### N08 — P1 — Iteração sobre arquivo corrompido informa sucesso e permite descartar o restante

**Evidência:** E2. [forEachItem:368–393][n08i]; [reescrita/validação:670–715][n08r].

Se um registro está truncado, tem tamanho inválido ou CRC errado, forEachItem interrompe o loop com break, mas termina retornando true. O contrato do header diz que true representa arquivo válido. rewriteBikeBufferExcluding usa esse resultado para autorizar a substituição/remoção do arquivo.

**Cenário de fluxo:** registros A, B e C; B está corrompido e C ainda é válido. O upload pode selecionar A antes de encontrar B. Após confirmar A, a reescrita exclui A, para em B, recebe sucesso da iteração e pode concluir kept=0, removendo o buffer operacional com B e C. C não foi incluído no lote. O backup não tem recuperação automática nesse fluxo.

**Aceite:** erro de leitura/integridade é propagado e impede substituir o original por uma cópia parcial apresentada como completa. Preservar/quarentenar o arquivo e os registros válidos recuperáveis, inclusive depois do ponto corrompido.

### N09 — P1 — Revisões sem mudança e rejeições de configuração ainda são inconsistentes

**Evidência:** E2. [processUpdate:145–176][n09b]; [applyUpdate:213–234][n09a]; [load central:68–83][n09c]; [portal:274–283/387–396][n09p].

Há casos importantes fora dos testes novos:

1. Uma revisão maior com os mesmos valores é recusada por `!configChanged` antes de assumir a revisão remota. O servidor pode republicar uma configuração equivalente e ficar com pendência permanente, embora esse seja um avanço válido de revisão.
2. Campos inválidos da bike podem ser silenciosamente ignorados, enquanto outros campos válidos são aplicados e a revisão é confirmada. Exemplo por leitura: intervalo válido junto de max_networks zero; o zero é ignorado, o intervalo muda e a candidata pode passar. A validação final também não limita max_networks a vinte.
3. Na central, load de tamanho correto mas conteúdo inválido atribui a candidata ao estado ativo e apenas retorna isConfigValid. O setup que anuncia salvar padrões não reconstrói esses padrões nesse caminho.
4. O portal protege contra campo tardio inválido, mas atribui as credenciais ativas antes de saveCredentials. Em falha de persistência, responde erro sem restaurar a RAM anterior. O arquivo de credenciais continua sendo regravado diretamente.

**Aceite:** revisão maior/equivalente persiste a nova revisão e confirma; campo inválido rejeita a operação inteira; qualquer load inválido restaura configuração válida; HTTP de erro e falha ao salvar preservam RAM e última versão recuperável.

### N10 — P1 — Bot não identifica sempre a nova sessão e chegada ainda usa o schema antigo

**Evidência:** E2. [seleção de sessões:44–48/112–115][n10s]; [dedupe de scan:139–144][n10d]; [listener de conexão:77][n10c].

O bot e consultas escolhem a maior chave lexicográfica `{millis}-{salt}` como sessão mais recente. A chave não é cronológica: `9999-1` ordena depois de `10000-1`, embora, no mesmo boot, 10000 seja posterior. Após reinício a relação fica ainda menos válida. Uma sessão nova pode deixar de ser processada ou mostrada.

O comentário do dedupe afirma incluir sessão, mas a chave segue `scan_${bikeId}`. Uma sessão com timestamp menor que o da anterior tem seus scans recusados. O conjunto que registra listeners impede duplicar a mesma assinatura, mas não as remove ao longo das sessões.

Além disso, o listener de chegadas ainda lê `bikeData.sessions`/connections. A projeção nova escreve `sessions_v2` sem produzir esses eventos de conexão. Não foi identificado um produtor novo que feche esse caminho; a viagem iniciada pelo novo consumidor pode ficar sem finalização por chegada.

**Aceite:** consumir cada sessão por identidade/evento, com cursor/ordenação próprios; dedupe inclui sessão/scan. Chegada e encerramento devem usar um contrato ativo de ponta a ponta. Testar fronteiras de quantidade de dígitos, reboot, atraso e reprocessamento.

### N11 — P1 — Transações dos contadores não tornam a viagem idempotente; primeiro resumo gera NaN

**Evidência:** E1-S. [exists/set/métricas:105–120][n11r]; [estatísticas:161–189][n11s].

O fluxo verifica se a viagem existe e depois escreve. Duas execuções concorrentes podem observar ausência, salvar no mesmo caminho e incrementar os contadores duas vezes. Transações tornam cada incremento atômico, mas não o associam ao processamento único da viagem.

**Reprodução das funções reais com banco transacional local:** duas finalizações concorrentes → **uma viagem salva, contador de viagens igual a dois**.

No banco inicialmente vazio, updatePublicStats cria primeiro o ramo monthly e depois faz transação no pai public_stats. O objeto pai já existe, mas os contadores gerais não. `undefined + 1` e somas equivalentes geram NaN. **Reprodução:** `primeiro_resumo_gera_NaN=true`. O adaptador local expõe o objeto proposto; validação/erro do SDK remoto não foi executado. Em produção esse objeto não é uma estatística válida.

Falhas de agregação são capturadas e não garantem retomada: salvar a viagem antes do agregado faz um replay encontrar a viagem existente e pular a tentativa de reparo.

**Aceite:** uma identidade de viagem contribui uma única vez, inclusive em concorrência e retry; inicialização tolera ramos parciais; agregação interrompida é reconciliável sem duplicar ou esquecer contribuição.

## 5. Validação, resultados e limites

### Ambiente

- Checkout isolado do SHA completo informado pelo usuário, conferido com a branch remota.
- PlatformIO Core 6.2.0, `espressif32@8.0.0`, Arduino-ESP32 3.3.12, NimBLE 1.4.3, ArduinoJson 6.21.6, conforme INIs e dependências obtidas.
- Builds completos de bike e central, incluindo checagem de tamanho e firmware.bin.
- Instalação do lockfile das Functions sem scripts de instalação; tsc sem emissão nos projetos normal/dev; lint. Nove fontes JS checados apenas quanto à sintaxe.
- Verificadores fornecidos compilados com GCC 13.3 e ASan/UBSan. LeakSanitizer foi desabilitado porque não consegue examinar `/proc` nesse executor; ASan e UBSan continuaram ativos. Isso não é uma validação de vazamentos.
- Experimentos da bike chamam os métodos dos próprios fontes compilados em biblioteca compartilhada, usando os stubs fornecidos. Não foi criado ou editado fonte C++ de teste/aplicativo.
- Experimentos do normalizador executam o fonte TypeScript atual transpilado em memória; métricas executam o JS original. Banco e transações são adaptadores locais; HTTP e envio de Telegram ficam bloqueados. Nenhuma conta ou serviço real é chamado.
- Objetos automáticos grandes são tolerados pelo host e por isso exigem inspeção da pilha do binário alvo, realizada em N01.

### Verificadores fornecidos

Foram obtidas 26 linhas de resultado em cada executável, totalizando **52 saídas**. Nenhuma contém BUG e os dois logs de sanitizador estão vazios após o ajuste do LeakSanitizer. Os números impressos nos cenários de dedupe, confirmação, versão e rejeição correspondem aos documentados no commit.

O documento do autor menciona 53 checagens; os dois executáveis emitem 52 linhas e o script ainda tem uma mensagem final de sucesso. Isso não deve ser apresentado como 53 asserções independentes. O runner não verifica automaticamente os valores nem falha só por imprimir `no(BUG)`; ele precisa de expectativas/asserts explícitos para ser um bloqueio confiável de regressão.

| Cenário fornecido | Resultado repetido nesta revisão |
|---|---|
| Scan assíncrono | Quatro updates; um scan com três redes. |
| Capacidade antiga de onze fragmentos | Todos armazenados; sessão completa. |
| Mesmo fragmento após seis segundos | Sem duplicação do fragmento. |
| Erro inicial de escrita | Falha reportada; cópia anterior permanece nesse cenário. |
| Lote preparado e begin/reconcile | Itens voltam a pendente; mesma identidade de lote. |
| Confirmar quatro itens e deixar os demais | Quatro removidos; extra e nove pendências sobrevivem; mutex disponível. |
| Sequência 10/total 11 isolada | Sem acesso fora do array; incompleta. |
| Configuração repetida, antiga ou conflitante | Resultados esperados nos valores exercitados. |
| CSV incompleto, sem revisão e divisores zero | Rejeitados. |
| Corrupção de um byte na sessão | CRC detecta; cópia vai a session.corrupt. |

### Experimentos adicionais

| Nº | Método / entrada | Resultado observado | Relação |
|---|---|---|---|
| E01 | ScanningState original, doze ciclos, três redes. | Dez scans, dez leituras de bateria, cheio=true. | N02 |
| E02 | Fragmentador original, cinquenta scans de vinte SSIDs com 32 aspas. | 501 fragmentos; limite central 256. | N06 |
| E03 | Sessão salva, beginBoot novo, load e novo scan. | Boot gerado 8107 substituído por 40273; timestamps 900000/1000 na mesma origem. | N04 |
| E04 | Normalizador original, um scan em dois chunks. | scan_count=1; array com dois scans de uma rede cada. | N05 |
| E05 | Coordenadas no scan e replay do lote. | Coordenadas deixam de existir. | N05 |
| E06 | Função toMs original sobre campo de 10.000 ms. | 10.000.000 e, em segunda passagem, 10.000.000.000. | N04 |
| E07 | Duas finalizações concorrentes da mesma viagem. | Uma viagem persistida, contador dois. | N11 |
| E08 | Primeiro updatePublicStats com banco vazio. | Contador geral proposto é NaN. | N11 |

### Inspeção do alvo

O ELF da bike confirma reserva de **54.432 bytes** no início de load e tamanho de tarefa **8.192 bytes**. O SDK define o mesmo orçamento; setup executa nessa tarefa. Esta é evidência sobre o código realmente compilado para o ESP32, mas o sintoma físico exato continua pendente de placa.

Os tamanhos de binário medidos diferem em poucos bytes dos informados pelo autor e mantêm os percentuais de 39,2%/60,7%. Build aprovado comprova tradução/link e encaixe na partição; não comprova integridade de dados, uso seguro da pilha, autonomia ou rádio.

## 6. Outros pontos ainda relevantes

| Tema | Constatação e próximo aceite |
|---|---|
| Índices do manifesto | ManifestItems é ordenado, mas manifestIndexes não é reordenado junto. Se a enumeração de bikes não estiver na mesma ordem, flags podem ser aplicadas em índices associados a outra bike. E2 condicionado à ordem de enumeração; não reproduzido em LittleFS real. Manter identidade/índice unidos e testar enumeração fora de ordem. |
| Backup e espaço | Há cópia do conjunto antes de cada confirmação de lote; cleanup é chamado em begin. Muitos lotes podem acumular snapshots antes da limpeza. Testar backlog alto e espaço para temporário + backup + spool, com limite durante o sync. |
| Cadastro cheio | O merge só desocupa pending. Dez slots antigos allowed/blocked podem impedir inserir um ID novo antes da revogação. O retorno false de merge não é usado para reprovar o download em CloudSync. Testar troca de frota e expor falha de aplicação. |
| Autorização/filas | Evento é enfileirado no loop, mas ConfigCallbacks ainda consulta BikeManager::canConnect na task BLE. PriorityQueue continua sem capacidade/dedupe definidos. Não foi reproduzida corrida; a serialização integral anunciada ainda precisa ser demonstrada. |
| Horário desconhecido da estação | Ao receber heartbeat inválido depois de um estado conhecido, o código não grava null no mapa. Pode preservar o online anterior. Atualizar explicitamente o estado desconhecido e testar essa transição. |
| Prazo de scan | O timeout continua sendo também max_ms_per_chan; o prazo global pode expirar antes do scan completo. scanDelete libera resultados, não demonstra parada do driver. Homologar conclusão/cancelamento real e coordenação Wi-Fi/BLE. |
| Política de acesso | Auth obrigatório na escrita é avanço. Não substitui escopo por dispositivo/base/papel. Leitura pública e webhook sem segredo configurado seguem escolhas a fechar, sem alegar que o ambiente implantado foi inspecionado. |
| Atualização de placas | Não foi verificada a afirmação de que o formato v2 não chegou a produção. Se for apenas protótipo sem dados instalados, a necessidade dessa migração específica muda. Inventário, IDs e partições reais continuam necessários antes de atualização com dados. |
| Busca e localização | A análise consolidada e os PRDs posteriores descrevem propostas. Esse commit não implementa o protocolo público BPR-BUSCA/BPR-AQUI nem demonstra repouso econômico na base ou o novo serviço de localização. Não são tratados aqui como correções prometidas já entregues. |

## 7. Sequência recomendada de correção

1. **N01:** eliminar a reserva automática de 54 KB e comprovar boot/recuperação com o orçamento real de pilha.
2. **N02/N06:** restabelecer a capacidade de coleta e torná-la compatível com o transporte, incluindo SSID escapado e bateria.
3. **N03/N07/N08:** garantir substituição recuperável, manifesto válido, propagação de falhas e preservação de registros após corrupção.
4. **N04/N05:** fechar identidade/tempo por amostra e remontagem de uma observação completa; replay preserva enriquecimento.
5. **N09/N10/N11:** revisão de configuração, consumidores por evento e agregação única/reconciliável.
6. Repetir os testes relevantes no host e no alvo; executar os cenários de placa já previstos no PRD. Não basta repetir somente os casos que passaram neste commit.

O PRD complementar foi atualizado com esses critérios e mantém as decisões pendentes como decisões, sem transformá-las em implementação. A recomendação de não liberar o conjunto ainda se apoia em N01 e nas falhas concretas de coleta e dados, não apenas na ausência de testes físicos.

## 8. Fontes fixadas para revisão

Todos os links abaixo são do SHA conferido. O relatório anterior permanece como evidência histórica em [melhorias/revisao_bpr_fixes_november.md][anterior], e o autor descreve o conjunto em [correcoes_aplicadas_revisao_november.md][correcoes]. Os resultados aqui foram obtidos independentemente desses textos.

[anterior]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/melhorias/revisao_bpr_fixes_november.md
[correcoes]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/melhorias/correcoes_aplicadas_revisao_november.md
[n01]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/bici/src/buffer_manager.cpp#L154
[n01m]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/bici/src/main.cpp#L81
[n02s]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/bici/src/scanning.cpp#L128
[n02f]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/bici/src/buffer_manager.cpp#L361
[n02c]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/bici/include/constants.h#L21
[n03s]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/bici/src/buffer_manager.cpp#L118
[n03b]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/bici/src/config_manager.cpp#L98
[n03c]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/src/config_manager.cpp#L106
[n03d]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/src/buffer_manager.cpp#L718
[n04b]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/bici/src/buffer_manager.cpp#L193
[n04f]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/bici/src/buffer_manager.cpp#L295
[n04p]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/bot/functions/src/index.ts#L327
[n04r]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/bot/src/services/rideCalculator.js#L15
[n05]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/bot/functions/src/index.ts#L298
[n06b]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/bici/src/buffer_manager.cpp#L234
[n06c]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/src/buffer_manager.cpp#L324
[n07m]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/src/buffer_manager.cpp#L550
[n07f]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/src/buffer_manager.cpp#L540
[n07r]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/src/buffer_manager.cpp#L727
[n08i]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/src/buffer_manager.cpp#L368
[n08r]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/src/buffer_manager.cpp#L670
[n09b]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/bici/src/config_manager.cpp#L145
[n09a]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/bici/src/config_manager.cpp#L213
[n09c]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/src/config_manager.cpp#L68
[n09p]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/firmware/central/src/config_ap.cpp#L274
[n10s]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/bot/src/config/firebase.js#L44
[n10d]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/bot/src/services/bikeMonitor.js#L139
[n10c]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/bot/src/config/firebase.js#L77
[n11r]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/bot/src/services/rideCalculator.js#L105
[n11s]: https://github.com/Ameciclo/bpr-sistema/blob/23b13aaad53fbeda0c2b9e66b82fbfb3b9c5daa2/bot/src/services/rideCalculator.js#L161
