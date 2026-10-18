# Pokémon Legends: Z-A — Driver Vulkan experimental para Adreno 830

Status: **pipeline criado, compilação e gameplay a validar**. Este arquivo não afirma que um bug gráfico já foi resolvido.

## Objetivo

Comparar o driver Vulkan **de sistema** do Galaxy S25 Ultra (Snapdragon 8 Elite / Adreno 830) com um driver genuíno Mesa Turnip experimental compilado para ARM64, sem root, por meio da integração AdrenoTools já existente no Eden.

O alvo de testes é **Pokémon Legends: Z-A**, Title ID `0100F43008C44000`. Problemas observados: objetos/prédios ausentes na cidade, embora o jogo exiba cerca de 30 FPS numa captura. A captura não prova se a falha é de driver, sincronização do renderizador, carregamento de shaders, mods ou versão de jogo.

## O que é o driver

- Código-base: **Mesa / Freedreno Turnip**, projeto open source; não é um driver Qualcomm de sistema.
- Ferramenta de compilação: [Piperella/Piperella-Turnip](https://github.com/Piperella/Piperella-Turnip), fixada no commit `5d8ffa7b0b88f44a2b26207663955e12ccbfdb09` (licença do repositório de ferramentas: GPL-3.0).
- Patches A8xx para KGSL do upstream + ajuste **experimental** `has_fs_tex_prefetch = False` somente para A830, via `patches/tune_a830.py`.
- Produz um **driver Vulkan real**, `libvulkan_freedreno.so`, em um ZIP AdrenoTools com `meta.json` e manifesto de procedência. Nenhum jogo, keys, ROM, firmware ou mod integra o pacote.
- A alteração de texture prefetch tem base em uma hipótese do upstream; **não** é um patch confirmado para Pokémon Z-A.
- O workflow valida ELF ARM64, metadados, formato do ZIP e a presença da alteração A830. Falha se não encontrar a GPU ou o ajuste esperado.
- Fonte Mesa (branch main) será identificada pelo SHA efetivamente obtido e registrada em `BUILD-PROVENANCE.json`. A ferramenta upstream é fixada, mas a versão Mesa não é pinada nessa primeira experiência; reproduzir exige também o commit Mesa registrado.

## Compilar

Abra **Actions → Experimental Turnip A830 for Pokemon ZA** na branch `experiment/za-a830-turnip`. O workflow também dispara em alterações desta documentação ou do próprio pipeline.

Se a execução concluir em **success**, baixe o artefato `ZA-A830-Turnip-Experimental`. Extraia o ZIP de artefato do GitHub e mantenha o arquivo interno `ZA-A830-Turnip-Experimental.adpkg.zip` **compactado** ao instalar.

## Testar somente no jogo, sem perder o driver que funciona

1. Faça backup das configurações e saves do Eden. Feche a partida.
2. **Pokémon Z-A → Detalhes → Gerenciador de driver GPU**. Importe o ZIP AdrenoTools como driver experimental e selecione-o **apenas para esse jogo**. Se o app apresentar uma seleção global, **não** altere outros jogos sem querer.
3. Abra Pokémon Z-A sem mods gráficos, teste no mesmo local da cidade e compare a imagem com o **driver de sistema**, de preferência registrando screenshot, `eden_log.txt`, FPS e frametime.
4. Use a **mesma resolução, precisão GPU e configuração de shaders** nas duas execuções. Se possível, faça pelo menos duas passagens pelo local para evitar viés de compilação inicial de shaders.
5. Meça temperatura e quedas de desempenho após 15–20 minutos. Não desbloqueie limite térmico; não use opções de clock forçado.
6. Em caso de tela preta, travamento, piora gráfica ou aquecimento, volte ao **driver de sistema** pelo Gerenciador de driver GPU, feche e abra o jogo. Nunca substitua o driver Android do sistema.

### Checklist comparativo

| Teste | Driver de sistema | Turnip experimental A830 |
|---|---|---|
| Abre a tela inicial? | medir | medir |
| Carrega partida e saves? | medir | medir |
| Objetos/prédios invisíveis em Lumiose? | screenshot | mesma câmera |
| Texturas, chão e sombras completos? | screenshot | mesma câmera |
| FPS médio e pior frametime? | coletar | coletar |
| Estável após 15–20 min? | observar | observar |
| Alertas térmicos e temperatura ambiente? | registrar | registrar |

**Critério de aprovação:** o driver só será recomendado se reproduzir corretamente cenas antes defeituosas, sem novos artefatos ou regressões de estabilidade e sem aumento térmico inaceitável. Compilação verde, sozinha, **não** prova compatibilidade com o jogo.
