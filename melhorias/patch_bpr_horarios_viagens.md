# BPR — Patch de horários: saída, chegada e reinícios

**Versão:** 1.0 — 7 de novembro de 2026, America/Recife.  
**Status:** escopo funcional fechado com o usuário; especificação para implementação e validação. Não representa código aplicado, compilado ou implantado.  
**Base da revisão anterior:** Ameciclo/bpr-sistema, fixes/november, commit `7a3533b443d43bc39d9519afbb4d3945aa4d1a7b`. Conferir mudanças posteriores antes de implementar.

Este documento consolida a primeira parte da discussão e substitui, para implementação dos horários, as alternativas ainda abertas nos documentos didáticos anteriores. A transferência com hashes e recibos permanece no seu PRD próprio.

## 1. Decisão central

A viagem tem registro de saída e registro de chegada associados à central. A bike guarda os eventos com sua identidade de viagem, a referência de hora da estação e a leitura local de tempo correspondente a cada contato.

O último scan Wi-Fi e a chegada são registros diferentes. Se o último scan tem contador de 25 minutos e a chegada tem contador de 30 minutos, a chegada fecha a viagem nos 30 minutos. Não é necessário existir um scan Wi-Fi exatamente na chegada para registrar esse evento.

**Duração total = hora de chegada − hora de saída**, quando as duas referências civis forem válidas e consistentes. Essa duração inclui os intervalos entre scans e os períodos de interrupção entre os dois eventos. Precisão dos horários dos pontos intermediários é uma informação separada da duração total.

Contar para trás a partir da chegada é válido para pontos que compartilhem a referência de contador usada na conta. Os registros de saída, os heartbeats e os identificadores de reinício permitem interpretar os demais trechos.

## 2. Fronteira operacional da viagem

### 2.1 Saída

Registrar uma única saída por viagem, vinculada à estação de origem. Usar o evento operacional de saída quando disponível no sistema e capturar a referência de hora e contador associada a ele.

Quando a operação só dispuser de contatos periódicos, usar como referência operacional o último contato válido com a estação antes da saída confirmada pela máquina de estados. Enquanto a bike estiver na base, os heartbeats atualizam essa referência candidata; não abrem viagens repetidamente. Ao reconhecer a saída, congelar o evento inicial e sua proveniência.

Essa referência por último contato tem a resolução do mecanismo de presença. Registrar o método/qualidade sem apresentá-la como uma medição física exata do primeiro movimento da roda. Perda breve de BLE, reboot ou simples silêncio de uma mensagem não devem, isoladamente, abrir uma nova viagem. A implementação deve integrar a confirmação de saída ao estado operacional e aos intervalos de heartbeat existentes, com tolerância validada em bancada.

### 2.2 Chegada

Reconhecida a volta à estação durante uma viagem ativa, registrar a chegada no primeiro contato válido correspondente ao retorno. Capturar a hora da estação e a leitura atual do contador da bike, vinculadas ao mesmo evento, considerando a incerteza de comunicação.

A estação de chegada pode ser diferente da estação de origem. Preservar ambas como proveniência; elas não substituem a identidade da bike ou da viagem.

Heartbeats posteriores na base atualizam a sincronização, mas não empurram a chegada para a frente. Retry de entrega não reabre nem encerra outra viagem. A conclusão do upload também não é uma chegada nova.

### 2.3 Relação com buffers e nova saída

Fechar a chegada encerra o período da viagem; não autoriza apagar seus dados. Limpeza depende dos recibos descritos no PRD de transferência.

Se a bike sair novamente antes de confirmar uma entrega anterior, a viagem pendente deve permanecer identificada e preservada. A nova saída não pode sobrescrever o conjunto antigo nem reutilizar seu ID. Aplicar limites e diagnóstico de capacidade sem descartar dados silenciosamente.

## 3. Exemplo que resolve a diferença dos cinco minutos

Exemplo sem reinício, com origem do contador associada à saída às 13h30:

