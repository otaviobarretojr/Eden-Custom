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

## Fluxo de instalação unificado (complementos)
- O botão **Instalar** da tela de complementos continua abrindo a escolha de **Atualizações e DLC** ou **Mods e cheats**.
- **Atualizações e DLC** chama o seletor original e instala NSP/arquivos compatíveis pelo fluxo nativo existente, sem alterações.
- **Mods e cheats** no ASTRYX S25 Ultra envia um evento ao `AddonsFragment`, que inicia o importador local do jogo com `s25_mod_open_picker=true`.
- Ao entrar no importador pelo botão **Instalar**, o seletor de documentos é aberto automaticamente, sem botão intermediário.
- Cancelar a seleção volta à tela de complementos. Após a confirmação e importação bem-sucedida, o importador retorna `RESULT_OK`; a lista de complementos do jogo é atualizada.
- Nos outros sabores/dispositivos, o seletor de pastas legado para mods permanece disponível.
- O botão de importação duplicado na barra superior foi removido.
