# FrameRoom R0.5 — diagnóstico real Bloodframe / LSFG

Esta versão continua a R0.4 e **não é o FrameRoom final**. Ainda não existe espaço paralelo funcional nem apresentação de quadros gerados para jogos. Os apps escolhidos continuam abrindo fora do FrameRoom.

## Implementado na R0.5
- GitHub Actions compila a biblioteca **Bloodframe 2.0** do código-fonte MIT em commit fixado `f4ac713aef58d9403a136e179b3b0d163d85eb66`.
- A DLL original do usuário permanece em armazenamento privado. O APK não contém DLL nem shaders proprietários.
- O botão **Diagnosticar Bloodframe + DLL + Vulkan** carrega `libbloodframe.so`, cria instância Vulkan, consulta UUID de dispositivo e driver, chama `bb_lsfg_init`, relata shader e configuração AHB ou erro do driver, e libera os recursos.
- A demonstração Vulkan / AHB duplo da R0.4 foi mantida.

## Não implementado
- `bb_lsfg_create_context`, `bb_lsfg_present`, apresentação de quadros interpolados ou métricas de FPS gerados. O diagnóstico é inicialização, **não Frame Generation**.
- Execução de apps Android terceiros dentro do FrameRoom. Não existe sandbox ou clonagem real sem root nesta versão.

## Teste no POCO X6 5G
1. Compile pelo workflow GitHub Actions ou baixe o APK debug quando houver run bem-sucedido.
2. Importe sua própria `Lossless.dll`.
3. Toque em **Diagnosticar Bloodframe + DLL + Vulkan**, copie o resultado da tela.
4. Se houver falha no build, compartilhe o artefato `FrameRoom-r0.5-build-log`.

Este relatório distingue compatibilidade de shader/GPU de execução de um jogo; o teste em Adreno 710 continua necessário.

Próximo marco: criar AHBs com os flags exatos retornados por `bb_lsfg_init`, ligar os buffers à API `create_context/present`, sincronizar saída e validar um quadro interpolado na cena própria. Só depois tentar ambientes Android compatíveis.

## Licenças
FrameRoom: MIT. A biblioteca Bloodframe e seu cabeçalho vêm do projeto [ducvd89/bloodframe](https://github.com/ducvd89/bloodframe), MIT, com crédito em `app/src/main/cpp/third_party/BLOODFRAME-NOTICE.md`.
