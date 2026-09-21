# Eden Custom — Projeto v0.1

Base técnica: Eden Emulator v0.2.1
Branch principal de desenvolvimento: `custom-v0.1`

## Plataforma-alvo

Esta build é otimizada prioritariamente para:

- CPU: AMD Ryzen 5 5600X (Zen 3)
- GPU: NVIDIA GeForce RTX 5060 Ti 16 GB
- RAM: 32 GB
- Armazenamento: NVMe PCIe 4.0 1 TB
- Sistema operacional: Windows x64
- Backend gráfico prioritário: Vulkan

## Prioridades

1. Estabilidade de execução
2. Frame time consistente e redução de stutter
3. Desempenho de CPU/emulação
4. Shader/pipeline cache persistente
5. Qualidade gráfica
6. FPS máximo

Espaço em SSD não é restrição. Pré-processamento, cache persistente e arquivos convertidos podem usar armazenamento adicional quando isso melhorar estabilidade ou desempenho.

## Conversor de conteúdo

A versão v0.1 terá uma área independente de conversão.

Fluxo inicial:

- NSZ → NSP
- arquivo original preservado por padrão
- validação pós-conversão
- progresso real
- velocidade
- tempo restante
- fila/conversão em lote
- arrastar e soltar
- adição automática à biblioteca após conversão
- identificação de jogo base / update / DLC quando possível

Regra de arquitetura:

```
NSZ
 ↓
Conversor
 ↓
NSP validado
 ↓
Biblioteca / instalação de update-DLC
 ↓
Eden Core
```

O core de emulação não deve fazer descompressão NSZ durante o gameplay.

## Regra de estabilidade

Alterações de frontend, conversão e gerenciamento de biblioteca devem ficar separadas do core sempre que possível.

A base MSVC v0.2.1 é a referência estável. Otimizações de compilação/PGO serão comparadas contra ela antes de se tornarem padrão.

## Conteúdo e chaves

O projeto não inclui jogos, firmware, chaves ou conteúdo proprietário. Ele opera apenas sobre arquivos e configurações fornecidos legitimamente pelo usuário.


## Estado atual da v0.1

Implementado no branch `custom-v0.1`:

- área Tools -> Content Converter
- seleção múltipla e arrastar/soltar de arquivos NSZ
- conversão em fila NSZ -> NSP
- progresso e log
- verificação durante conversão
- preservação do NSZ original
- instalação opcional da ferramenta oficial NSZ 5.0.0 por download
- validação SHA-256 obrigatória do conversor baixado
- reutilização da pasta de prod.keys já configurada no Eden
- identificação preliminar BASE / UPDATE / DLC por Title ID
- confirmação pós-conversão usando o parser CNMT do próprio Eden
- relação de UPDATE/DLC com o Title ID base quando aplicável
- CI Windows MSVC via GitHub Actions
- cache CPM no CI
- cancelamento de builds CI obsoletas da mesma branch

### Validação

A referência continua sendo o Eden v0.2.1 MSVC puro.
Nenhuma otimização agressiva do core será ativada antes de uma build limpa e
comparação com a referência.

Próxima fase após a primeira build Windows validada:

1. testar o fluxo NSZ -> NSP com arquivo real do usuário;
2. instalar/associar updates e DLC convertidos;
3. criar perfil de hardware Ryzen 5 5600X + RTX 5060 Ti 16 GB;
4. comparar build MSVC padrão vs variante AVX2/Zen 3;
5. medir frametime, stutter e estabilidade antes de promover qualquer otimização.


## Fluxo integrado de NSZ

O Eden Custom trata NSZ como formato de entrada/armazenamento, não como formato de execução.

- arrastar um NSZ diretamente na janela principal abre o conversor;
- selecionar um NSZ em Load File abre o conversor;
- NSZ não é enviado ao loader do core;
- BASE convertido pode adicionar sua pasta automaticamente à biblioteca;
- UPDATE e DLC confirmados por CNMT podem ser enviados automaticamente à NAND;
- o arquivo NSZ original é preservado;
- a classificação final usa o parser CNMT do próprio Eden, e não apenas o nome do arquivo.
