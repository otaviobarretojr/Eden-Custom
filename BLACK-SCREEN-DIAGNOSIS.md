# Diagnóstico de tela preta — Eden Custom NX2

**Caso:** o emulador abre, mas o jogo fica em tela preta. Título ainda não identificado.

## Build de controle (sem patch de GPU)

GitHub Actions: `NX2 - Black Screen Control Build (Original GPU Core)` neste branch.
Esta versão usa EXATAMENTE a mesma revisão da fonte NX2 (commit `1dd40d86df4bca1b68700091fe5ddb71a13162b2`), mesma compilação MSVC Release AVX2, mesmas bibliotecas e ferramentas da Preview 01.

**Única diferença intencional:** NÃO executa `tools/nx2/apply_core_patch.py`, preservando `src/video_core/gpu_split.cpp` original. É um teste de controle, não uma alegação de reparo garantido.

## Como testar (sem apagar saves)

1. No emulador, restaurar as configurações conservadoras de gráficos antes de cada teste: GPU Mode Accurate, DMA/Fence Default, VRAM Conservative, ASTC GPU + Uncompressed, Sync Memory Operations e Reactive Flushing ATIVADOS, cache Vulkan/Persistent ATIVADOS; frame generation DESATIVADO.
2. Fechar totalmente o emulador após mudar opções.
3. Rodar o mesmo jogo, mesma cena, mesma fonte de jogo e driver, e registrar se aparece imagem, som, ou travamento.
4. Preview 01: `Eden-Custom-NX2-Zen3-RTX5060Ti-Windows-x64.zip` (GPU modificada).
5. Controle: `Eden-NX2-BlackScreen-CONTROL-Unpatched-GPU-Win64` em Actions / Artifacts (GPU original).
6. Se disponível, repetir com PokeDinner Eglantine original 2.1.0, SEM migrar ou sobrescrever saves/configurações.
7. Coletar apenas o log do emulador da tentativa com tela preta. Não compartilhar jogos, firmware, chaves ou saves. Revise caminhos pessoais presentes nos logs antes de enviar.

### Interpretação dos testes

| Preview modificado | Controle sem patch | Diagnóstico inicial |
|---|---|---|
| Tela preta | Funciona | Forte evidência de regressão no patch GPU; reverter e trabalhar mudanças separadas |
| Tela preta | Tela preta | O patch isolado não explica o problema; investigar compatibilidade do jogo, configurações, firmware, drivers, Vulkan e base Eglantine |
| Funciona após reset | Funciona | Configuração anterior era suspeita; comparar alterações uma a uma |
| Tela preta | Tela preta; original 2.1.0 funciona | Revisão upstream, diferenças de empacotamento/compilação ou configuração; coletar log de inicialização |

## Logs: o que procurar
- Mensagens com `ERROR`, `CRITICAL`, `Vulkan`, `nvn`, `shader`, `loader`, `CPU`, `GPU`, `firmware`.
- Primeiro erro reproduzível imediatamente após escolher o jogo.
- Se o programa encerra, conferir o Windows Reliability Monitor/Visualizador de Eventos para o módulo que falhou.
- Informar nome e versão do jogo, se Switch 1 ou Switch 2, e se o PokeDinner 2.1.0 original inicia o mesmo jogo.

**Sem nome do jogo e log, não é possível afirmar que o problema é especificamente nosso patch nem prometer compatibilidade.**

## Precauções
- Cada build deve ser extraído em pasta separada.
- Não apagar save nem cache imediatamente; antes, cópia de segurança.
- O teste não requer instalar drivers novos ou substituir firmware.
- O pacote de controle expira após 14 dias como artefato GitHub Actions; uma release separada só será publicada após análise comparativa.
