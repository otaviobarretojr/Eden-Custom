# Perfil de fluidez de shaders — Eden S25 Ultra

## Objetivo

Adiciona uma opção experimental **Fluidez de shaders · 1×** aos perfis globais
específicos do Samsung SM-S938. É uma configuração reversível, não uma alteração
no compilador Vulkan nem uma promessa de eliminar todos os travamentos.

Ao aplicar voluntariamente:
- Mantém a resolução 1×; **não** altera o driver Vulkan.
- Ativa cache de shaders em disco, shaders assíncronos e GPU assíncrona.
- Liga a sobreposição de FPS, frametime e contagem de shaders compilando.
- Preserva VSync, número de threads de pipeline, apresentação assíncrona,
  clocks e todas as opções não envolvidas no perfil.
- **Não** apaga caches, chaves, jogos nem saves.

## Compatibilidade com backups antigos

Os perfis antigos tinham backups para resolução, cache, GPU assíncrona e
indicadores FPS/frametime. As duas opções experimentais (shader assíncrono e
contagem de shaders) só ganham backup quando o perfil é selecionado, incluindo
instalações atualizadas com `backup_valid` de versões antigas.

Ao trocar do perfil shader para outro perfil, as duas opções experimentais
retornam ao estado anterior. `Restaurar original` também respeita o backup
anterior e não mexe nas flags experimentais de usuários que nunca ativaram
esse perfil.

## Validação

1. Testar em um SM-S938 sem jogo em execução.
2. Aplicar `Equilibrado` e observar que não altera shaders assíncronos.
3. Aplicar `Fluidez de shaders`; verificar resolução 1×, cache e async ON.
4. Conferir a tela `Diagnóstico` e anotar os valores.
5. Executar um backup legal por 5 min em cenário novo; medir FPS/frametime.
6. Repetir o mesmo trajeto em sessão reiniciada, sem limpar cache.
7. Caso haja glitches, trocar ao `Equilibrado` ou restaurar original.
8. Instalar por cima da versão anterior, verificando assinatura/package.
9. Testar diálogo e rollback em instalação com backup de perfil v1.

A compilação do APK **não** substitui testes no aparelho; as primeiras cenas
podem continuar exigindo compilação de pipelines de drivers.
