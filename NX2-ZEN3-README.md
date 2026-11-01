# Eden Custom NX2 — edição Ryzen 5 5600X / RTX 5060 Ti

Esta ramificação gera **um executável Windows próprio, compilado a partir do código de emulação de Switch 2 do Eglantine/PokeDinner**, com modificações em C++. Não é launcher, gerenciador de perfis ou configuração de ambiente.

## Origem e isolamento
- Repositório de destino: `otaviobarretojr/Eden-Custom`, ramificação `nx2-zen3-5060ti`.
- Fonte NX2: `https://git.eden-emu.dev/Franseven/PokeDinner-a-Eglantine-Fork`.
- Revisão de origem fixada: `1dd40d86df4bca1b68700091fe5ddb71a13162b2` (1º/11/2026).
- `master` do Eden-Custom fica intocado; a fonte NX2 é obtida e modificada durante a compilação, sem copiar milhares de arquivos de terceiros para o repositório pessoal.

## Alterações reais do motor
No arquivo `src/video_core/gpu_split.cpp` da fonte NX2:
1. Guarda de entrada que ignora o diagnóstico de propriedade de thread quando `check_owner` está desligado.
2. Escritas atômicas `g_back_busy` não são realizadas fora do modo diagnóstico.
3. A devolução de espaço do anel de comandos GPU acontece em lotes de 2048 palavras, em vez de 4096. **Hipótese**: diminuir esperas da thread produtora sob pressão; **risco**: mais tráfego atômico e possível regressão. Exige comparação em jogos reais.

No processo de compilação:
4. MSVC Release x64 com `/arch:AVX2`, instruções suportadas pelo Ryzen 5 5600X.
5. Base mais nova do PokeDinner/Eglantine para incorporar as correções NX2 da revisão fixada.
6. Validação rígida de contexto: se a base não tiver exatamente os trechos esperados, a alteração é interrompida. Isso evita aplicar mudanças incorretas silenciosamente.

## Build Windows
Acesse `Actions > Eden Custom NX2 - Zen3 Windows` nesta ramificação; é possível usar o evento de push ou `Run workflow` quando disponível. O resultado, **se a compilação concluir**, aparece como artefato `Eden-Custom-NX2-Zen3-RTX5060Ti-Win64`.

Descompacte em uma pasta separada do Eden original. O executável é `eglantine.exe`. Não é preciso trocar firmware, jogos ou salvar arquivos no repositório. Não inclua ROMs, chaves ou arquivos comerciais na compilação.

## Critérios de aceitação antes de chamar de versão estável
- Pipeline CMake/MSVC concluído; executáveis Windows produzidos e bibliotecas Qt empacotadas.
- Abertura e fechamento sem falhas; cache de shaders carregando normalmente.
- Testes em jogos possuídos legalmente, comparando FPS **e** 1% low/frametime contra a compilação de origem.
- Verificação de texturas, áudio, tempo de carregamento, travamentos e persistência de saves.
- Reverter o lote 2048 -> 4096 se o frametime piorar.
- Avaliar o resultado real na RTX 5060 Ti e identificar gargalos com logs/perfilamento antes de novas intervenções em Vulkan.

## Estado de validação
Arquivos C++ e pipeline de compilação **criados**. O código ainda não foi compilado nem testado em execução no PC do usuário. Nenhum ganho de FPS ou aumento de compatibilidade foi comprovado.

Código derivado do projeto GPL-3.0-or-later; preserve avisos de direitos autorais e disponibilize o código-fonte correspondente ao distribuir executáveis.