| Evento | Contador relativo à saída | Hora do evento |
|---|---|---|
| Saída registrada com a central | 0 min | 13h30 |
| Scan intermediário | 10 min | 13h40 |
| Último scan antes da chegada | 25 min | 13h55 |
| Chegada registrada com a central | 30 min | 14h00 |
| Upload confirmado, por exemplo | Fora da duração da viagem | 14h03 |

O total é 30 minutos. O scan de 25 minutos aconteceu cinco minutos antes da chegada. O upload às 14h03 não aumenta a viagem para 33 minutos.

Um contador físico não precisa voltar a zero na saída. Se ele vale 100 minutos na saída e 130 na chegada, o relativo da viagem é a diferença de 30 minutos. A origem deve ser guardada explicitamente.

Para um trecho contínuo, as duas formas de cálculo são:

- Pela saída/referência anterior: **hora do ponto = hora da referência + (contador do ponto − contador na referência)**.
- Pela chegada/referência posterior: **hora do ponto = hora da referência − (contador na referência − contador do ponto)**.

As contas usam unidades consistentes e o mesmo domínio de contador. Quando referências válidas divergem além da incerteza aceitável, registrar a divergência e o método adotado. Não alterar os dados originais para forçar igualdade.

## 4. O que cada participante registra

Os nomes abaixo descrevem o contrato lógico; o formato de transporte deve respeitar versão, tamanho de mensagem e compatibilidade do firmware.

| Registro | Dados necessários |
|---|---|
| Viagem | trip_id persistente, bike_id canônico, estado e referências de saída/chegada. |
| Evento de saída/chegada | ID estável do evento, tipo, trip_id, estação, hora civil com validade/qualidade, boot/trecho e contador local correspondente. |
| Observação de rota | ID/ordem estável dentro da viagem, boot/trecho, contador local e todas as redes daquele scan. |
| Referência de heartbeat | ID da referência, estação, hora civil informada, contador de recebimento na bike, boot/trecho e qualidade/idade da sincronização. |
| Reconstrução no backend | Hora atribuída, referência/método usados, qualidade e versão do processamento. |

Usar timestamps civis em UTC e unidades explícitas. Converter para o fuso de exibição na apresentação, preservando a regra de período já adotada pelo produto. Contador relativo não é timestamp Unix. Ausência de hora válida deve ter representação explícita, sem inventar 1970 ou enviar undefined ao banco.

Cada evento confirmado é imutável no reenvio. Repetir o evento de chegada devolve a mesma entidade e hora. Mudanças deliberadas são revisões auditáveis, com preservação do original.

A numeração das observações é a autoridade de ordem, inclusive com contadores iguais, menores depois de reboot ou timestamps civis ajustados. Não confundir essa ordem com o índice dos fragmentos BLE. Não deduplicar pontos apenas por horário.

Saída/chegada são eventos próprios, e não scans fictícios. Podem usar a localização cadastrada da estação quando disponível, com sua proveniência. Sem localização cadastrada, preservar o evento e a ausência de coordenadas; não inventar uma posição.

## 5. Referência de hora pelo heartbeat

A bike não tem acesso à internet. Ela observa redes Wi-Fi; recebe a referência civil pela comunicação com a estação.

1. A bike informa no heartbeat sua identidade, boot/trecho e contador atual.
2. A central responde com ID da referência, sua hora atual e validade/qualidade dessa hora.
3. No recebimento, a bike lê seu contador e persiste a associação entre essa leitura, a hora recebida e o trecho.
4. A bike devolve a referência efetivamente guardada no próximo heartbeat ou na entrega dos dados.
5. Central e backend preservam as referências necessárias às observações, à saída e à chegada.

A central envia a hora que está correndo naquele momento a partir da sua sincronização NTP, não o número congelado de uma consulta antiga. A idade da última sincronização é informação separada.

Preservar referências de trechos antigos enquanto houver dados que dependam delas. Receber outra referência não apaga a única evidência temporal das observações anteriores. A latência da troca deve ser considerada na qualidade; o MVP aceita horário aproximado.

Os eventos de contato podem acompanhar a entrega como metadados vinculados, sem modificar silenciosamente um lote já fechado e seu hash. Distinguir evento de chegada da hora em que o backend recebeu ou processou o upload.

## 6. Continuidade após reinício

