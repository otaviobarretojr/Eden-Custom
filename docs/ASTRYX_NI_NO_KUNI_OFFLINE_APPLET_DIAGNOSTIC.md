# ASTRYX — Ni no Kuni: loop de abertura, teste de applet offline

## Evidências do log real (18/10/2026)
- Android S25 Ultra build `915fb355c8`, Ni no Kuni `0100E5600D446000`.
- Firmware informado `NintendoSDK Firmware for NX 23.0.0-4.0`; Qualcomm Adreno Vulkan 512.800.64.
- Opção global `Debugging.disable_web_applet: true` e `LibraryApplet.offline_web_applet_mode: LLE`.
- 12 loops idênticos (t~39 a t~78 s) incluem `File 1EED56785513B285251A392ADDFF4D63.nca has a different type (unknown)` e `Web Browser Applet is disabled. shim_kind=3`.
- `ShimKind::Offline = 3`. O jogo tenta repetidamente iniciar um applet offline; não há crash fatal registrado.
- O backend de páginas locais no Android ainda está stubado: `AndroidWebBrowser::OpenLocalWebPage` retorna `WindowClosed` sem exibir HTML.
- Precisão GPU está em `Low`; isto não explica por si só a falha do applet.

## Experimento de compatibilidade v1 — não significa jogo consertado
- Bypass da opção `disable_web_applet` somente quando o título é `0100E5600D446000` e o applet solicitado é `ShimKind::Offline`.
- A verificação é aplicada tanto na inicialização quanto na execução do applet. Não altera preferências nem outros jogos.
- A falha seguinte (por exemplo `RomFS ... cannot be extracted` ou backend `(STUBBED)`) ficará explícita no log do aparelho.
- O patch NÃO disponibiliza interface de browser LLE, não modifica firmware nem fabrica saves.
- Não alterar `nand`, `save`, shaders ou drivers automaticamente.

## Teste no celular
1. Instalar APK por cima da build anterior apenas se assinatura e pacote coincidirem. Fazer backup dos saves importantes.
2. Abrir Ni no Kuni e observar se a tela inicial avança.
3. Se continuar em loop, exportar novo `eden_log.txt` após ~40 segundos para identificar se a falha restante é NCA/firmware, extração de HTML ou falta do backend Android.
4. Confirmar que os outros jogos e Biblioteca 2.0 continuam funcionando.
