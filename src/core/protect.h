// SPDX-License-Identifier: MIT
// Lista de proteção: o que o EXTIRPA nunca apaga, venha a sugestão de onde vier
// (varredura de sobras, rastreamento de instalação ou seleção manual).
//
// Regra geral: um caminho é bloqueado se for igual a um caminho protegido ou se for
// ANCESTRAL de um (apagar "C:\Users" apagaria "C:\Users\fulano\AppData"). Além disso,
// algumas árvores são proibidas por inteiro (a pasta do Windows, HKLM\SYSTEM, o banco
// de dados do Windows Installer...).
#ifndef EXTIRPA_PROTECT_H
#define EXTIRPA_PROTECT_H

#include "wstr.h"

// Pastas do sistema e do usuário, descobertas em tempo de execução (src/win/platform.c).
void protect_add_dir(const WCHAR *path);
void protect_add_dir_tree(const WCHAR *path);  // proíbe a pasta e tudo dentro
void protect_reset(void);

bool protect_dir_blocked(const WCHAR *path);   // vale para pastas e arquivos
bool protect_key_blocked(const WCHAR *key);    // chave no formato "HKLM\SOFTWARE\..."
// Valor de Registro: bloqueado se a chave estiver numa árvore proibida (valores soltos
// dentro de chaves protegidas, como um item de Run, são permitidos).
bool protect_value_blocked(const WCHAR *key);
// Chave de rastros de uso do Windows (MuiCache, compatibilidade): valores podem sair.
bool protect_usage_key(const WCHAR *key);

#endif
