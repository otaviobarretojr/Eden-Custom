# ASTRYX Ni no Kuni — Offline HTML WebView v2 (experimental)

## Evidence
- Build 6ccd8df032 reached `ExecuteOffline` five times with `pp.htdocs/index.html?1`.
- Each attempt ended immediately at `OpenLocalWebPage: (STUBBED)`.
- Font RomFS extraction also failed for system archives 0810–0814; not the same as missing title HTML.
- No change to saves, installed firmware or graphic drivers.

## Implementation
- Only allow URL paths from the Ni no Kuni game ID `0100E5600D446000`.
- Before opening, extract the requested offline RomFS if the HTML page is not already available.
- The native bridge launches a bounded modal WebView from a JNI worker, never calls the JVM directly on a CPU fiber.
- WebView only accepts local files in the emulator-owned offline HTML directory. External network loads and cross-file access are disabled; no JavaScript interface is exposed.
- The modal page has **Voltar ao jogo** and **Cancelar** actions. The response is delivered back to the original applet request.
- It is a test, not a claim of full Nintendo WebSession compatibility or a verified Ni no Kuni fix.
- No servers for mods and translations are restored, and existing ZIP/RAR import is unchanged.

## Validation
- Code checks confirm single-title target, JNI signature, document extraction, local sandbox, and disabled network.
- APK build, signature and actual Android runtime still require verification.
