# Eden S25 Ultra — Conversor offline

A função BitTorrent foi retirada do projeto. O APK não incorpora biblioteca jlibtorrent,
mecanismo de busca, tracker, download de jogos ou gerenciamento de torrents.

## Motor nativo

Conversão local NSZ → NSP e XCZ → XCI por Andro-NSZ, código aberto MIT:
https://github.com/Fast-and-Pythonic/Andro-NSZ-unpacker
Commit fixo: 57fed17c76c4dcb35420f7dad7c5aefc8fd49d7c

O workflow compila a engine com NDK ARM64, inclui a licença original em assets
e preserva o pacote e assinatura do Eden. Ponte JNI com.androNSZ.NszConverter.

## Comportamento

O usuário escolhe input/output pelo Storage Access Framework do Android.
O serviço em primeiro plano mostra progresso, cancelamento e diagnóstico.
A origem nunca é modificada. A conversão falha se o provedor não fornecer
um descritor pesquisável; não cria cópias temporárias enormes.
Saídas incompletas são removidas quando possível. As proteções originais e
eventuais requisitos de chaves do emulador são preservados.

## Testes pendentes

- Instalar atualização diretamente sobre a build anterior, preservando dados.
- Converter backup autorizado NSZ (solid e block) e XCZ no S25 Ultra.
- Verificar conteúdo, hash e leitura do arquivo de saída no Eden.
- Testar falta de espaço, interrupção de energia, provedor SAF não pesquisável,
  cancelamento e tela apagada. Não declarar compatibilidade absoluta antes disso.
