# FrameRoom R0.12

A R0.12 usa um perfil de trabalho do Android para abrir cópias de aplicativos compatíveis com contas e armazenamento próprios. Na biblioteca, abra **Espaço paralelo → Criar espaço** e aceite a configuração do Android. Depois escolha o aplicativo, confirme a instalação se solicitado e toque em **Abrir no espaço**. O Android usa um ícone com maleta para identificar os apps do perfil.

O Android compõe a tela dos apps clonados. O FG continua disponível na Arena incluída e na demonstração Vulkan. O APK R0.12 instala ao lado da R0.11. Importe sua Lossless.dll pela aba **Motor** para usar a Arena com FG.

[Espaço paralelo e testes](docs/R12-PARALLEL-SPACE.md) · [Cadência e transporte Vulkan](docs/R11-PACING-GUEST.md)

## Arena e demonstração Vulkan

1. Instale o APK ARM64 de **FrameRoom** do build disponível em [GitHub Actions](https://github.com/Johnatafgfdgf/FrameRoom/actions). A Arena já vem incluída.
2. Em **Motor**, importe sua **Lossless.dll** e abra a **Demonstração Vulkan**. Use **Ligar FG 2×** para executar a interpolação ou desligue no mesmo controle para comparar.
3. Na biblioteca, abra **Arena FrameRoom → Jogar**. Em **Configurar FPS e FG**, defina o limite original e a preferência de FG. Arraste o quadrado verde até o alvo laranja para verificar toque, áudio e progresso.
4. **Copiar diagnóstico** inclui o transporte AHB, o custo do lote, a verificação de pixels e o status da apresentação. A aba **Avançado** inclui os testes isolados de Vulkan, AHB e Bloodframe.

O teste usa 640×360 no compositor FG, com ajuste 16:9 à superfície. O overlay exibe quadros confirmados pelo driver quando `VK_GOOGLE_display_timing` responde. Em outros aparelhos, exibe envios à swapchain. A latência registrada vai da amostra da cena até o horário informado pelo driver.

## Compatibilidade

- **Aplicativos comuns compatíveis** podem ser instalados e abertos num perfil FrameRoom por meio do APK base e dos splits acessíveis. O perfil começa com armazenamento separado. Apps que dependem de OBB, downloads, serviços ou políticas específicos podem exigir configuração própria ou recusar o perfil.
- **APK cooperativo**, com contrato v1 e assinatura do FrameRoom, pode executar a cena hospedada com acesso ao compositor LSFG. O host usa o UID do FrameRoom e atribui uma pasta de dados ao jogo.
- **Adreno 710:** na R0.11, o vídeo do POCO X6 mostrou a demonstração com cerca de 29,5 originais/s e 42,3 apresentados/s, incluindo 13 gerados/s. Na Arena, apresentou cerca de 32–33 quadros/s com 29 originais/s. O ganho varia com a cena e a capacidade disponível.

[Integração, auditoria de viabilidade e roteiro de testes](docs/R10-VULKAN-HOST.md) · [Histórico](CHANGELOG.md)

Bloodframe MIT, fixada em [`f4ac713a`](https://github.com/ducvd89/bloodframe/tree/f4ac713aef58d9403a136e179b3b0d163d85eb66). Importe a sua DLL no aparelho.
