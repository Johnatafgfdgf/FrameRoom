# FrameRoom R0.7 — APK snapshots + processo gráfico separado

**NÃO executa clones de jogos ainda.** A meta continua sendo um espaço paralelo rootless que controle o pipeline Vulkan do aplicativo e aplique Bloodframe com a DLL do usuário.

## O que a R0.7 acrescenta
- A partir de um jogo instalado e visível ao Android, o FrameRoom tenta copiar o APK base e todos os APKs split para seu armazenamento privado. Cada arquivo recebe SHA-256 e validação ZIP, e a versão do pacote é registrada.
- Identifica heurísticas de Unity, Unreal e bibliotecas ARM64, sem prometer que um jogo usa Vulkan ou que será compatível com um runtime.
- O usuário pode remover só as cópias privadas. Isso não modifica, reinstala nem lê os dados privados do aplicativo original.
- A demonstração Vulkan própria é executada no processo secundário :virtualgfx, sob controle do FrameRoom; não corresponde à execução de um APK de terceiros.
- Preserva a importação da Lossless.dll, testes Bloodframe 2x, Vulkan/AHB e a UI atual.

## Limites
Copiar um APK não é clonar a instalação: não copia contas, dados, OBB, Play Asset Delivery ou serviços. O arquivo copiado não é executado. Jogos que dependem de Play Integrity, DRM, Google Play Services ou anticheat podem ser incompatíveis com virtualizadores.
Sem root, camadas Vulkan externas não podem ser injetadas universalmente em jogos Android não depuráveis. O único caminho potencial é um runtime que efetivamente hospede o jogo no processo controlado e permita fornecer quadros ao motor LSFG. Isso precisa ser demonstrado em jogo de teste.

## Próximas etapas técnicas
1. Integrar um runtime Android comprovado, começando por APKs de teste sem DRM, como VirtualApp (Apache-2.0), com as adaptações e testes necessários ao Android 16.
2. Validar execução independente e permissões, lifecycle, input, áudio e Vulkan.
3. Expor as imagens da renderização do clone ao Bloodframe com buffers AHardwareBuffer e sincronização correta.
4. Mostrar quadros realmente gerados e medir FPS original/apresentado e latência na Adreno 710.

Fontes: https://github.com/ISEKHON/VirtualApp e https://github.com/ducvd89/bloodframe.
Esta R0.7 é experimental; o APK será distribuído somente quando o workflow de compilação passar.