Persistir a identidade da viagem e o estado necessário à ordem de coleta junto ao checkpoint dos dados. Depois de um reboot não intencional, recuperar a mesma viagem ativa, abrir um trecho identificável para o contador reiniciado e continuar a sequência de observações. Reinício não é nova saída nem nova chegada.

Não deduzir reboot apenas porque o contador diminuiu: o primeiro valor novo pode ser maior que o último valor antigo. Preservar identificação explícita do domínio de contador.

### Exemplo com os dois extremos registrados

| Evento | Trecho / contador | Referência de hora |
|---|---|---|
| Saída | A / 0 min | 13h30, pela central |
| Ponto 1 | A / 15 min | Aproximadamente 13h45 pela referência de A |
| Falta de alimentação | Transição A → B | Sem medição direta do intervalo desligada |
| Ponto 2 | B / 1 min | Aproximadamente 13h51 pela referência da chegada |
| Ponto 3 | B / 5 min | Aproximadamente 13h55 pela referência da chegada |
| Chegada | B / 10 min | 14h00, pela central |

**A duração total continua sendo 30 minutos**, pois vem de 13h30 até 14h00. O contador reiniciado não reduz a viagem a dez minutos.

O ponto 1 usa a referência de A; os pontos 2 e 3 usam a de B. O intervalo entre os pontos 1 e 2 pode ser estimado em seis minutos neste exemplo, sem afirmar que os seis minutos inteiros foram de placa desligada.

Se houver trechos intermediários sem referência civil suficiente, conservar a sequência e os intervalos locais conhecidos. O backend pode estimar a posição temporal dentro dos limites disponíveis e deve registrar método/qualidade. Não aplicar diretamente a referência de B a um contador de A.

Continuar um contador lógico a partir do último checkpoint pode ser uma informação auxiliar, com sua natureza identificada. Não é necessário para calcular a duração total quando saída e chegada têm horas válidas; também não mede sozinho o tempo desligada.

Sleep planejado deve ser distinguido de perda de alimentação. Se um temporizador realmente preservado permitir continuidade, registrar essa evidência. Se a referência usada zerar, separar o trecho ou registrar a ligação comprovada. Tempo de sleep apenas solicitado não é automaticamente uma medição do tempo realmente transcorrido. Validar o comportamento na placa e na versão efetiva do SDK antes de escolher a API.

## 7. Backend, dashboard e agregados

Manter na bike operações simples de coleta, identificação, persistência e troca de referências. Reconstrução de horários, geolocalização e inferência da rota ficam nos serviços do backend usados pelo dashboard, sem depender de alguém abrir a página.

Guardar os fatos recebidos separadamente dos horários reconstruídos. Revisar uma estimativa não muda a identidade do ponto nem transforma um retry em outra viagem. Inferência por distância e ajustes manuais devem registrar método e revisão; não substituem silenciosamente os contadores originais.

A chegada original é o evento de conclusão temporal da viagem neste contrato. A hora de upload ou de replay não pode substituí-la como finished_at. O reparo de agregados reutiliza a contribuição e o período canônicos persistidos, conforme a especificação dos itens 2/4/5. Este patch não redefine o fuso nem redistribui automaticamente o histórico por meses.

Se a hora da estação for inválida ou inconsistente, preservar os eventos/observações e sinalizar pendência de reconstrução. Não produzir duração negativa, falsa precisão ou descarte silencioso para fazer o cálculo passar. Ordenação da rota continua baseada na sequência.

## 8. Compatibilidade e pontos de implementação

Revisar a máquina de estados de saída/retorno, os heartbeats e seus retornos, checkpoints da bike, registro da estação e consumidores de sessões/viagens. Passar IDs, trechos e referências até o backend, sem tentar inferir toda a identidade a partir de timestamps.

Versionar os registros e mensagens novos. Contar os metadados no tamanho do pacote e testar o limite de aplicação do protocolo e o MTU real. Não alterar silenciosamente mensagens já confirmadas de um lote para acrescentar outro horário.

