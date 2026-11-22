# FrameRoom R0.10

## AHardwareBuffer e Bloodframe

O erro observado na R0.8 do POCO foi `AHardwareBuffer_allocate=1` para a saída RGBA8 com uso 16777984, uma combinação que inclui `GPU_DATA_BUFFER`. O sucesso de `vkGetPhysicalDeviceImageFormatProperties2` estabelece a capacidade do driver Vulkan de importar a imagem, enquanto a alocação determina se o gralloc aceita a combinação concreta.

A R0.10 testa a alocação real de 640×360 para o transporte básico e o de saída direta, verifica as capacidades direcionais no Vulkan e deixa o seletor da Bloodframe decidir entre `direct-storage`, `direct-input-copy-output` e `transport-only`. O primeiro fallback escreve em um storage image privado e faz cópia Vulkan para um AHB de saída. Se a entrada também precisar de cópia, o modo `transport-only` carrega os AHBs em imagens privadas. As transferências permanecem na GPU. Esta verificação usa as flags reportadas pela Bloodframe. [Código do seletor upstream](https://github.com/ducvd89/bloodframe/blob/f4ac713aef58d9403a136e179b3b0d163d85eb66/framegen/src/core/device.cpp) · [Código das cópias](https://github.com/ducvd89/bloodframe/blob/f4ac713aef58d9403a136e179b3b0d163d85eb66/framegen/v3.1p_src/context.cpp)

As importações do FrameRoom usam `allocationSize` e `memoryTypeBits` fornecidos pelo AHB. A R0.9 consultava `vkGetImageMemoryRequirements` antes de vincular duas imagens importadas. A especificação exige que a memória já esteja vinculada para essa chamada em imagens AHB. Dispositivos que anunciam `VK_ANDROID_external_memory_android_hardware_buffer` também precisam da dependência `VK_EXT_queue_family_foreign` ativada ao criar o dispositivo. [Especificação da consulta](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetImageMemoryRequirements.html) · [Extensão de memória AHB](https://docs.vulkan.org/refpages/latest/refpages/source/VK_ANDROID_external_memory_android_hardware_buffer.html)

### Execução

1. Cria dois AHBs de entrada e um de saída, importados em dois dispositivos lógicos Vulkan com o mesmo UUID físico e de driver. O AHB de saída nasce com uma cor sentinela.
2. Renderiza as cenas originais alternadamente nas duas entradas. O host transfere a propriedade para `VK_QUEUE_FAMILY_EXTERNAL` com layout `GENERAL`, aguarda a gravação e chama `bb_lsfg_present`. Três originais preparam o histórico temporal.
3. Aguarda o sync FD de lote se exportado e confirma a conclusão pelo fence da Bloodframe. Nos dispositivos sem suporte a sync FD, o fence permite o caminho sem esse handle. A implementação serializa entrada, geração e reaproveitamento. O Vulkan executa cópias e apresentação.
4. Compara pixels GPU das duas entradas e da saída nos primeiros lotes. Procura movimento entre originais, diferença em relação às duas imagens, contraste e ausência da sentinela. Essa verificação detecta saída vazia, incompleta ou cópia do original. A qualidade visual continua dependendo de inspeção em jogo.
5. Envia o intermediário à swapchain FIFO antes do original mais novo. Se o lote perde o prazo, o app mantém a apresentação dos originais. Libera buffers e contexto ao desligar, suspender ou perder a superfície. Tenta recriar o renderizador em modo direto após perda do dispositivo do host.

O motor roda em modo 2× com LSFG de desempenho e fluxo 0,5. O cabeçalho upstream aceita três saídas, mas 3× e 4× exigem nova validação de buffers, ordenação, pacing e custo na GPU. O renderer solicita uma frequência compatível ao Android. A tela e o sistema decidem a frequência efetiva.

O tempo LSFG vai da chamada de geração até a conclusão do lote, incluindo espera. O FPS original vem de cenas produzidas. O FPS exibido e a latência cena → exibição exigem feedback do driver. Nos demais casos, o overlay informa envios/s. O descarte conta quadros FG que o app omitiu ou deixou de enviar. Toque → fóton exige instrumentação externa. [Apresentação Vulkan no Android](https://developer.android.com/games/develop/vulkan/frame-pacing-extensions)

## Hospedagem cooperativa

O FrameRoom confirma o certificado e valida o SHA-256 dos arquivos copiados contra os arquivos instalados. O APK base e seus splits são gravados em área privada e somente leitura. `DexClassLoader` carrega a entrada, que precisa vir do classloader do APK hospedado. A entrada cooperativa é executada em um processo do FrameRoom com o mesmo UID. `GameStorage` oferece arquivos e preferências separados por pacote. Suas APIs de armazenamento são um contrato entre participantes com a mesma assinatura.

### Contrato v1

Use [GameScene](../game-api/src/main/java/dev/frameroom/sdk/GameScene.java), uma entrada pública no namespace do APK e o certificado compartilhado pelos dois builds autorizados. O manifesto declara `dev.frameroom.GAME_ENTRY` com a classe e `dev.frameroom.VULKAN_GAME_API` com valor inteiro 1. A classe oferece `public static GameScene createVulkanScene(Context context)`. O FrameRoom fornece tempo monotônico e recebe retângulos SDR normalizados da simulação. O host grava a cena em Vulkan. O código de jogo gerencia toque e áudio, recebe pausa/retorno e pode persistir dados no contexto fornecido. A implementação de referência é [HostedArcade](../samplegame/src/main/java/dev/frameroom/samplegame/HostedArcade.java). A classe `GameEntry` anterior com `createView(Context)` segue disponível para a Arena Canvas.

O contrato v1 atende jogos que delegam o desenho ao host. Ports com renderizadores próprios precisam de um adaptador de frames que importe RGBA8 via AHB, controle a fila e compartilhe a propriedade e o fence do produtor. Isso também exige reconhecer o dispositivo e driver de origem, reter os frames até a conclusão do consumo e confirmar a liberação antes de reciclar buffers.

### Aplicativos Android comuns

| Caminho | Evidência | Resultado para o FrameRoom |
| --- | --- | --- |
| APIs públicas e perfil de trabalho | O Android separa UIDs e armazenamento entre perfis. A instalação no perfil pode ser independente. | O perfil separa os dados. O jogo possui seu próprio processo e swapchain. O FrameRoom não recebe seus buffers. |
| VirtualApp `b3c634a` | Fork Apache 2.0 com adaptações para Android 16 em `TransactionHandlerProxy` e interfaces internas de Binder/Activities. A documentação principal cita testes Android 14. | Compatibilidade de execução precisaria de validação no Android 16. A virtualização de componentes não concede acesso ao swapchain do jogo. |
| Wayondroid `99f64b1` | O repositório GPL-3.0 documenta guest Android 13, Binder em userspace, áudio, toque e Vulkan/GLES experimental por gfxstream. A validação publicada usa Galaxy S24 com Android 17. O host recebe AHBs em `gpu_presenter.cpp` e os entrega a uma SurfaceControl. | Candidato a integração em fork separado, respeitando a licença. Para LSFG, precisa de um contrato de sincronização e reciclagem dos AHBs antes de entrar no compositor. Desempenho e execução na Adreno 710 continuam pendentes. |
| AVF/Microdroid | O guest oficial Microdroid não inclui SystemServer, Zygote, HALs de gráficos nem UI de apps. | Serve para cargas isoladas. A configuração documentada é incompatível com jogos Android que dependem de Activity e Vulkan. |

[Sandbox Android](https://source.android.com/docs/security/app-sandbox) · [Restrições às APIs fora do SDK](https://developer.android.com/guide/app-compatibility/restrictions-non-sdk-interfaces) · [Perfis de trabalho](https://developer.android.com/work/managed-profiles) · [Microdroid](https://source.android.com/docs/core/virtualization/microdroid) · [VirtualApp](https://github.com/ISEKHON/VirtualApp) · [Wayondroid](https://github.com/yoro1836/wayondroid)

Um contêiner rootless separado continua sendo uma alternativa a investigar. O APK `org.nx2a.t010035f022078000` 2.0.76 pode ser identificado no aparelho. O relatório da biblioteca enumera tamanho do APK base, splits e versão. A cópia identifica sinais de Unity/Unreal e presença de `lib/arm64-v8a`. O tamanho informado de 5,9 MB não confirma a existência ou ausência de OBB, dados entregues à parte ou Vulkan ativo. A execução hospedada e o FG seguem sem comprovação nesse port.

## Verificação

| Verificação | Como reproduzir |
| --- | --- |
| Validador PE e imagem | `bash tools/test-all.sh` |
| Binários, certificado compartilhado, ARM64 e alinhamento de 16 KiB | Actions executa `tools/verify-apks.sh` após montar os dois APKs |
| Instalação, assinatura e execução hospedada | Instale os APKs do mesmo build, adicione a Arena e abra com **Jogar**. Confira movimento, alvo, som e progresso após sair e reabrir. |
| DLL, gralloc e apresentação | Importe a DLL, rode a demonstração e copie o diagnóstico depois de ligar o 2×. Procure transporte, contagens de pixels, envios e confirmação de tela. |
| Alternância e ciclo de vida | Compare ligado/desligado, teste giro da tela, suspensão, retorno, troca de FPS e reabertura. Mantenha a cena em execução alguns minutos e confira desvios do pacing e descartes. |
| Memória | Compare `adb shell dumpsys meminfo dev.frameroom` antes e depois de vários ciclos, caso haja acesso a ADB. O teste funcional continua disponível pelo celular. |

Os testes de Java/C++ e a compilação não validam a GPU do aparelho. O POCO e a DLL importada são necessários para confirmar geração produzida, aparência, estabilidade, latência e vazamentos no hardware alvo.
