# FrameRoom R0.9 — correção do AHardwareBuffer na Adreno 710

**Correção baseada em evidência do aparelho:** R0.8 conseguiu inicializar Bloodframe 2.0 com uma Lossless.dll na GPU Adreno (TM) 710. Shaders selecionados: FP32 SPIR-V; `ahb_transport=direct-storage`, `input_copy=0`, `output_copy=0`, `sync_fd=1`, AHB input flags=768 e output flags=16777984. Ao tentar gerar 1 quadro, `AHardwareBuffer_allocate` retornou 1. A demonstração Vulkan direta e AHB com permissões regulares funcionou em vídeo enviado pelo usuário.

## O que mudou
- `tools/patch_bloodframe_ahb.py`: antes de construir Bloodframe no CI, testa a alocação REAL do AHardwareBuffer RGBA8 usando `GPU_DATA_BUFFER`, além do Vulkan format probe. Se o Android gralloc rejeitar essa combinação, desabilita apenas o transporte de saída direct-storage e faz a seleção padrão do upstream escolher `direct-input-copy-output` ou `transport-only` conforme capacidades verificadas. Não força formatos incompatíveis, nem remove flags que o backend exige.
- `framegen_batch.cpp`: agora reporta qual buffer falhou (entrada 0/1 ou saída FG), flags exatas, `AHardwareBuffer_isSupported` e código de alocação; o resultado de sucesso discrimina `input_copy` e `output_copy`.
- Mantém a DLL importada pelo usuário, Bytecode cooperativo de teste e snapshots APK da R0.8. Nenhuma DLL proprietária é distribuída.

## O que testar no POCO X6
1. Baixe o FrameRoom r0.9 debug do GitHub Actions desta branch após build verde.
2. A DLL já importada pode continuar válida se atualizar sobre o APK com a mesma assinatura.
3. Toque **Diagnosticar Bloodframe + DLL + Vulkan**. O transporte esperado SE o problema for mesmo o modo direct-storage é `direct-input-copy-output`, com `output_copy=1` e usage de saída sem `GPU_DATA_BUFFER`.
4. Toque **Teste experimental: gerar 1 quadro LSFG (sem exibição)**. O resultado será uma chamada real de `create_context/present/wait`, mas o quadro produzido ainda não é mostrado.
5. Se falhar, anote a mensagem completa; os diagnósticos agora mostram o buffer, uso e etapa exatos.

## Limitações
- **Não executa jogos Android comerciais em sandbox e não aplica LSFG ao Sparking ZERO.** Os APKs preparados continuam arquivos inertes. O processo do jogo de teste R0.8 é um carregador cooperativo e não virtualiza apps arbitrários.
- Mesmo sucesso do processamento sintético não prova correção visual dos quadros, FPS ou latência do jogo.
- A seleção alternativa é um fallback seguro; pode consumir mais largura de banda da GPU e precisa de teste real.

Créditos à Bloodframe (MIT), commit upstream fixado `f4ac713aef58d9403a136e179b3b0d163d85eb66`. A modificação é aplicada apenas na compilação, documentada e testada contra as âncoras da versão fixada.