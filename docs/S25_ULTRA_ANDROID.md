# Eden S25 Ultra — Android V1

Este projeto personaliza o código aberto do Eden (respeitando licenças GPL e demais licenças de componentes), sem incluir jogos, firmware ou chaves proprietárias.

## Escopo implementado

- Variante Gradle independente s25Ultra, package dev.eden.eden_emulator.s25ultra, arquitetura ARM64, Android 15 ou superior.
- Detecção da família Samsung Galaxy S25 Ultra, modelo SM-S938.
- Acesso a perfis pela tela Configurações, apenas nesse aparelho.
- Desempenho: escala interna 0,75x, cache de shaders, GPU assíncrona, sobreposição com FPS e frametime.
- Equilibrado: escala 1x, cache de shaders e GPU assíncrona; sobreposição desativada.
- Qualidade: escala 1,5x, cache de shaders e GPU assíncrona; sobreposição desativada.
- Backup das configurações globais alteradas ao aplicar um perfil pela primeira vez e restauração manual dos valores anteriores.
- Informações do aparelho, arquitetura, RAM, status térmico fornecido pelo Android e versão Vulkan, com compartilhamento manual do texto.

Não há alteração automática de driver, overclock, bypass térmico, spoof de outros jogos ou alteração de configurações por jogo.

## Compilação

Requer JDK 17, Android SDK 36, NDK 28.2.13676358, CMake 3.31.6 e submódulos. Na raiz do repositório:

~~~bash
git clone --recurse-submodules --branch android/s25-ultra-profile-v1 https://github.com/otaviobarretojr/Eden-Custom.git
cd Eden-Custom/src/android
./gradlew :app:assembleS25UltraRelWithDebInfo
~~~

O workflow "Android S25 Ultra (test APK)" tenta compilar a variante e salva um artefato apenas se concluir sem erros. A assinatura de teste é de depuração; não é um release de produção. Compilação e testes reais precisam ser verificados separadamente.

## Checklist de testes no Galaxy S25 Ultra

1. Instalar ao lado do Eden padrão; realizar backup antes de manipular dados.
2. Abrir Configurações e conferir o diagnóstico de Vulkan e hardware.
3. Testar o perfil Equilibrado, executar o mesmo jogo com cenário reproduzível e observar FPS/frametime, áudio e crashes.
4. Comparar Desempenho e Qualidade, uma alteração de cada vez.
5. Acompanhar temperatura, queda de desempenho e consumo de bateria em sessões longas.
6. Validar opção de restaurar os valores anteriores e persistência após reinício.
7. Testar gamepad Bluetooth, controles por toque, biblioteca de jogos e atualizações locais.

Os perfis são hipóteses iniciais de ajuste; não demonstram ganhos de FPS sem medições reais. A emulação exige cópias de jogos e arquivos que o usuário tenha direito de utilizar.
