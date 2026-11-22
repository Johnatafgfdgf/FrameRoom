# R0.11 — cadência de FG e Arena incluída

- Remove a espera de um período extra após atraso e organiza a apresentação dos quadros pelo relógio da tela.
- Preserva a resolução dos originais e reduz a resolução da interpolação quando falta tempo. Retoma Vulkan direto se a geração continuar a afetar a cadência.
- Inclui o APK cooperativo da Arena dentro do FrameRoom. O build de teste instala ao lado da R0.10.
- Registra os resultados do POCO e o requisito de sincronização para compor quadros de um Android convidado.

# R0.10 — apresentação Vulkan e biblioteca mobile

- Envia o resultado da Bloodframe à swapchain, com dois originais distintos, validação inicial de pixels, pacing e fallback Vulkan direto.
- Corrige a importação AHB e verifica o suporte de gralloc e de sync FD no backend pinado.
- O APK Arena controla toque, áudio e progresso por um contrato cooperativo. O FrameRoom executa sua lógica autenticada e renderiza a cena.
- Biblioteca de jogos, pesquisa, opções por jogo, central Lossless.dll e diagnósticos avançados. Overlay distingue envios e exibição observada.
- CI compila os dois APKs e verifica assinatura, ABI e ausência de DLL empacotada. A validação de hardware permanece registrada no roteiro de teste.

# R0.9 — fallback AHardwareBuffer na Adreno 710

- Investigação do erro de hardware `AHardwareBuffer_allocate 1` com LSFG inicializado e `ahb_transport=direct-storage`.
- Correção da detecção de suporte no Bloodframe no build: gralloc real é testado antes de selecionar direct-storage; fallback para saída por cópia GPU.
- Diagnóstico detalhado de entrada/saída AHB e flags.
- CI gera APK R0.9 e aplica patch auditável ao upstream MIT.
- Ainda não apresenta quadros LSFG nem executa jogos em um espaço paralelo universal.

# R0.8 — primeiro APK cooperativo rodando dentro do FrameRoom
- App de demonstração Android próprio compilado como segundo APK e assinado no mesmo build.
- Um processo `:virtualgame` separado carrega bytecode do APK copiado por `DexClassLoader` após verificar certificado e SHA-256.
- UI adiciona botão para executar somente o demo; outros apps continuam limitados a snapshots/atalhos.
- O demo usa Canvas, sem Vulkan/LSFG. Não corresponde à clonagem de jogos comerciais.

# R0.7 — preparação de pacote e processo gráfico
- Snapshot privado do APK base e splits instalados, com SHA-256 e validação ZIP.
- Indicadores de versão/motor estimado/ABI; aviso explícito de cópia não executável.
- Renderizador Vulkan de demonstração no processo Android :virtualgfx.
- Não implementa ainda clonagem executável nem Frame Generation de jogos externos.

# R0.6 — lote GPU LSFG 2× (sem apresentação)
- FrameGen batch headless 640×360, duas entradas Vulkan AHB e uma saída com flags do backend.
- `create_context`, `present` com um quadro gerado e espera de sincronização.
- Botão de teste com diagnóstico; resultados na Adreno 710 ainda não comprovados.
- Não é espaço paralelo e ainda não exibe frames interpolados.

# R0.5 — Inicialização Bloodframe LSFG em APK (diagnóstico)
- Bibliotecas nativas Bloodframe compiladas pelo CI a partir de versão fixada.
- JNI usa `bb_lsfg_init`, relata shaders e recursos AHB ou mensagem real de erro.
- Interface oferece diagnóstico explícito; não afirma gerar quadros.
- Não implementa ainda virtualização Android nem FG em aplicativos.

# R0.4

- Dois AHardwareBuffer importados separadamente como origens de quadros consecutivos, com alternância por frame.
- Getter não proprietário para AHardwareBuffer, destinado a futura integração de motor.
- Limpeza de CMake e atualização de checagens estáticas.
- Ainda sem virtualização Android, Bloodframe ou LSFG ativo.

# Changelog

## R0.3 (22 novembro 2026)
- Adicionado `AhbOffscreen`: importa AHardwareBuffer RGBA8 como imagem Vulkan mediante `VK_ANDROID_external_memory_android_hardware_buffer` e verificações de capacidade.
- Adicionada animação por staging buffer com comandos de transferência Vulkan, executados diretamente no AHB.
- Adicionada cópia GPU do AHB para a swapchain e transições explícitas de layout.
- Nova alternância entre demo direta R0.2 e pipeline experimental AHB; status e falhas visíveis.
- Preservados importador de DLL, atalhos, seletor de apps e diagnósticos; incrementada versão para R0.3.
- Adicionadas checagens estáticas e workflow de testes da integração.
- **Não são FG, captura de outros apps, virtualização nem integração com Bloodframe.**


## R0.2 (22 novembro 2026)
- Preservados importador de DLL, diagnóstico Vulkan e abertura externa de aplicativos.
- Novo experimento Vulkan nativo em SurfaceView: swapchain, render pass, quadro animado, meta 30/60 FPS, redimensionamento, lifecycle e tratamento de falhas.
- Teste de alocação AHardwareBuffer (não confundir com integração Vulkan/AHB).
- Biblioteca persistente de apps e seletor pesquisável; abertura externa é rotulada corretamente.
- Validador PE independente de Android e teste de regressão Java.
- SHA-256 da DLL para diagnóstico e substituição por movimentação atômica, quando suportada.
- CI com logs de build enviados mesmo na falha.
- Sem LSFG real, sem clonagem de APKs e sem APK validado; consulte README.
