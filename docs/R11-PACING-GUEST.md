# R0.11 — desempenho e Android convidado

## Evidência no POCO X6

O vídeo do usuário foi gravado no FrameRoom R0.10. As leituras abaixo vêm do overlay da demonstração Vulkan.

| Instante | Limite configurado | FG | FPS originais | FPS na tela | Tempo do lote |
| --- | ---: | --- | ---: | ---: | ---: |
| 00:11 | 30 | Ligado | 17,7 | 35,4 | 10,7 ms |
| 00:22 | 30 | Desligado | 30,0 | 30,0 | — |
| 00:53 | 60 | Ligado | 27,3 | 46,9 | 12,7 ms |

Às 00:53, a amostra da cena até a apresentação levou 46,2 ms. Às 00:22 foram 23,1 ms. São momentos com metas de FPS diferentes. O par comparável no limite de 30 demonstra a queda nos originais e confirma que o driver registrou frames gerados.

## Correção de cadência

A R0.10 esperava após enviar o interpolado. Quando o envio do original passava do prazo, o produtor esperava outro período inteiro antes de retomar. A R0.11 remove essa espera e usa os horários do Vulkan para espaçar original e interpolado em aparelhos compatíveis. A fila de apresentação permite dois comandos em andamento. Um quadro gerado que não cabe entre os prazos sai da fila de candidatos, preservando a chance do original.

Os originais são renderizados na resolução da superfície. A interpolação pode usar 640×360, 480×270 ou 384×216 conforme o limite original e a carga. O host testa o gralloc nas dimensões solicitadas. A proteção volta à apresentação direta quando a tela não comporta o 2×, o lote perde prazos repetidamente ou o ritmo dos originais permanece abaixo da meta. O overlay mostra o estado efetivo e a taxa dos originais exibidos. [Horários de apresentação Vulkan](https://docs.vulkan.org/refpages/latest/refpages/source/VkPresentTimeGOOGLE.html) · [Frequência de quadros no Android](https://developer.android.com/media/optimize/performance/frame-rate)

## Espaço paralelo com FG

O objetivo é instalar apps Android compatíveis e executar suas instâncias dentro de um Android convidado. O compositor do convidado entrega os frames ao FrameRoom. Isso evita integração específica por jogo.

A tela virtual de um app Android comum impõe a política de lançamento para Activities externas. Um perfil de trabalho permite instâncias com dados separados, mas mantém os processos e os buffers gráficos sob controle do sistema. A VM Microdroid fornecida pelo Android possui outro escopo e vem sem gráficos e sem os serviços de uma instalação Android de uso geral. [Política de lançamento em displays](https://source.android.com/docs/core/display/multi_display/activity-launch) · [Perfis de trabalho](https://developer.android.com/work/managed-profiles) · [Microdroid](https://source.android.com/docs/core/virtualization/microdroid)

O Wayondroid em `99f64b19` usa um Android 13 convidado e gfxstream com aceleração experimental. O compositor sinaliza o ID do buffer em `VineFbHeader` e o processo host importa o AHardwareBuffer. A saída direta em `gpu_presenter.cpp` entrega esse buffer à SurfaceControl com fence de entrada `-1`. A documentação registra o risco de tearing entre o render do convidado e o consumo no host. O reaproveitamento depende do avanço de `host_shown_seq`.

Para conectar um convidado ao LSFG, o compositor precisa entregar uma imagem RGBA8 com um fence de prontidão verificável e manter a imagem sob responsabilidade do host até um fence de conclusão. Também precisa validar formato, propriedade da fila e caminho de apresentação. O primeiro teste completo precisa exercitar instalação de um APK normal no convidado, entrada, áudio, redimensionamento e frames interpolados. O upstream documenta seu teste em Galaxy S24 com Android 17. O comportamento no POCO X6 com Android 16 segue pendente. [Arquitetura do Wayondroid](https://github.com/yoro1836/wayondroid/blob/99f64b19a62500f03ee3b82175fe06ebcf54299c/docs/rootless.md) · [Consumo do AHardwareBuffer](https://github.com/yoro1836/wayondroid/blob/99f64b19a62500f03ee3b82175fe06ebcf54299c/app/src/main/cpp/display/gpu_presenter.cpp)

A Arena incluída é a via cooperativa disponível nesta compilação. A cópia de APKs na biblioteca continua disponível para outros apps. O teste físico 2× confirmado foi o da R0.10 na demonstração. O efeito da nova cadência e a Arena incluída exigem uma rodada no aparelho.
