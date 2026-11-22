# FrameRoom R0.8 — APK cooperativo executado pelo FrameRoom (prova de conceito)

**Esta R0.8 não é um Parallel Space universal.** Ela acrescenta uma demonstração verificável de execução de código de um APK independente dentro do processo do FrameRoom, sem root, por um mecanismo estritamente limitado a um APK de teste de mesma assinatura.

## Para testar a execução

1. No GitHub Actions, baixe **do mesmo build** o APK `FrameRoom-r0.8-debug-apk` e o `FrameRoom-r0.8-cooperative-demo-apk`.
2. Instale **ambos**. Se a instalação do FrameRoom falhar por assinatura, desinstale a versão debug anterior antes. Isso remove os dados do FrameRoom, incluindo DLL e snapshots, então guarde seus próprios arquivos antes.
3. Abra FrameRoom → Adicionar aplicativo → **FrameRoom Test Game**.
4. Toque **Preparar cópia dos APKs deste jogo** para criar e validar uma cópia privada.
5. Após a preparação, toque **Executar cópia do JOGO DE TESTE dentro do FrameRoom**.
6. O jogo de teste executará seu código de APK (Canvas) no processo secundário `:virtualgame`, sem abrir a `SampleActivity` original. A tela responde ao toque.

O mesmo aplicativo de demonstração também pode ser aberto normalmente fora do FrameRoom para comparação. A classe é carregada de forma dinâmica de um APK privado, não da Activity original.

## Propriedades e limites

- **Funcional de propósito restrito**: apenas o pacote `dev.frameroom.samplegame`, criado e assinado com o mesmo certificado do host, oferecendo um ponto de entrada cooperativo `createView(Context)`.
- O host valida assinatura, integridade SHA-256, correspondência com o APK instalado e somente carrega arquivos somente leitura (Android 14+).
- O processo `:virtualgame` é separado, mas compartilha o mesmo UID Android do FrameRoom. Não é sandbox endurecida nem isolador de aplicativos não confiáveis.
- Não reproduz Activities, Services, ContentProviders, PackageManager, Google Play Services, recursos externos nem bibliotecas nativas arbitrárias de um jogo de terceiros. Jogos comerciais **não são compatíveis** com este protocolo.
- **O jogo de teste usa Canvas.** Ele não rende por Vulkan nem usa Bloodframe. Os experimentos Vulkan/LSFG continuam disponíveis em outras telas, mas ainda não estão conectados a um jogo virtualizado.
- O app não concede permissões extras nem tenta contornar DRM, Play Integrity ou anticheat.

## Para atingir seu objetivo de verdade

O próximo marco é substituir o protocolo cooperativo por um runtime real capaz de hospedar Activities e bibliotecas nativas de um jogo compatível, com estado separado. Só depois o runtime precisa entregar seus buffers de Vulkan ao motor Bloodframe, apresentar os frames gerados e medir latência. A compatibilidade com jogos proprietários continua **não comprovada**, especialmente Android 16.

Referências de restrições: https://developer.android.com/about/versions/14/behavior-changes-14 e https://developer.android.com/privacy-and-security/security-tips

Código base das fases anteriores preservado. Consulte também `docs/R08-COOPERATIVE-RUNTIME.md`.
