# ASTRYX — auditoria e correção das fontes de tradução (2026-10-18)

## Diagnóstico confirmado

- Repositório oficial do [CostelaCNX/CNX-Updater](https://github.com/CostelaCNX/CNX-Updater) contém a opção de **baixar traduções**, mas o `include/constants.hpp` publicado na branch `main` ainda referencia:
  `https://raw.githubusercontent.com/gamemoddesignbr/nx-links/master/nx-links-v204.json`.
- A branch `master` do repositório `gamemoddesignbr/nx-links` **não existe**. A branch `main` mantém 5 índices (v204/v205/v213/v214/v220), hospedados em GitHub, mas o catálogo é antigo (última atualização 2022).
- `nx-links-v220.json` possui **153 referências históricas** de tradução. Os downloads no repositório `AMSNX/Traducoes-BR-NX` estão removidos (404 na consulta ao repositório). **Não publicar esses ZIPs como ativos**.
- Exemplos históricos com Title ID exato: Xenoblade Chronicles Definitive Edition `0100FF500E34A000`, Super Mario Odyssey `0100000000010000`; um histórico não confirma arquivo disponível.
- O [catálogo JumpmanClub Brasil / Nintendo Switch](https://www.jumpmanclubbrasil.com.br/category/nintendo-switch) está publicamente acessível e contém **20 posts**. A página [Super Mario Odyssey](https://www.jumpmanclubbrasil.com.br/post/mario-odyssey) contém informações oficiais da tradução e variantes de download. Os botões reais podem depender do navegador, terceiros e versão do patch: **não inferir URL direta**.
- O GitHub Releases dos autores [JUNIORGBJ](https://github.com/JUNIORGBJ) já é consultado pelo ASTRYX para alguns jogos específicos (The Escapists, Two Point Hospital, Crysis 2 Remastered), somente ZIP publicado, com validação de Title ID e tamanho.
- O GameBanana continua como fonte comunitária de busca, sem garantia de resultado para todos os jogos.

## Implementação

- Nova função `S25ModCatalog.cnxArchiveReferences(titleId)`: lê índice de **GitHub/main** sob HTTPS, limita bytes e 500 entradas, só aceita correspondência de **16 dígitos hexadecimais** com Title ID informado pelo jogo, obtido do campo `installed.hash_file` ou nome do arquivo ZIP, sem usar o link de download obsoleto.
- Resultados CNX históricos são classificados como **referências**, `downloadUrl = null`, sem `gameBananaId`, e não habilitam instalação automática. A tela distingue contagem de fontes publicadas e referências antigas.
- Novos links oficiais por Title ID comprovado para **Captain Toad**, **Donkey Kong Tropical Freeze**, **Skyrim**, **South Park The Stick of Truth** e **Ultra Street Fighter II**.
- Botão de busca de repositórios públicos no GitHub por nome do jogo + PT-BR, e links de consulta JumpmanClub/CNX histórico.
- O aplicativo mantém fluxo já existente de importação **ZIP/RAR** (com validação, sem sobrepor mods existentes). Downloads automáticos continuam limitados às fontes que efetivamente publicam links avaliados pelo instalador.
- Nenhum arquivo do jogo, save, driver, identificação Android, certificado ou configuração é modificado pela consulta.

## Verificação crítica

**Um servidor que entrega catálogo não prova que os arquivos do catálogo podem ser baixados**. Sob nenhuma hipótese reintroduzir link `AMSNX/Traducoes-BR-NX/releases/download/` como ativo sem novo repositório/autorização e checagem real.

### Testes necessários após a compilação

1. **Super Mario Odyssey** (`0100000000010000`): exibir publicação oficial JumpmanClub e referência CNX histórica separadas; link da página deve abrir; nenhuma tentativa de download automático do arquivo histórico.
2. **Xenoblade Chronicles Definitive Edition** (`0100FF500E34A000`): exibir pelo menos referência histórica CNX, embora **não haja ZIP ativo confirmado**. Não confundir com Xenoblade X ou 2.
3. **Pokémon Legends: Z-A** (`0100F43008C44000`): não confundir com Pokémon Legends Arceus, Scarlet ou Violet; pode não haver fonte atual para o jogo. Continuar permitindo importação manual da tradução autorizada já obtida.
4. Se GitHub devolver 403/429, exibir aviso de rede claro e manter páginas de autoria conhecidas; não indicar arquivo encontrado se catálogo falhar.
5. Testar busca manual do GameBanana e link de pesquisa de repositórios GitHub.
6. Confirmar versão e integridade dos ZIPs e assinatura idêntica ao ASTRYX Settings V1, sem desinstalar o app atual.

## Fontes para consulta

- [CNX-Updater](https://github.com/CostelaCNX/CNX-Updater), em especial `include/constants.hpp`.
- [CNX index /main](https://github.com/gamemoddesignbr/nx-links/blob/main/nx-links-v220.json) (histórico 2022).
- [JumpmanClub Brasil](https://www.jumpmanclubbrasil.com.br/category/nintendo-switch) (catálogo do tradutor).
- [GameBanana](https://gamebanana.com/) (publicações comunitárias).
