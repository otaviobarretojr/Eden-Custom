# Traduções PT-BR — correção do catálogo indisponível

## Diagnóstico (2026-10-17)
O índice gamemoddesignbr/nx-links/nx-links-v220.json ainda contém dados, porém
aponta frequentemente para releases de AMSNX/Traducoes-BR-NX, repositório que
retorna 404 atualmente. Antes, a interface exibia o botão "Baixar e instalar"
mesmo para links não verificados. Isso gerava falsas expectativas e falhas HTTP.

## Alteração
- Retirado o índice legado CNX da busca padrão. Os arquivos indisponíveis não
  aparecem mais como download confirmado.
- Tradutores originais são apresentados com atribuição e link para suas páginas
  públicas, SEM fingir que existe ZIP instalável via API.
- Links JumpmanClub por Title ID conhecidos:
  Super Mario Odyssey, Luigi's Mansion 2 HD, Mario Kart 8 Deluxe,
  Metroid Prime Remastered, Metroid Dread, Luigi's Mansion 3 e
  Super Mario 3D World + Bowser's Fury.
- ZIPs GitHub Releases são consultados em tempo real exclusivamente para
  repositórios autorais e Title IDs explícitos: Two Point Hospital,
  The Escapists Complete Edition e Crysis 2 Remastered.
  Só aceita releases publicadas com ZIP menor que 150 MiB.
- Um botão novo "Instalar tradução baixada (ZIP)" importa via seletor Android
  e reutiliza S25ModInstaller para validar a estrutura antes de instalar.
- Erros HTTP 404/410 distinguem arquivo removido de erro genérico;
  HTTP 403/429 informam rate limit.
- Mods e o conversor NSZ/XCZ permanecem inalterados.

## Segurança, compatibilidade e limitações
- Dados do jogo e seus saves não são tocados pelo instalador.
- Instalação requer romfs/exefs/cheats compatíveis dentro do ZIP, não RAR/7z.
- Nunca sobrescreve mod existente. O usuário pode reverter em "Gerenciar mods".
- A mera presença do ZIP em GitHub Releases não garante compatibilidade de
  versão ou integridade. Exigir verificação de compatibilidade pelo usuário.
- O site JumpmanClub pode conter hospedagem própria/terceiros e instruções
  especiais. Nesses casos abrir no navegador original; baixar e importar ZIP.
- Para Super Mario Odyssey, o site oficial dos autores lista tradução PT-BR
  v1.1 e variantes Yuzu/Sudachi. Não inferir URLs de download ocultas.
- O catálogo não converte arquivos .rar/.7z nem executa scripts de instaladores.

## Verificações para teste em aparelho
1. Super Mario Odyssey (0100000000010000): "Pesquisar traduções PT-BR"
   deve mostrar o link JumpmanClub e não deve exibir o antigo link AMSNX.
2. "Abrir página da tradução" deve abrir
   https://www.jumpmanclubbrasil.com.br/post/mario-odyssey.
3. Um ZIP de mod traduzido legítimo e compatível deve poder ser importado por
   "Instalar tradução baixada (ZIP)" e aparecer em "Gerenciar mods instalados".
4. Em The Escapists (01001B700BA7C000) confirmar que GitHub Releases fornece
   um asset .zip, e que o usuário pode iniciar download direto.
5. Tentar selecionar .rar e ZIP com Title ID errado, caminhos "../" ou
   ROMs/executáveis. Esperado: falha segura, sem alterar mods existentes.
6. Desativar e reativar a tradução; confirmar que saves permanecem intactos.
7. Confirmar mesmo package/certificado do build aa57db885d-relWithDebInfo.
8. Não considerar o recurso aprovado em runtime sem confirmar no telefone.
