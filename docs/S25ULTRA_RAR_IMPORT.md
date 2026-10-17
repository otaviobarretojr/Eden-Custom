# Eden S25 Ultra — suporte a RAR 4/5 para traduções

## Objetivo
Adicionar suporte para o usuário selecionar diretamente um **arquivo .rar**
legítimo da tradução (além de .zip), sem utilizar ferramentas externas nem
reescrever arquivos de jogos, saves ou mods existentes.

## Tecnologia e distribuição
- Biblioteca **com.github.junrar:junrar:8.0.0** (Maven Central), apenas no
  flavor Android s25Ultra via s25UltraImplementation.
- Junrar 8 implementa leitura e descompressão de RAR 4 e RAR 5.
- **UnRAR License:** a tecnologia de descompressão RAR não pode ser
  utilizada para desenvolver um compressor/arquivador RAR compatível.
  Este projeto implementa somente DESCOMPRESSÃO, nunca compactação RAR.
- A licença e os créditos permanecem com os autores da biblioteca.

## Caminho de instalação
1. Complementos do jogo > Mods e traduções.
2. **Instalar tradução baixada (ZIP/RAR)**.
3. Selecionar via seletor padrão de arquivos Android.
4. Cópia temporária limitada a 150 MiB no cache privado.
5. Se RAR: verificar assinatura RAR4/RAR5, rejeitar redirecionamentos
   (links), criptografia, arquivos multipartes, caminhos perigosos,
   Title IDs incompatíveis, mais de 6000 entradas ou 350 MiB extraídos.
6. Descompactar somente entradas sob romfs/exefs/cheats/romfslite/romfs_ext
   para ZIP temporário, lendo streams integralmente para validação.
7. Executar o instalador ZIP existente, que efetua segunda validação e
   move mods para load/<TitleID>/<mod> sem sobrescrever pastas existentes.
8. Sempre excluir arquivos temporários ao finalizar.

## Referência do teste
RAR5 original informado pelo usuário:
  "Tradução Super Mario Odyssey v1.1 [JUMPMANCLUBBRASIL] [YUZU].rar"
  Title ID 0100000000010000.
  4 arquivos .szs dentro de load/<TitleID>/Mario_Odyssey_1.1_Jumpmanclub/
  romfs/LocalizedData/USen/ e um tutorial_yuzu.txt ignorado.
Esse arquivo privado NÃO deve ser publicado no repositório nem no APK.

## Limitações
- RAR sem senha e volume único; arquivos RAR divididos (.part01.rar)
  e protegidos por senha NÃO são suportados nesta primeira versão.
- RAR 7z é um formato distinto e continua não suportado.
- O patch pode ser incompatível com a versão instalada do jogo, mesmo
  quando o RAR é descompactado corretamente. Mods com conflitos devem
  permanecer desativados.

## Validações necessárias
- GitHub Actions: compilar Gradle, verificar assinatura e package.
- Celular real: selecionar o arquivo RAR5 original, confirmar instalação,
  pasta ativável/desativável, iniciar o Mario Odyssey e checar idioma.
- Testar RAR inválido, RAR de outro Title ID, RAR com caminho "../",
  links/redirecionamentos, RAR multipartes, RAR com senha, arquivo
  comprimido grande e falta de espaço.
- Testar ZIP antigo para garantir regressão zero.
- Nunca apagar os dados do Eden para instalar a atualização.
