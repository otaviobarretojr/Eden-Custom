# Eden S25 Ultra — Biblioteca Online, v1

## Fontes

**Homebrew App Store (ForTheUsers)**: catálogo público de software homebrew
de terceiros distribuído pelos respectivos criadores e pelos mantenedores do
repositório. API: https://switch.cdn.fortheusers.org/repo.json .
Downloads: https://switch.cdn.fortheusers.org/zips/<package>.zip .
O app não instala os pacotes automaticamente, apenas permite salvá-los nos
Downloads do Android. Muitos pacotes contêm arquivos NRO, que precisam de um
Switch desbloqueado e não rodam normalmente no emulador Android.

**Catálogo JSON estilo Tinfoil**: o usuário informa uma URL HTTPS pública e
autorizada. O Eden lê as propriedades básicas:
  {"files":[{"url":"https://example.com/demo.zip","size":1000}],
   "directories":["https://example.com/outros.json"],
   "success":"Bem-vindo"}

Referência: https://blawar.github.io/tinfoil/custom_index/ .
O parser oferece suporte às variantes de arquivos e diretórios como objetos
ou strings. Links HTTP/FTP/SDMC/GDrive exclusivos do Tinfoil, headers extras,
cookies, tokens e DRM não são interpretados pelo aplicativo. Não há catálogo
de ROMs comerciais de terceiros embutido, e a licença para distribuir
conteúdo permanece de responsabilidade de cada fornecedor.

## Funcionamento

Em **Configurações > Biblioteca Online**:
1. Selecionar **Explorar Homebrew App Store** para buscar pacotes públicos.
2. Opcionalmente, inserir URL HTTPS de um catálogo público autorizado e
   selecionar **Conectar catálogo HTTPS**.
3. Navegar por diretórios, filtrar títulos e examinar tipo e tamanho.
4. Confirmar download; o Android DownloadManager salva em
   **Downloads/EdenS25Ultra** com uma notificação.
5. Os downloads NÃO são instalados automaticamente no emulador.
   Para jogos que o usuário pode utilizar e que sejam compatíveis com o Eden,
   mover arquivos NSP/NSZ/XCI/XCZ para a pasta de jogos configurada no app.

## Proteções

- Apenas HTTPS sem URL com usuário/senha embutidos; bloqueio básico de
  hosts locais, IPs literais privados e portas não padronizadas.
- Redirecionamentos da leitura do catálogo são revalidados.
- Tamanho máximo do JSON: 7 MiB; limite de 3500 itens e 250 pastas.
- Transferências confirmadas individualmente e realizadas pelo Android.
- Não altera chaves, firmware, saves, lista de jogos nem configurações.
- O parser trata metadados remotos como dados não confiáveis.

## Limitações conhecidas

- Muitos servidores Tinfoil comerciais usam sessões de console, URLs com
  token/certificados e protocolos próprios: não serão compatíveis.
- O conteúdo da fonte deve ser autorizado e os downloads podem exigir
  conta, sessão ou licença; tais mecanismos não são contornados.
- Não há instalação automática de pacotes homebrew NRO no Eden.
- Dados listados não constituem prova de compatibilidade.

## Testes

- Compilar variante \`s25Ultra\` e confirmar pacote/certificado.
- Explorar homebrew, filtrar por jogo e tentar baixar ZIP independente.
- Abrir JSON autorizado com arquivo e pasta e testar navegação/voltar.
- Rejeitar HTTP, localhost, endereços de rede privada e catálogos enormes.
- Verificar destino Downloads/EdenS25Ultra e abertura do DownloadManager.
- Confirmar que saves e mods continuam disponíveis após atualização.
