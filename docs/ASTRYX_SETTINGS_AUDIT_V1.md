# ASTRYX — auditoria das configurações e rework visual v1

**Base auditada:** branch `ui/astryx-library-v2` do fork `otaviobarretojr/Eden-Custom`, em 18/10/2026. **Escopo do rework:** somente `s25Ultra`, branch experimental `ui/astryx-settings-v1`. Preservar GPL e créditos do Eden.

## Diagnóstico principal

O problema observado é **excesso de funções expostas no mesmo nível**, não a ausência de utilidade técnica. Os usuários não devem precisar atravessar logs de GPU, parâmetros de buffer ou paths de NAND para configurar uma resolução.

A tela `HomeSettingsFragment` reúne **23 cartões potenciais** (alguns dependem de Adreno, Galaxy, firmware ou instalação de módulos), e abre `SettingsActivity` com outros grupos de controles. A implementação anterior não separava frequência, periculosidade, propósito nem recorrência.

### Inventário por menu — conferido no código

| Local | Inventário aproximado | Recomendação |
|---|---:|---|
| Painel inicial | **23 cartões** possíveis | Criar Principal / Ferramentas / Avançado, mantendo todos os 23 |
| Configurações da emulação (raiz) | **8 submenus + restaurar padrões** (alguns condicionais) | Manter acesso para sistema, gráficos, áudio e controles |
| Sistema | **18 ajustes** + divisores | Essencial: nome do console, limite de velocidade, modo dock, região e idioma; completo mantém clocks, memory layout, RTC, CPU ticks e rede |
| Gráficos | **33 chaves e 1 controle combinado** + divisores | Essencial: resolução, vsync, escala, antialiasing, precisão GPU, ASTC, cache, shaders assíncronos e proporção; completo mantém DMA, fences, buffers, extensões, hacks |
| Overlay de desempenho | **11 ajustes** | Necessário para monitorar FPS/frametime; poderia ser agrupado em leitura essencial e métricas extras futuramente |
| Overlay do dispositivo/SOC | **9 ajustes** condicionais | Avançado; não confundir com FPS: mostra hardware, firmware e drivers |
| Overlay de controles | **6 chaves e editor de layout** | Útil para controles touch e gamepad; manter |
| Áudio | **2 ajustes** | Mantê-los simples; não há sistema de dublagem |
| Controle de jogadores | **até 8 slots**, perfis por jogador e mapeamento | Manter funcional; sugerir aba jogadores adicionais no futuro sem remover 2–8 |
| Aparência / aplicativo | idioma, cor/tema, modo escuro, fullscreen, botões, atalhos | Manter; nota: o estilo de biblioteca ASTRYX é deliberadamente escuro |
| Debug | **21 chaves e 1 controle combinado** + agrupamentos | Muito técnico: CPU backend, logs, dumps shader, patches; manter somente em caminho avançado |
| Caminhos personalizados | save, NAND, SDMC | Manter apenas sob configuração completa e com aviso de risco futuro |
| Frame generation | **6 chaves**, depende do módulo e do suporte GPU | Avançado/experimental, independente das configurações normais de FPS |
| Pós-processamento | lista dinâmica de filtros/presets e técnicas | Avançado/experimental; não é mera mudança cosmética, pode afetar recursos |
| Freedreno | variáveis e presets do driver | Avançado; parâmetros inválidos podem causar defeitos/crashes |
| Drivers Vulkan | instalar, escolher, consultar | Acesso Principal; relevante para defeitos de renderização |
| Perfis Galaxy | diálogo rápido global / controles individuais em cada jogo | Manter ambos porque têm **escopos diferentes** |
| Diagnóstico Galaxy | informações de GPU, Vulkan, memórias e térmicas | Avançado; não altera o jogo |
| Biblioteca, conversor NSZ/XCZ, fontes online, varredura, perfis, applets | ferramentas diversas | Agrupar em Ferramentas |
| Informações, compartilhar logs/GPU logs, pasta do usuário | suporte | Agrupar em Avançado ou Ferramentas (pasta de usuário é Ferramentas) |

**Contagens = opções explicitamente listadas no presenter Kotlin**, não o número de opções visíveis simultaneamente. Diversos itens têm dependências de compilação, firmware, hardware, jogo global/individual ou ajustes ativados.