Dados antigos sem evento de chegada ou boot conhecido podem ser mantidos como legados/estimados. Recuperar o que tiver evidência inequívoca e preservar os demais para revisão. Não renomear uptime como hora civil nem apagar pontos repetidos para adaptar o histórico ao formato novo.

## 9. Critérios de aceite

Os casos abaixo são critérios da implementação futura; este documento não executou testes de firmware ou backend.

| ID | Cenário | Resultado exigido |
|---|---|---|
| H01 | Saída 13h30, scan aos 25 min e chegada 14h00 aos 30 min. | Total 30 min; scan às 13h55. |
| H02 | Último scan ocorre cinco minutos antes do retorno e não há scan na chegada. | Evento de chegada usa o contador atual; não desloca o scan para a hora do contato. |
| H03 | Contador físico vale 100 min na saída e 130 na chegada. | Origem registrada; duração relativa 30 min, sem exigir zerar o relógio físico. |
| H04 | Upload termina às 14h03 ou é repetido no dia seguinte. | Chegada continua 14h00; duração e identidade não mudam. |
| H05 | Bike fica parada por horas na base, com muitos heartbeats. | Nenhuma viagem por heartbeat; referência de saída corresponde ao evento operacional adotado. |
| H06 | Reboot no caminho, com origem A e retorno B conforme exemplo. | Mesmo ID, ordem dos pontos preservada e duração total 30 min pelas duas horas civis válidas. |
| H07 | Reinício com primeiro contador novo maior que o último antigo. | Trecho novo reconhecido sem depender de redução numérica. |
| H08 | Vários reboots e trecho intermediário sem referência suficiente. | Rota preservada; horário intermediário estimado/desconhecido, com método explícito. |
| H09 | Nova referência de heartbeat chega antes do upload de pontos antigos. | Referências anteriores necessárias continuam recuperáveis. |
| H10 | Hora NTP indisponível, antiga ou alterada na central. | Qualidade/pendência explícitas; sem hora inventada, duração falsa ou descarte de observações. |
| H11 | Conexão breve se perde na base e depois volta; chegada recebe vários retries. | Sem saída/viagem espúria; uma chegada canônica quando houver retorno real pelo estado operacional. |
| H12 | Saída em uma estação e chegada em outra. | Ambas preservadas; mesmo conjunto de viagem, com horas/qualidades correspondentes. |
| H13 | Chegada registrada, autorização de limpeza ainda pendente, nova saída depois. | Dados antigos preservados; novo ciclo não sobrescreve nem reutiliza o anterior. |
| H14 | Dois scans têm o mesmo contador ou hora reconstruída. | Ambos permanecem distintos pela sequência/identidade. |
| H15 | Sleep planejado, corte total de energia e gravação interrompida. | Continuidade e checkpoints recuperados conforme evidência real; sem misturar domínios de contador. |
| H16 | Registro legado não tem todos os campos novos. | Compatibilidade explícita; evidência preservada e qualidade adequada. |

Validar persistência e concorrência com SDK/emulador real onde aplicável. Em placa, testar alimentação interrompida, sleep, retomada, contato BLE e os limites reais de tamanho. A aprovação deste Markdown fecha o desenho funcional, não substitui essas verificações.

## 10. Documentos relacionados

- [Diagramas da discussão](https://chatgpt.com/library?file_id=libfile_ccd66191ab7881918b0186026069dcd9&account_id=personal): exemplos que explicaram a associação dos contadores.
- [Discussão original](https://chatgpt.com/library?file_id=libfile_ffe082edd17c8191bfcf151fe74f9e4f&account_id=personal): alternativas anteriores, agora consolidadas por este contrato de horários.
- [PRD de transferência e recibos](https://chatgpt.com/library?file_id=libfile_4957b70627908191b8b66c62444060cb&account_id=personal): regras para liberar dados depois da confirmação remota.
- [Correções dos itens 2, 4 e 5](https://chatgpt.com/library?file_id=libfile_7766cb4a3220819196ca054fccb8e430&account_id=personal): identidade e agregados, período no replay e concorrência das coordenadas.

Nenhum código foi gerado ou alterado por este documento. O próximo trabalho técnico é implementar este contrato, preservando as demais correções e verificando os cenários de aceite.
