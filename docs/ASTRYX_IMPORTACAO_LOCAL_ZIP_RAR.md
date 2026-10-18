# ASTRYX — Importação local de mods e traduções

## Uso
Na tela de complementos do jogo, toque em **Importar ZIP/RAR**. A nova tela oferece somente **Importar arquivo ZIP ou RAR**. Selecione um arquivo salvo no Android e confirme a instalação após conferir a versão do jogo.

## Funcionamento offline
- Não há pesquisa de servidores de mods/traduções, índice CNX, GameBanana, JumpmanClub ou GitHub Releases.
- O `S25ModCatalog.kt` e a função de download remoto foram removidos.
- ZIPs são validados localmente. RAR4/RAR5 são convertidos para ZIP temporário pelo `S25RarBridge`, com limites de tamanho, entradas e expansão.
- `S25ModLayout` identifica as pastas LayeredFS, inclusive o formato específico de Pokémon Legends Z-A já suportado.
- `S25ModInstaller` verifica o Title ID, caminhos inseguros, colisões e extensões proibidas antes de mover arquivos para `load/<TitleID>/`.
- O nome do arquivo selecionado é usado como nome do mod quando necessário. Arquivos temporários são descartados após sucesso ou falha.
- A instalação não substitui pastas de mods existentes; feche o jogo antes de importar.

## Recursos preservados
Biblioteca 2.0, novas configurações, drivers, saves, dados existentes dos jogos, identidade ASTRYX, identificador Android e assinatura não foram modificados. A biblioteca online de homebrew/jogos é independente do sistema de busca de mods/traduções e permanece inalterada.

## Validação
O workflow exige ausência do catálogo remoto e do download de mods, confirma a opção local de importar e mantém verificações de assinatura, identidade e recursos do APK. Ainda são necessários testes no dispositivo com um ZIP e um RAR válidos.
