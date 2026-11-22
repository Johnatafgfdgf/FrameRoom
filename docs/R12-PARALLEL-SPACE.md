# Espaço paralelo R0.12

Use **Jogos → Espaço paralelo → Criar espaço** e conclua a configuração do Android. Selecione **Escolher aplicativo**, toque em **Clonar** e abra a cópia pelo resultado da instalação. Para voltar ao perfil, use **Abrir espaço** ou o ícone FrameRoom com maleta. O perfil de trabalho precisa estar ativo. A opção de criação depende do fabricante, do suporte a perfis gerenciados e das políticas do aparelho.

## Como funciona

O FrameRoom é o controlador do perfil que cria com `ACTION_PROVISION_MANAGED_PROFILE`. `SpaceProfile` configura dois filtros de intenção para encaminhar as telas de instalação e da biblioteca. A tela de destino só funciona quando o FrameRoom é dono do perfil que a recebeu.

O FrameRoom encaminha o identificador do pacote escolhido. A cópia do FrameRoom no perfil abre o instalador do sistema com `ACTION_INSTALL_PACKAGE` e uma URI `package:`. Esse fluxo do Android disponibiliza o pacote existente com APK base, assinatura e splits, sem reempacotar os arquivos. O Android dá à cópia dados, contas e permissões daquele usuário e mantém a versão do código compartilhada quando o mesmo pacote existe em dois perfis. Apps que disponibilizam recursos por download ainda precisam obtê-los no novo perfil. [Perfis de trabalho](https://developer.android.com/work/managed-profiles) · [Intent do instalador](https://developer.android.com/reference/android/content/Intent#ACTION_INSTALL_PACKAGE)

## Acesso aos quadros

O perfil usa SurfaceFlinger. As APIs públicas de perfil e de exibição virtual não transferem ao FrameRoom as superfícies Vulkan de outros aplicativos. Uma composição com FG precisa de acesso ao produtor de imagens e à sincronização de GPU. A Arena continua com o pipeline autorizado, onde o FrameRoom é dono do renderizador. O perfil também funciona sem importar uma DLL. [Restrições de exibição e lançamento](https://source.android.com/docs/core/display/multi_display/activity-launch)

O Wayondroid é um caminho de pesquisa para um convidado com compositor controlável. A implementação rootless em `99f64b19` usa o UID do host para processos convidados e documenta limitações na separação de diretórios privados de apps. A entrega de AHardwareBuffer ao `SurfaceControl` usa fence de entrada `-1`, e o upstream documenta a possibilidade de tearing. A compilação do código nativo ARM64 em ambiente local passou. Ainda faltam provas de isolamento e sincronização em execução no Android 16 para esse caminho. [Runtime](https://github.com/yoro1836/wayondroid/blob/99f64b19a62500f03ee3b82175fe06ebcf54299c/docs/rootless.md) · [Apresentação](https://github.com/yoro1836/wayondroid/blob/99f64b19a62500f03ee3b82175fe06ebcf54299c/app/src/main/cpp/display/gpu_presenter.cpp)

## Verificação

- `bash tools/test-all.sh` exercita a validação de pacotes escolhidos para clonar, além dos testes de PE, detecção de gralloc, pacing e verificação de frames.
- O build de CI compila o APK ARM64, verifica assinatura e bibliotecas incluídas e preserva os logs da execução.
- No POCO X6 com Android 16, falta validar criação do perfil pelo sistema, instalação de pacote base e splits, inicialização, entrada, áudio, retorno de suspensão e persistência independente. A versão R0.12 ainda não tem medição de GPU nesse aparelho.
