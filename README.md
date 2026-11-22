# FrameRoom R0.4 — ring duplo de AHardwareBuffer (experimental)

O FrameRoom é um projeto Android ARM64 de pesquisa para um futuro **espaço paralelo + frame generation**. Esta entrega continua o código da R0.2 e **não** afirma que já virtualiza jogos Android, utiliza o `Lossless.dll` para gerar quadros, ou injeta Vulkan em apps de terceiros.

## Novidade técnica da R0.4

A R0.2 apenas alocava um `AHardwareBuffer` para diagnóstico e renderizava diretamente na swapchain. A R0.4 acrescentou um **segundo modo real de comandos Vulkan**:

```text
AHardwareBuffer_allocate ×2 (RGBA8)
     ↓
VkImage + VkImportAndroidHardwareBufferInfoANDROID
     ↓
vkCmdClearColorImage + barra móvel (staging VkBuffer)
     ↓
vkCmdCopyImage (GPU → imagem adquirida da swapchain)
     ↓
vkQueuePresentKHR
```

Neste modo, dois buffers alternados são alocados e importado no **próprio processo FrameRoom**. Não há captura MediaProjection; não há comunicação com um segundo aplicativo nem interpolação. A movimentação é gerada diretamente no Vulkan, para futura aferição. O modo anterior continua disponível para comparação.

A importação verifica disponibilidade da extensão `VK_ANDROID_external_memory_android_hardware_buffer`, suporte ao formato/uso de imagem e tipos de memória. Caso o driver não suporte essas condições, mostra uma mensagem de erro em vez de fingir que o teste funcionou.

### O que existe e o que ainda falta

| Recurso | R0.4 |
|---|---|
| Listar e adicionar atalhos de apps | Código existente (abre externamente, **sem virtualização**) |
| Importar DLL do usuário | Código existente (validação PE/armazenamento, **sem extração LSFG**) |
| Vulkan em SurfaceView | Código existente |
| Alocação AHardwareBuffer | Código existente |
| `AHardwareBuffer` importado para `VkImage` | **Código experimental**, sem teste em GPU real |
| Renderizar uma faixa móvel na imagem Vulkan importada | **Código experimental**, sem teste em GPU real |
| Copiar imagem AHB para swapchain e apresentar | **Código experimental**, sem teste em GPU real |
| Testes de compatibilidade do driver | Verificações de extensão, formato, uso e memória no código |
| Pipeline AHB → Bloodframe → AHB | **Não implementado** |
| Aplicar frame generation 2× à cena | **Não implementado** |
| Executar outros APKs dentro do FrameRoom | **Não implementado** |

## Como compilar sem PC

1. Envie o conteúdo da pasta para a raiz de um repositório GitHub.
2. Em **Actions → Build FrameRoom Android → Run workflow**, execute o workflow.
3. Quando terminar, procure os artefatos `FrameRoom-r0.3-debug-apk` e `FrameRoom-r0.3-build-log`.
4. Se houver falha, o artefato `build-log` contém o erro detalhado.
5. Instale o APK no POCO X6 5G para testar o driver Adreno 710 (esse teste ainda não foi realizado por nós).

### Roteiro de teste no aparelho

1. Abra **Testar Vulkan direto / AHB importado**.
2. Confirme que o **modo Vulkan direto** apresenta uma faixa roxa móvel.
3. Ative **Modo Vulkan + AHardwareBuffer (experimental)**. A tela deve mostrar uma faixa roxa móvel sobre um fundo de cor variável.
4. Alterne a meta de 30/60 para observar a taxa de envios reportada; **isso não mede FPS interpolado nem latência de entrada**.
5. Gire o aparelho, pause, retorne e observe erros de swapchain, uso de memória e travamentos.
6. Se houver falha no modo AHB, copie a mensagem de status (incluindo eventual `VkResult`).

### Testes locais disponíveis

```bash
bash tools/test-all.sh
```

Executa os 6 casos da validação PE (JDK) e checagens estáticas de integração JNI/CMake/Vulkan. **Checagens estáticas não substituem build Android ou execução na GPU**.

## Arquitetura e limites

- `app/src/main/cpp/ahb_offscreen.*`: propriedade, importação, escrita e cópia de um `AHardwareBuffer` pela Vulkan. Buffer pertencente ao próprio app. Não produz sync FDs externos, nem faz ownership transfer para outro motor Vulkan.
- `app/src/main/cpp/vulkan_demo.cpp`: dispositivo, surface, swapchain, controle de modo, loop e apresentação.
- `VulkanDemoActivity.java`: seleção entre renderização direta e modo AHB experimental.
- `probe.cpp`: enumeração de Vulkan e alocação de pequeno AHB de diagnóstico.
- `DllStore`/`PeValidator`: importação privada da DLL fornecida pelo usuário sem carregá-la como código executável.
- `MainActivity`/`AppPickerActivity`: lançador de aplicativos selecionados, **não** sandbox.

## Próximo marco: Bloodframe

1. **Compilar R0.4** no CI e validar o caminho AHB na Adreno 710.
2. Auditar API C, licenças, requisitos e dependências de [Bloodframe](https://github.com/ducvd89/bloodframe) (notar versões mais recentes do projeto). Integrar uma versão fixada e reproduzível, sem copiar código incompatível.
3. Substituir imagem única por **ring de quadros AHB** (pelo menos dois frames de origem), com sincronização/ownership corretos e exportação/importação de sync FDs quando necessário.
4. Chamar a API real de Bloodframe com o `Lossless.dll` fornecido pelo usuário e medir quadros gerados. O arquivo atualmente **não** é consumido pelo motor.
5. Só depois investigar integração autorizada com renderizadores de terceiros e virtualização. APKs Android comuns continuam isolados pelo sistema operacional; a técnica desta R0.4 **não** contorna essa proteção.

## Compilação e verificação nesta sessão

- Testes Java de validação PE: **executados localmente** (ver resultado da entrega).
- Checagens estáticas Vulkan/JNI/CMake: **executadas localmente**.
- Android SDK/NDK e Gradle: **indisponíveis no ambiente local**, então o APK não foi compilado nesta sessão.
- GPU Adreno 710: **não testada**.
- Frame Generation: **não testado/não integrado**.

## Licenças

Código original FrameRoom sob licença MIT (veja `LICENSE`). Não inclui `Lossless.dll`, shaders proprietários ou Bloodframe. O código da interface de LSFG Android tem termos próprios: este projeto não o copia. A implementação AHB é original e segue a API Vulkan/Android disponível publicamente.

## Novidades técnicas da R0.4

- **Ring duplo AHB:** dois `AHardwareBuffer` importados em `VkImage` separadas; a renderização alterna o destino a cada quadro. A fila única e a fence existente serializam a escrita e apresentação na demonstração. **Não há interpolação nem comunicação com outro processo.**
- **Ponte futura:** `AhbOffscreen::hardwareBuffer()` expõe o handle (não é transferida sua propriedade) para uma integração do motor que respeite a sincronização e a duração do buffer.
- **Build nativo:** removida a dependência `nativewindow` do link CMake, pois as funções usadas são fornecidas por `android` no NDK.
- **Testes estáticos:** verificam ring duplo e linking. A compilação Android e os testes na Adreno 710 continuam pendentes.
