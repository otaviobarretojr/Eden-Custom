# Eden S25 Ultra — Diagnóstico de jogos não reconhecidos

## Objetivo

A tela **Configurações > Diagnóstico da biblioteca** faz uma inspeção local e
somente de leitura das pastas de jogos configuradas no Eden. Ela foi criada
para distinguir problemas de acesso, profundidade e formato antes de tentar
alterar drivers, firmware ou configuração do emulador.

## Resultados

- Pastas de jogos configuradas, acessíveis e sem permissão.
- Arquivos candidatos com extensões aceitas pelo scanner: NSP, XCI, NCA e NRO.
- Arquivos de extensão aceita escondidos mais fundo que o nível de busca
  escolhido para a pasta: nível 1 na raiz, até nível 3 com busca profunda.
- NSZ/XCZ, que exigem conversão local no conversor S25 para NSP/XCI.
- ZIP/RAR/7Z, que não são jogos diretamente executáveis.
- Até 12 exemplos com *nomes simples* de arquivos, exibidos apenas no aparelho.

O botão **Copiar resumo anônimo** compartilha somente contagens, sem nomes,
URIs, caminhos, chaves ou conteúdo. A varredura não modifica bibliotecas,
saves, mods, firmware, arquivos ou ajustes.

A auditoria examina no máximo 4.000 entradas e até seis níveis de pasta.
Quando esses limites são alcançados, indica explicitamente que o resultado
pode ser incompleto.

## Correções de robustez

O scanner principal agora ignora entradas individuais que lançam exceções
ao acessar metadados, sem encerrar toda a descoberta de jogos.
O fallback de ID para arquivos sem metadados não usa mais `substring(-1)`
quando um título não possui ponto/extensão.
A validação de diretórios SAF fecha o cursor da consulta e não considera
consultas nulas como pastas acessíveis.

## Limitações

Apenas a **extensão** do arquivo é inspecionada nessa tela. Não se tenta
descriptografar, executar ou conferir a integridade do conteúdo.
Portanto, uma contagem de arquivos candidatos **não significa** que o
emulador pode rodá-los; pode ser necessário verificar compatibilidade,
integridade de backups próprios, chaves e firmware obtidos legitimamente.
NSZ/XCZ convertidos também podem falhar se os dados originais forem inválidos.

## Testes manuais necessários no Galaxy S25 Ultra

1. Pasta raiz contendo um NSP próprio: candidato visível.
2. NSP próprio dentro de duas subpastas: fora do alcance sem busca profunda,
   candidato visível com busca profunda.
3. NSZ/XCZ próprios: contagem de conversão, sem tentar abrir diretamente.
4. ZIP/RAR próprios: contagem de arquivo compactado.
5. Pasta SAF com permissão revogada: reportar sem acesso.
6. Nome de arquivo sem extensão: biblioteca não deve interromper descoberta.
7. Conferir que o resumo copiado não contém nomes de arquivos e que nenhum
   arquivo ou save foi modificado.
8. Validar compilação e instalação por atualização sobre a variante anterior,
   com backup dos saves antes da instalação.

O build do GitHub Actions apenas demonstra a compilação; resultados de execução
e compatibilidade precisam ser reproduzidos no dispositivo.
