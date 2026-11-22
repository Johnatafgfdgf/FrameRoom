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
