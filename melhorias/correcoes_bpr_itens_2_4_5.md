# BPR — Especificação de correção dos itens 2, 4 e 5

**Data:** 7 de novembro de 2026, America/Recife.  
**Status:** comportamentos autorizados pelo usuário; especificação pronta para implementação e teste. Este documento não contém implementação nem comprova implantação.  
**Base técnica conferida anteriormente:** Ameciclo/bpr-sistema, branch fixes/november, commit `7a3533b443d43bc39d9519afbb4d3945aa4d1a7b`. Antes de implementar, conferir a revisão atual sem sobrescrever mudanças posteriores.  
**Referência:** [revalidacao_bpr_7a3533b.md](https://chatgpt.com/library?file_id=libfile_9ac148d13478819196b426b593823462&account_id=personal).

## 1. Escopo e numeração

Os números abaixo são os da explicação na conversa. Não são a numeração M do relatório.

| Item da conversa | Achado técnico | Correção autorizada |
|---|---|---|
| 2 | M03 | Distinguir viagens/dispositivos e impedir colisão dos identificadores usados nos agregados. |
| 4 | M04 | Reprocessar uma viagem sem mudar seu período nem duplicar contribuições mensais. |
| 5 | M06 | Preservar coordenadas e demais enriquecimentos quando normalização e cálculo de localização se sobrepõem. |

O item 5 aqui é o apagamento de coordenadas, não o achado M05 sobre sessões que o consumidor deixa de processar. Os demais achados da auditoria continuam com seu próprio status.

Relógio da bike, reconstrução de horário após reboot e protocolo de retomada da entrega ficam no documento de discussão complementar. Esta correção não muda a definição de saída física, não redefine automaticamente sessão de transporte como viagem e não exige novo firmware para resolver os agregados do backend.

## 2. Item 2 — Identidade estável da viagem e da contribuição

### Problema que deve desaparecer

Hoje startRide forma rideId apenas com o tempo relativo de início. O histórico é separado por bike, mas o token global de contribuição usa esse identificador sem o escopo da bike. Duas bikes com início relativo de 10.000 ms podem produzir dois históricos válidos e apenas uma contribuição pública. A mesma bike também pode repetir esse número depois de reiniciar.

O teste anterior forçou essa coincidência. Não houve medição de sua frequência nas placas. O requisito é que a identidade continue correta caso ela ocorra.

### Requisitos

| ID | Comportamento exigido |
|---|---|
| ID-01 | Usar a identidade canônica da bike do cadastro. Horário, nome de apresentação e estação atual não substituem essa identidade. |
| ID-02 | Identificar a origem estável de cada viagem derivada. No fluxo atual há uma chave de sessão com session_id e session_salt; ela deve acompanhar o processamento juntamente com bike_id. Não reduzir a origem ao timestamp. |
| ID-03 | Produzir um identificador canônico de viagem que distinga origens diferentes e permaneça igual no reenvio, reinício do consumidor e reprocessamento da mesma viagem. Se a regra atual derivar várias viagens de uma única origem, incluir uma referência estável da divisão dentro dela, como a identidade da observação inicial; bike + sessão sozinhos não distinguem essas viagens. A versão da regra que deriva a viagem também deve ser identificável. |
| ID-04 | Caso se escolha identificador aleatório, gerá-lo uma única vez e persistir sua associação à origem com criação exclusiva/atômica. Reprocessar consulta essa associação; não gera outro ID. Uma derivação determinística e estável da origem também atende ao requisito. |
| ID-05 | Usar a mesma identidade completa nos históricos, tokens por bike, tokens públicos, buckets mensais, reparos e consultas. Não corrigir somente a chave onde a viagem é salva. |
| ID-06 | Manter estação como entidade distinta e como proveniência da entrega. A mesma viagem entregue/reprocessada por outra estação não se torna outra viagem por esse motivo. |
| ID-07 | Ausência de identidade suficiente deve produzir pendência/erro recuperável. Não adotar silenciosamente apenas uptime como substituto. |

### Escopo de implementação

Revisar startRide, finishRide, updateBikeMetrics e updatePublicStats em rideCalculator.js e os chamadores em bikeMonitor.js. Passar a identidade de origem até o cálculo, sem tentar recuperá-la posteriormente a partir da data. Consultas, notificações e publicações devem referenciar a mesma entidade.

Manter os IDs físicos e aliases já usados pela frota. Bicicletas e estações devem ter tipo e cadastro distinguíveis; esta tarefa não autoriza renomear todos os dispositivos nem substituir o provisionamento.

A solução pode registrar uma viagem derivada da unidade de coleta usada hoje, preservando essa regra. A definição futura de viagem que atravesse várias sessões, ou de várias viagens numa sessão, continua uma decisão separada. A identidade deve permitir guardar a origem e migrar essa regra depois.

### Migração e reconciliação

1. Inventariar viagens existentes, seus caminhos por bike e os tokens atuais.
2. Construir uma associação inequívoca entre identidade antiga e canônica. Não fundir duas bikes porque ambas possuem ride_10000.
3. Guardar as referências antigas como aliases/proveniência quando necessárias à compatibilidade.
4. Reconstruir os agregados a partir das viagens únicas recuperáveis. Renomear tokens sem recalcular não corrige um total que já perdeu contribuição.
5. Onde a origem antiga for ambígua, registrar a pendência e preservar o registro; não inventar correspondência nem apagar a evidência.

## 3. Item 4 — Período e conteúdo autoritativos no reprocessamento

### Problema que deve desaparecer

Uma viagem já existente é reprocessada em outro mês. O ramo de reparo usa os valores e finished_at da tentativa atual, mesmo depois de localizar a viagem original. Assim, janeiro conserva sua contribuição e fevereiro ganha outra, embora só exista uma viagem.

### Requisitos

| ID | Comportamento exigido |
|---|---|
| MES-01 | Na primeira persistência válida, registrar o período atribuído à viagem, a data utilizada, sua origem e a regra/fuso aplicados. |
| MES-02 | Um reprocessamento comum reutiliza período, distância, método e demais valores da viagem persistida. A tentativa atual não é autoridade para reconstruir esses valores. |
| MES-03 | O relógio do processamento não pode deslocar uma viagem para outro mês apenas porque houve retry ou reparo posterior. |
| MES-04 | Cada viagem contribui no máximo uma vez por métrica aplicável, com token e contador atualizados de forma atômica. O reparo consegue terminar uma contribuição interrompida sem duplicar as concluídas. |
| MES-05 | O resumo do mês deve representar o bucket correspondente. Não acumular vários meses num campo chamado mensal e apenas trocar seu rótulo current_month. Separar totais gerais e mensais quando o contrato assim os distinguir. |
| MES-06 | Mudança deliberada do conteúdo ou do período de uma viagem é uma revisão auditável, com retirada/compensação da contribuição anterior e aplicação da nova. Não é um reprocessamento comum. |

### O que este patch decide sobre o mês

Esta correção fixa a contribuição ao período canônico da viagem e elimina a dependência da data da nova tentativa. Ela não resolve, por conta própria, a reconstrução do horário da pedalada.

Enquanto o patch de tempo não estiver definido, preservar a base temporal documentada dos dados atuais. Se o único horário confiável do registro for o primeiro processamento persistido, identificar o período como mês de registro/processamento, sem apresentá-lo como mês comprovado da coleta. Não alterar fuso nem interpretar uptime como data civil durante a migração.

No desenho futuro, a chegada à base com horário válido pode ser a referência escolhida para o mês de conclusão. Essa referência deverá ser gravada e conservada mesmo que a nuvem só processe os dados depois. A escolha entre mês da chegada, da saída ou outro critério pertence ao debate temporal e deve ser explícita.

### Exemplo de aceite

Uma viagem canônica atribuída a janeiro contribui uma vez em janeiro. Reprocessá-la em fevereiro mantém janeiro=1, fevereiro=0 e total geral=1. Uma viagem diferente ocorrida/atribuída a fevereiro pode elevar fevereiro para um sem alterar a contribuição da primeira.

## 4. Item 5 — Não sobrescrever enriquecimento com uma cópia antiga

### Problema em linguagem simples

Um processo abre a lista de observações ainda sem coordenadas. Outro calcula e grava a localização. O primeiro termina e salva a lista antiga por cima, fazendo as coordenadas desaparecerem.

O caso sequencial foi corrigido no 7a3533b. O requisito pendente é preservar a escrita que acontece entre a leitura e a gravação de outro processo.

### Requisitos

| ID | Comportamento exigido |
|---|---|
| GEO-01 | Cada observação tem identidade estável dentro de sua origem. Usar a identidade do scan, sem depender apenas do timestamp ou da posição ocasional de uma lista. |
| GEO-02 | Dados recebidos e resultados calculados possuem responsabilidade de escrita definida. Repetir a normalização não pode apagar resultado válido produzido em paralelo. |
| GEO-03 | Vincular coordenadas à identidade e à revisão/conteúdo da observação que foi geolocalizada. Igualdade de timestamp sozinha não comprova conteúdo igual. |
| GEO-04 | Se o conteúdo-base mudou, o resultado antigo deve ficar identificado como desatualizado/pendente de recálculo; não deve ser atribuído silenciosamente à observação nova. |
| GEO-05 | Falha ou indisponibilidade da geolocalização mantém os dados Wi-Fi e o estado pendente/desconhecido. Não apaga o registro original. |
| GEO-06 | Atualizar leitores e escritores envolvidos de forma compatível: conversão automática/manual, comandos de rota, projeções, exportações e demais consumidores. Não deixar dois caminhos contraditórios considerados canônicos. |

### Estratégia recomendada

Separar a observação recebida do enriquecimento, vinculando ambos por uma identidade estável e revisão/conteúdo. O normalizador escreve os campos de sua responsabilidade; o conversor grava o resultado de localização no destino próprio. Uma projeção para leitura pode reunir os dois, sem virar outra fonte de verdade editada por vários serviços.

Uma solução que mantenha o layout existente também pode atender, desde que todos os escritores relevantes usem uma operação condicional/transacional adequada. O merge deve considerar o estado atual no commit, e não apenas uma cópia lida antes. Depois de uma consulta externa demorada, conferir novamente a versão da observação antes de instalar o resultado. Não fazer chamada paga ou envio de mensagem dentro de uma função de transação que pode ser repetida pelo SDK.

Escolher a alteração compatível de menor impacto que passe os testes abaixo. Não é suficiente alterar somente assembleSessionIfComplete se outro escritor ainda substituir toda a lista com uma cópia antiga.

## 5. Matriz de aceite

Estes cenários devem virar testes de comportamento; não foram executados como implementação deste documento.

| Teste | Entrada / falha | Resultado exigido |
|---|---|---|
| C245-01 | Duas bikes distintas têm início relativo 10000 ms. | Duas viagens e duas contribuições públicas. |
| C245-02 | Mesma bike, duas origens/sessões distintas com o mesmo tempo relativo. | Identidades distintas; nenhuma viagem real omitida por colisão. |
| C245-03 | Mesma origem repetida por outra estação ou consumidor. | Mesma viagem e uma contribuição, mantendo a proveniência das entregas. |
| C245-04 | Dois trabalhadores tentam criar a identidade da mesma origem simultaneamente. | Um identificador canônico; nenhuma duplicação de contribuição. |
| C245-05 | Reinício depois de reservar ID e antes de concluir os agregados. | Retomada reutiliza a entidade e completa só as contribuições pendentes. |
| C245-06 | Viagem de janeiro reprocessada em fevereiro. | Janeiro=1, fevereiro=0 e total geral=1 para essa única viagem. |
| C245-07 | Gravação interrompida entre histórico, bucket mensal e total geral. | Reparo converge para uma contribuição em cada destino necessário, no período original. |
| C245-08 | Tentativa de reparo contém km/CO2 diferentes do registro já persistido. | Usa a versão canônica ou registra conflito/revisão explícita; não aplica silenciosamente os novos valores. |
| C245-09 | Migrar duas viagens antigas de bikes diferentes com ID textual igual. | Ambas preservadas e agregados reconciliados corretamente. |
| C245-10 | Conversor grava coordenadas entre leitura e gravação de uma normalização repetida. | Coordenadas válidas permanecem. |
| C245-11 | Duas remontagens e uma geolocalização terminam fora de ordem. | Dados-base e enriquecimento convergem para a revisão correta, sem perda de escrita válida. |
| C245-12 | Resultado geográfico de uma revisão antiga chega depois que a observação foi corrigida. | Resultado não é instalado como atual na revisão diferente; há diagnóstico/recálculo. |
| C245-13 | Registro legado sem idx, timestamp repetido ou origem ambígua. | Identidade recuperada de forma documentada ou pendência explícita; nenhum undefined enviado ao SDK e nenhuma observação descartada silenciosamente. |
| C245-14 | Geolocalização falha; rota e exportação leem dados parcialmente enriquecidos. | Observações preservadas, ausência explícita e contrato de leitura consistente. |
| C245-15 | A regra atual reconhece duas viagens distintas dentro da mesma origem de coleta. | Identidades distintas pela divisão estável; reprocessar preserva ambas sem criar outras. |

Usar o SDK/emulador real nos pontos de transação, conflito e validação de payload, além dos adaptadores locais. Adaptadores que removem undefined ou serializam todas as operações podem ocultar parte dessas falhas.

## 6. Ordem de execução e definição de pronto

1. Identidade canônica e associação com registros existentes.
2. Migração/reconstrução dos tokens e agregados com comparação antes/depois.
3. Fixação de período e conteúdo usados no reparo.
4. Escrita concorrente segura de enriquecimento, com compatibilidade dos leitores.
5. Testes de falha/replay/concorrência e verificação dos projetos JS/TS afetados.

Pronto significa: cenários relevantes aprovados, migração recuperável, dados ambíguos identificados, resultados verificáveis e nenhuma mudança indireta no protocolo de horário ou BLE ainda em discussão. Não é suficiente apenas compilar ou trocar a forma textual de um ID.

## 7. Pontos de partida no código e referências

- [rideCalculator.js](https://github.com/Ameciclo/bpr-sistema/blob/7a3533b443d43bc39d9519afbb4d3945aa4d1a7b/bot/src/services/rideCalculator.js): criação da viagem, reparo e agregadores.
- [bikeMonitor.js](https://github.com/Ameciclo/bpr-sistema/blob/7a3533b443d43bc39d9519afbb4d3945aa4d1a7b/bot/src/services/bikeMonitor.js): propagação da identidade da origem e associação dos pontos.
- [Functions/index.ts](https://github.com/Ameciclo/bpr-sistema/blob/7a3533b443d43bc39d9519afbb4d3945aa4d1a7b/bot/functions/src/index.ts): remontagem e conversores/leitores relacionados.
- [Firebase — gravação e transações no Realtime Database](https://firebase.google.com/docs/database/admin/save-data): atualizações de filhos não equivalem a substituir um objeto inteiro; transações recebem o estado corrente e seu manipulador pode ser chamado mais de uma vez. Essas propriedades fundamentam os requisitos de concorrência, mas não determinam sozinhas o desenho do BPR.
