# ASTRYX — branding experimental no Android S25 Ultra

**Status:** rebrand visual experimental, separado da branch Android estável. O nome ASTRYX não foi validado juridicamente como marca disponível e já possui usos públicos (incluindo o projeto Astryx da Meta e um estúdio de jogos). Não é recomendada divulgação comercial antes de uma pesquisa de marcas INPI/USPTO com análise de risco.

## Escopo da alteração

- **Somente o flavor `s25Ultra`**. Mainline Eden e os outros flavors continuam como antes.
- Nome exibido **ASTRYX**; `app_name` e `app_name_suffixed` resolvidos por flavor.
- Monograma geométrico A/X em vetores Android `astryx_launcher_foreground.xml` (ciano, azul e violeta), modo de ícone monocromático do Android 13+, fundo escuro e splash dedicado.
- Cores: `#0B0F14` fundo, `#111A2B` ícone, `#33CAEF` ciano, `#477EFF` azul, `#8F63FC` violeta.
- Notificações com nome ASTRYX, mas preservando os identificadores dos canais existentes e suas preferências.
- **Não** renomear símbolos internos do upstream, namespace, arquivos de jogos, saves, chaves, drivers nem caches; manter os créditos e licenças originais do Eden.
- Não embutir conteúdos do jogo nem remover avisos de licença.
- Nome de arquivo do APK pode variar, mas não é a identidade Android; CI publica artefato como `ASTRYX-S25-Ultra-test-APK`.

## Identificadores e chaves — não modificar

| Propriedade | Mantida da versão S25 Ultra anterior |
|---|---|
| `applicationId` do flavor | `dev.eden.eden_emulator.s25ultra` |
| `applicationIdSuffix` do build de teste | `.relWithDebInfo` |
| Package ID final do APK de teste | `dev.eden.eden_emulator.s25ultra.relWithDebInfo` |
| `namespace` | `org.yuzu.yuzu_emu` |
| Assinatura | mesmíssimo `debug.keystore` e certificado verificado pelo CI |
| Saves/config/driver/userDirectory | paths e APIs sem mudanças |
| `versionCode` | cálculo de build existente, sem reiniciar contagem |

**O Android só atualiza a instalação atual sem apagá-la se** package ID e assinatura coincidirem e o novo `versionCode` não for inferior. O CI checa ID e impressão SHA-256 do certificado, além do nome do launcher. Ainda assim, não temos como testar o armazenamento do seu telefone no CI.

## Instalação segura

1. Antes de testar, exporte seus saves e faça backup das configurações e diretório de dados pelo próprio Eden ou Android, para armazenamento externo.
2. Abra o GitHub Actions `ASTRYX S25 Ultra (test APK)` da branch `brand/astryx-s25-v1`; aguarde status **success**.
3. Baixe o artefato `ASTRYX-S25-Ultra-test-APK` e extraia o APK, sem confundir o ZIP do GitHub com o APK.
4. Instale **sobre a versão S25 Ultra RelWithDebInfo que já estava usando**, sem desinstalar. Se o Android solicitar desinstalar para instalar, **cancele**. Isso indica incompatibilidade de certificado/ID ou outro problema; desinstalar pode apagar os dados.
5. Confirme o nome e ícone ASTRYX, abra seus jogos, perfis gráficos, mods, drivers e saves anteriores.
6. Se quiser reverter, use build **posterior** assinado pela mesma chave e com versionCode maior, contendo o branding Eden anterior. Não instale APK mais antigo com `INSTALL_FAILED_VERSION_DOWNGRADE` nem desinstale como atalho.

## Riscos e verificações remanescentes

- O projeto e o logo são **conceitos originais** de rebrand; não há liberação formal de marca.
- Nome usado publicamente: [Astryx da Meta](https://astryx.atmeta.com/) e [Astryx Interactive](https://astryxgames.com/). É possível haver conflitos de marca.
- Um APK compilado não garante que os saves estejam acessíveis em todos os dispositivos: validar atualização real em Galaxy S25 Ultra com backup.
- O experimento de dublagem PT-BR foi cancelado e não está nesta branch.
- O rebrand visual não altera recursos da emulação, velocidade, fidelidade ou temperatura.

## Créditos

ASTRYX (branding da variante personalizada) é baseado no **Eden Emulator** e herda suas licenças. Preservar avisos de copyright e as condições da GPL ao redistribuir.
