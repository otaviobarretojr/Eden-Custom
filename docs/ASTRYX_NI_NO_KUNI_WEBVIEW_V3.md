# ASTRYX Ni no Kuni — WebView V3: completion protocol + diagnostics

## Evidence from user log, build 268b047063
- Five cycles of opening `pp.htdocs/index.html?1` at 140.2, 172.6, 180.5, 185.4 and 193.5 seconds.
- `[ASTRYX WebView] Opened local Ni no Kuni HTML` logged five times, but this line is immediately after `loadUrl` and does not prove `onPageFinished`.
- Missing shared font RomFS errors and a firmware NCA type warning persist, but no evidence that they alone cause loop.
- WebView V2 did not implement Eden desktop's `window.nx.endApplet` JS compatibility script.
- Returning from V2's manual button used `WebExitReason::ExitRequested`, unlike the desktop's `WebExitReason::EndButtonPressed`.

## V3 experiment
- Add restricted document-start `window.nx` shim matching required nonprivileged subset of Eden Qt WINDOW_NX_SCRIPT.
- Poll `end_applet` and return `EndButtonPressed` as on desktop. Recognize localhost callback URL and return `CallbackURL`. Cancellation returns `WindowClosed`.
- Use AndroidX WebKit document-start injection; log unsupported provider fallback, page completion, main-frame errors, user closure and native exit reason.
- Preserve S25 Ultra APK appId/signature, firmware, saves, graphics drivers, library 2.0, and offline mod ZIP/RAR import. No online mod servers.
- Do not state this fixes the game until confirmed on the actual device.