### Sobre a possível duplicação

1. `Perfis Galaxy S25 Ultra` vs `Configurações → Gráficos`: relação de **atalho controlado** versus ajustes finos. Não são funções idênticas.
2. `Diagnóstico Galaxy` vs `Overlay SOC`: diálogo de leitura pontual vs telemetria na tela do jogo.
3. `Driver Manager` vs `Freedreno`: seleção/importação de um driver vs configuração de variáveis; não unificar a execução.
4. `Pós-processamento` nas propriedades do jogo e nas ferramentas globais: contextos **por jogo/global**. Não remover.
5. `Controles` na home e controles dentro de emulação por jogo: perfis por usuário e modos globais/individuais.
6. `Logs` e `Logs da GPU`: fontes diferentes, com utilidade para diagnóstico técnico.

Não foram identificadas bases para **apagar** permanentemente opções, reduzir permissões ou migrar dados.

## Mudanças v1 implementadas

- `HomeSettingsFragment`: agrupar sem descartar: Principal (6 destinos), Ferramentas (9), Avançado (8), condicionado ao pacote `s25Ultra`; a seleção de aba é persistida localmente e não modifica opções em disco.
- `SettingsFragmentPresenter` + `SettingsFragment`: filtro **Essencial/Completo** só em Sistema e Gráficos, acessível na barra superior; lista completa é a original, produzida pela mesma lógica de dependências e disponibilidade.
- Gráficos Essencial **não força** nenhuma configuração: inclui ASTC, precisão GPU, cache e shaders assíncronos, importantes para diagnosticar Xenoblade sem modificar o driver.
- Estilo dos cartões principais e das linhas técnicas: grafite escuro, ciano, títulos claros, bordas discretas e feedback tátil nativo.
- Tudo em `src/s25Ultra/res` quando possível. O restante do Eden não muda de tema nem perde menus.

**Atenção ao escopo:** o rework não altera nomes/chaves INI, estados, assinatura, `applicationId`, diretórios, dados, controle de firmware, compatibilidade do jogo, perfis e implementações nativas.

## Melhorias planejadas para uma v2, sem alterações prematuras

- Adicionar descrição de risco em termos claros a CPU ticks, memória, clocks, DMA, GPU readback, patches de driver e save paths.
- Evitar que o reset global seja acionado sem confirmação (a confirmação já existe; revisar o texto e os backups).
- Fazer uma busca integrada por todas as ferramentas, inclusive as categorias da home, sem duplicar entradas.
- Adicionar favoritos reais para ajustes por jogo, sem persistir configurações de renderização inadvertidamente.
- Mostrar avisos contextuais apenas ao alterar drivers ou opções arriscadas.
- Auditar compatibilidade de temas claros/acessibilidade, escalas de fonte e leitores de tela em dispositivo real.
- **Não** ocultar (ou remover) perfis de jogadores 2–8: podem ser necessários em multiplayer.

## Teste de aceitação no Galaxy S25 Ultra

1. Fazer backup externo de saves e configs, instalar somente APK com mesma assinatura/ID e versionCode superior.
2. Confirmar que Principal, Ferramentas e Avançado juntos têm **todos os 23 destinos** e todos abrem corretamente.
3. Em Sistema e Gráficos, comparar Essencial com Completo: Completo lista todos os controles anteriores e os valores numéricos originais.
4. Testar acesso a ASTC e GPU precisa para Xenoblade; não modificar valores sem anotá-los.
5. Testar pesquisa, teclado, input dos controles, importadores, gerenciadores, configurações por jogo, editor de overlay, links de arquivos e retorno de navegação.
6. Confirmar que nenhuma configuração, save, firmware, driver ou diretório foi alterado apenas ao navegar entre as abas.
7. Testar rotação/landscape, texto ampliado e leitores de tela.
8. Somente então promover para versão principal; preservar rollback por meio de uma compilação **posterior** sem os novos menus (não forçar downgrade).

## Resultado e prioridade

**Prioridade alta:** reduzir a sobrecarga cognitiva mantendo caminhos avançados. **Prioridade média:** tornar ações experimentais autoexplicativas e permitir buscas cruzadas. **Prioridade baixa:** migrar APIs e alterar nomes internos — não recomendado neste rework.
