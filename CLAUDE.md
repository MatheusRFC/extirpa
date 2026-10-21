# EXTIRPA — instruções para o Claude Code

Desinstalador completo para Windows (equivalente ao Revo Uninstaller, escrito do zero) em C11 freestanding: sem CRT, sem SDK do Windows, sem MinGW, um `.exe` só. Licença MIT.

## Comandos
- Testes: `python3 tools/build.py test` — núcleo (`tests/test_core.c`) e camada Windows contra Registro e disco simulados (`tests/test_win.c` + `tests/fakewin.c`, flag `-DEXTIRPA_FAKE_WIN`). No Linux rodam com ASan/UBSan (gcc); no Windows só o núcleo roda.
- Executável: `python3 tools/build.py` → `build/extirpa.exe`. Só precisa do LLVM (clang, lld-link, llvm-rc, llvm-dlltool); funciona no Windows e no Linux.
- Tudo de uma vez: `python3 tools/build.py all` (testes, `extirpa.exe` e `extirpa-selftest.exe`).
- Autoteste num Windows de verdade: `extirpa-selftest.exe --selftest` (relatório e capturas em `%LOCALAPPDATA%\EXTIRPA\selftest`). Teste com programas reais: `--snapshot-save <arquivo>` antes de instalar e `--realtest <arquivo> <nomes...>` depois (relatório em `selftest\real\report.txt`, com o que FICOU). O `selftest.c` só entra no executável de autoteste (`-DEXTIRPA_SELFTEST`).
- Declarações do Windows: `src/win/win32.h` é escrito à mão. Depois de mexer nele, `python3 tools/check_win32.py --mingw <pasta mingw-w64-headers>` (a do código-fonte do mingw-w64, com `include/` e `crt/`) confere assinaturas, constantes, campos e GUIDs contra os cabeçalhos do mingw-w64. As `.lib` de importação são geradas pelo `build.py` a partir das funções declaradas no `win32.h`.
- Ícones: `python3 tools/gerar_icones.py` (Pillow). Desenho-mestre: `assets/extirpa.svg`.
- Release: `python3 scripts/package.py` → `dist/` (`extirpa.exe`, zip e `SHA256SUMS`).

## Regras do projeto
- Sem dependências externas e sem biblioteca C: só APIs do Windows, declaradas em `win32.h`. `src/win/rt.c` fornece `memset`/`memcpy`/`memmove`/`memcmp`.
- `src/core/` não chama o Windows (é testado no Linux); tudo o que toca o sistema fica em `src/win/`.
- Todo arquivo de código começa com `// SPDX-License-Identifier: MIT` (ou `#` em Python/shell).
- Comentários no código e mensagens de log em português.
- Todo texto que o usuário vê passa por `src/win/i18n_strings.h`, em pt-BR e en-US (en-US para os outros idiomas do Windows).
- O README existe em 8 idiomas (`README.md` + `docs/i18n/README.<idioma>.md`): mudança em um vai para todos.
- Versão: `EXTIRPA_VERSION` em `src/win/platform.h` e `FILEVERSION`/`PRODUCTVERSION`/`FileVersion`/`ProductVersion` em `assets/app.rc` (o `package.py` confere). Mudanças entram no `CHANGELOG.md` (seção `[Unreleased]` até a release).
- Commits em inglês. Versões: `EXTIRPA x.y.z: resumo`; documentação: `docs: ...`.
- Segurança: a lista de proteção (`src/core/protect.c` e `platform.c`), as regras de dono da pasta mais específica, de nomes compartilhados com outro programa instalado e de pastas que contêm outro programa (`scan.c`), o backup `.reg` antes de mexer no Registro, a Lixeira para arquivos e as tarefas `\Microsoft\` intocadas não podem ser enfraquecidos sem discutir antes. Toda mudança na busca de sobras precisa de cenário em `tests/test_win.c`, incluindo o que NÃO pode ser apagado.
