# Eden S25 Ultra — Gerenciador de mods e traduções (MVP)

## Integração e fluxo
Abrir o jogo na biblioteca > Complementos > **Mods e traduções** (barra superior).
A atividade é exclusiva da variante S25 Ultra, não é exportada, requer Title ID de 16
caracteres e nunca baixa jogos, DLCs, keys, firmware ou atualizações comerciais.

- **Mods:** consulta GitHub Contents API no repositório comunitário
  Fl4sh9174/Switch-Emulator-Ultrawide-FPS-Mods e filtra o ZIP estritamente por
  [TitleID]. Tenta também GameBanana (Core/List/Like e apiv11/Mod/Index) por
  nome do jogo. GameBanana pode limitar/bloquear chamadas; não inventar mods.
- **Traduções:** consulta índice histórico
  gamemoddesignbr/nx-links/nx-links-v220.json, com aviso explícito de metadata
  de 2023 e possibilidade de URL morta. Filtra GitHub ZIP por Title ID. Também
  tenta GameBanana via palavras-chave, e oferece navegação em patcheados.org.
  Não afirmar que uma tradução funciona numa versão recente do jogo.
- **Download:** somente HTTPS em allowlist de hosts GitHub/GameBanana;
  verificação de redirecionamentos, limite de 150 MiB, assinatura ZIP PK.
- **Instalação:** extrair em stage privado dentro do userDirectory do Eden,
  validando caminho e extensão, no máximo 6000 entradas/350 MiB descompactados,
  sem executar scripts, sem permitir .., caminhos absolutos ou Title ID estranho.
  Aceita apenas arquivos sob romfs/exefs/cheats/romfslite/romfs_ext. Remove
  stage/arquivos parciais no erro. Move por rename para load/<TitleID>/<mod>
  APENAS se a pasta ainda não existir, sem sobrescrever nenhum mod anterior.
- **Gerenciar:** desativar = mover load/<TitleID>/<mod> para
  disabled_mods/<TitleID>/<mod> fora da árvore load; reativar move de volta.
  O gerenciamento não toca nos saves. Fechar o jogo antes de alternar.

## Limitações importantes

- O filtro pelo Title ID não verifica o Build ID do executável nem a versão.
  Mods FPS ou gráficos podem quebrar jogos ou causar instabilidade. O usuário
  deve verificar instruções e licença na publicação original.
- Arquivos RAR/7z, instaladores EXE, NSP/XCI e pacotes que não possuem diretórios
  romfs/exefs/cheats são rejeitados para instalação automática.
- O GameBanana pode mudar API/nomes de campos; falhas são mostradas por fonte
  sem afetar o repositório GitHub.
- ZIPs que exigem instruções especiais devem ser instalados manualmente.
- Nenhum link antigo do CNX é garantido como ativo.

## Testes para aprovação em hardware
1. Compilar variante S25 Ultra, confirmar idêntico package + assinatura e APK.
2. Abrir jogo com Title ID conhecido (Scarlet/Violet/Legends Z-A).
3. Validar consulta ao repositório GitHub e visualização de mods do título.
4. Fazer download de pacote legal pequeno, visualizar resultado em Complementos,
   iniciar jogo e confirmar aplicação (não suficiente compilar).
5. Ativar e desativar mod; reiniciar jogo; confirmar saves preservados.
6. Verificar ZIP malicioso com ../, caminhos absolutos, Title ID diferente,
   zip bomb, arquivo proibido, ZIP inválido e falhas de rede.
7. Repetir tentativa com mod já instalado, exigir ausência de sobrescrita.
8. Validar links quebrados/versão obsoleta no catálogo histórico.
9. Testar em tela rotacionada, interrupção de rede, fechar app durante download.
