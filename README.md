# FrameRoom R0.10

FrameRoom é um aplicativo Android ARM64 que executa jogos cooperativos e integra o LSFG da Bloodframe a um renderizador Vulkan próprio. A R0.10 inclui uma cena de teste e a Arena, cujo código de jogo vem de um APK separado. Os dois caminhos usam o mesmo renderizador.

## Instalação e teste

1. Instale **FrameRoom** e **Arena FrameRoom** do mesmo build disponível em [GitHub Actions](https://github.com/Johnatafgfdgf/FrameRoom/actions). Os dois APKs precisam ter a mesma assinatura. Se o instalador do Android rejeitar uma atualização por assinatura diferente, confira a origem do APK instalado. Desinstalar apaga os dados locais daquele aplicativo.
2. No FrameRoom, abra **Motor**, importe sua **Lossless.dll** e abra a **Demonstração Vulkan**. Use **Ligar FG 2×** para executar a interpolação, ou desligue no mesmo controle para comparar.
3. Na biblioteca, use **Adicionar aplicativo** e selecione **Arena FrameRoom**. Em **Configurar**, defina o limite original e a preferência de FG. **Jogar** prepara uma cópia local, verifica o APK e carrega a cena no processo do FrameRoom. Arraste o quadrado verde até o alvo laranja para verificar toque, áudio e progresso.
4. **Copiar diagnóstico** inclui o caminho de transporte AHB, custos do lote, resultados da leitura de pixels e status da apresentação. A aba **Avançado** preserva os testes isolados de Vulkan, AHB e Bloodframe.

O teste usa 640×360 no compositor FG, com ajuste 16:9 à superfície. O overlay exibe quadros confirmados pelo driver quando `VK_GOOGLE_display_timing` responde. Em outros aparelhos, exibe envios à swapchain. A latência registrada vai da amostra da cena até o horário informado pelo driver.

## Compatibilidade

- **APK cooperativo**, com contrato v1 e assinatura do FrameRoom, pode executar a cena hospedada. O host usa o UID do FrameRoom e atribui uma pasta de dados ao jogo.
- **Aplicativos instalados sem o contrato** podem ser adicionados à biblioteca e ter o APK base e os splits copiados, quando acessíveis. A execução hospedada e o FG ficam indisponíveis para esses pacotes.
- **Sparking! ZERO, `org.nx2a.t010035f022078000`**, ainda precisa de inspeção dos arquivos e do renderizador do port. Uma cópia de 5,9 MB não identifica OBB, pacotes de recursos externos ou o uso real de Vulkan.
- **Adreno 710** é o alvo do próximo teste físico com a DLL do usuário. A inicialização na R0.8 foi observada. A geração e a apresentação na R0.10 exigem teste no aparelho.

[Integração, auditoria de viabilidade e roteiro de testes](docs/R10-VULKAN-HOST.md) · [Histórico](CHANGELOG.md)

Bloodframe MIT, fixada em [`f4ac713a`](https://github.com/ducvd89/bloodframe/tree/f4ac713aef58d9403a136e179b3b0d163d85eb66). Importe a sua DLL no aparelho.
