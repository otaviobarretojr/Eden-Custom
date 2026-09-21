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
