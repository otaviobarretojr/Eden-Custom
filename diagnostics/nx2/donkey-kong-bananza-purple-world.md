# Donkey Kong Bananza — fundo lilás e HUD visível

Observação em 01/11/2026: no Eden Custom NX2, após iniciar com dados limpos, gameplay mostra HUD normal, mas mundo 3D lilás. Status: Vulkan, FAST, DOCKED, escala 1x, compilando 38 shaders, 6 FPS, 114.42 ms/frame. Hardware do usuário: Ryzen 5 5600X, RTX 5060 Ti 16 GB, 32 GB RAM, Windows 11.

## A/B de isolamento
1. Sem alterar outras opções, pressionar D-pad para baixo para entrar e sair do modo fotografia; ver relatos em https://git.eden-emu.dev/Franseven/PokeDinner-a-Eglantine-Fork/issues/15 e https://github.com/zortos293/Eglantine/issues/3 .
2. Testar `EDEN_NX2_TENSOR=1` no `%APPDATA%\eden\nx2_switches.txt`, com aviso de compilação demorada; depois retirar para testar novamente. https://github.com/zortos293/Eglantine/issues/2 documenta fundo lilás/HUD e erro 'native matrix multiply'.
3. Registrar `%APPDATA%\eden\log\eden_log.txt` depois da tentativa, e comparar Preview 01 com build de controle sem GPU split patch.
4. Conferir se fallback `UpscaleInsteadOfNativeProgram` para shaders rejeitados está sendo invocado, e se formatos, superfície de entrada/saída e sincronização estão corretos.
5. Separadamente investigar vídeo VP9/erro 2111-3604 (depois do fim do vídeo), sem misturar com a falha de renderização.
6. Não mesclar https://github.com/zortos293/Eglantine/pull/5 automaticamente: a própria PR ressalva que o cooperative matrix lowering é experimental, não prova conserto do fundo lilás e depende de suporte Vulkan específico.
7. Não prometer aumento de FPS antes de corrigir cenário e medir frametime/FPS com mesmo jogo e cena.

Nenhum código de shader alterado ou corrigido nesta entrada. A versão Preview 01 permanece preservada; faltam log e teste em runtime para implementação orientada por evidências.
