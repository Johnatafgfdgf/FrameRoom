# FrameRoom R0.6 — lote experimental LSFG 2× em Vulkan/AHB

**Estado:** a R0.5 compilou com sucesso no GitHub Actions; a R0.6 adiciona um teste que passa de inicialização a **chamar o motor real de Frame Generation** (`bb_lsfg_create_context`, `bb_lsfg_present`, `bb_lsfg_wait`) com dois quadros sintéticos criados pela GPU Vulkan. **Não exibe o quadro interpolado e não executa jogos num espaço paralelo.** Nenhuma alegação de FPS extra em aplicativos.

## O que foi criado

- `framegen_batch.cpp`: renderizador Vulkan headless (640×360), duas imagens AHardwareBuffer independentes, conteúdo sintético GPU, output AHardwareBuffer, chamada Bloodframe real e espera pela conclusão do lote.
- JNI `NativeProbe.frameGenBatchReport` e botão na interface, com diagnóstico distinto para inicialização, importação Vulkan, buffer, contexto e processamento.
- Preserva o demo Vulkan, a seleção de atalhos e a importação privada da `Lossless.dll`.
- GitHub Actions continua construindo `libbloodframe.so` a partir de fonte MIT com hash fixo. A DLL não é distribuída.

## Testes e limites

1. Compilar APK R0.6 na aba Actions e instalar no POCO X6 5G.
2. Importar uma DLL legítima do Lossless Scaling.
3. Tocar em **Diagnosticar Bloodframe + DLL + Vulkan** para checar shaders/driver.
4. Tocar em **Teste experimental: gerar 1 quadro LSFG (sem exibição)**. O retorno comprova somente que o pipeline retornou sucesso ao processar o lote. **Não demonstra imagem visível correta, ausência de artefatos ou desempenho**.
5. Não existe ainda reaproveitamento da saída para apresentação na tela, pacing 2× ou suporte a aplicativos de terceiros sem root.

### Sobre o espaço paralelo

O Android não permite injeção geral em aplicativos não depuráveis sem autorização; os atalhos atuais não são uma VM. A próxima fase deve usar um aplicativo renderizador cooperante ou uma sandbox realmente comprovada, não prometer universalidade.

### Licenças

Bloodframe 2.0 e dependências seguem seus termos (MIT, com notices upstream). Os shaders do LSFG não estão no APK; o usuário importa a própria Lossless.dll.
