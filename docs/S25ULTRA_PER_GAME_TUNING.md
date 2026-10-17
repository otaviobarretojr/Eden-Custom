# Eden S25 Ultra — perfis por jogo e validação de gráficos

**Dispositivo-alvo:** Samsung Galaxy S25 Ultra (SM-S938 / Adreno 830). **Tipo:** experimento manual, por jogo, reversível.

## Por que por jogo?

Um driver que renderiza bem um título pode apresentar artefatos em outro. A opção **Perfis gráficos por jogo · S25 Ultra** fica nos detalhes de cada título e não altera os outros jogos nem o perfil global.

| Perfil | Escala interna | Precisão GPU | GPU assíncrona | Shaders assíncronos | Objetivo |
|---|---|---|---|---|---|
| Compatibilidade | 1× | Alta (High = 1) | Não | Não | Reduzir artefatos ligados à precisão ou compilação assíncrona; pode reduzir FPS |
| Desempenho | 0,75× | Baixa (Low = 0) | Sim | Sim | Testar menor custo gráfico e menos interrupções por shaders; pode ocultar efeitos temporariamente |
| Qualidade | 1,5× | Alta (High = 1) | Sim | Não | Priorizar imagem e precisão; pode exigir mais GPU |
| Econômico térmico | 0,5× | Baixa (Low = 0) | Sim | Não | Diminuir pixels renderizados e evitar compilação assíncrona em paralelo; imagem mais suave e possível perda de FPS |

O cache em disco de shaders fica ativo nos três perfis. A versão atual **não modifica** drivers gráficos, clock, limite térmico, resolução da tela Android, modificações, atualizações, saves, keys ou firmware.

As opções são baseadas em configurações existentes do Eden, **não são uma promessa de FPS nem um patch de compatibilidade universal**. O modo Compatibilidade pode ser mais lento; o modo Desempenho pode perder efeitos enquanto shaders são compilados. Para jogos difíceis, comparar driver de sistema e driver compatível usando somente pacotes de fontes confiáveis, instalados manualmente.

## Como testar

1. Feche qualquer jogo e abra **Detalhes do jogo → Perfis gráficos por jogo · S25 Ultra**.
2. Teste primeiro **Compatibilidade** se houver falhas de imagem ou travamentos; teste **Desempenho** para queda de FPS e **Qualidade** para melhorar resolução.
3. Inicie o jogo de novo. Jogue a **mesma cena**, com o mesmo estado de jogo, mods e driver para comparar FPS, frametime e defeitos visuais.
4. Se a temperatura estiver elevada, teste **Econômico térmico**. A redução de 1× para 0,5× reduz a carga potencial de pixels mas **não garante** queda de temperatura, especialmente em jogos limitados por CPU. A resolução e precisão reduzidas podem causar desfocagem e artefatos.
5. Caso um resultado piore, retorne ao menu e toque **Restaurar original**.
6. Para evidência técnica, use **Configurações → Compartilhar log** e anote o jogo, driver, perfil, cena testada e versões do Eden/Android.

## Backup e proteção de dados

- A primeira aplicação do perfil salva o INI original do jogo ao lado dele com sufixo `.s25-original.bak`. Se o título ainda não tinha INI, o recurso registra essa condição e remove apenas o INI criado ao restaurar.
- Cada alteração posterior conserva a primeira cópia, para **Restaurar original** retornar ao estado inicial, não apenas ao perfil anterior.
- Um hash SHA-256 detecta se o arquivo foi editado externamente entre operações. Nesse caso o recurso bloqueia a substituição automática para evitar sobregravar alterações; exporte seu INI antes de proceder manualmente.
- O recurso não toca nos saves e não apaga cache de shaders. Não é necessário desinstalar o Eden original.
- Não aplique ajustes por jogo enquanto uma sessão do emulador estiver aberta.

## Critérios de aceitação

- Um jogo com INI existente: aplicar cada perfil, verificar valores no menu do Eden, restaurar bytes do INI original e garantir saves intactos.
- Um jogo sem INI: aplicar perfil, confirmar novo INI, restaurar e confirmar remoção apenas deste INI.
- Alteração manual após a aplicação: não sobrescrever o INI silenciosamente.
- GPU High/Low e resolução devem afetar apenas o jogo escolhido, inclusive após fechar e reabrir o Eden.
- Testar jogo no modo Compatibilidade, Desempenho e Qualidade com o mesmo driver e comparar FPS mínimo, médio percebido, frametime e qualidade visual.
- A compilação GitHub Actions não testa execução real de jogos no SM-S938. O resultado depende dos testes no aparelho.

Fontes no código: `src/common/settings_enums.h` define `GpuAccuracy = Low, High`; `src/android/app/src/main/res/values/arrays.xml` define índices das resoluções.

## Monitoramento térmico do S25 Ultra

- Durante uma partida, o `S25ThermalGuardian` consulta o estado térmico reportado pelo Android e, no máximo uma vez a cada 15 segundos, uma previsão de `getThermalHeadroom(10)`.
- O status térmico também é acompanhado por um listener do sistema; isso permite avisar quando ocorre uma transição sem manter o emulador em um laço de medição intensiva.
- O sistema operacional pode não expor todos os limiares/predições. O recurso ignora valores indisponíveis e não faz estimativas de graus Celsius a partir do índice.
- Quando o status passa a moderado ou superior, ou a previsão atinge o limite de moderado disponibilizado pelo fabricante, uma recomendação é mostrada. Repetições são limitadas a aproximadamente um aviso a cada 2 minutos, exceto agravamento.
- Os avisos **não** alteram parâmetros do renderizador durante o gameplay, suspendem jogos automaticamente ou contornam proteções térmicas Samsung. Para reduzir carga, selecione o perfil de um jogo **antes** da próxima execução.
- Não há garantia de temperatura constante nem de ausência de throttling. O objetivo é chegar a um ponto sustentável de desempenho comparando sessões de 15–30 minutos, com a mesma carga, brilho, ventilação e sem carregador quando possível.
