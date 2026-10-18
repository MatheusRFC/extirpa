// SPDX-License-Identifier: MIT
// Arquivos .reg (o formato do Editor do Registro, versão 5.00), usados nos backups:
// antes de apagar uma chave ou um valor, o EXTIRPA grava o conteúdo num .reg que
// devolve tudo com um clique duplo (ou pela aba Backups).
#ifndef EXTIRPA_REGTEXT_H
#define EXTIRPA_REGTEXT_H

#include "wstr.h"

// Tipos de valor do Registro (os mesmos números do Windows).
enum { RT_NONE = 0, RT_SZ = 1, RT_EXPAND_SZ = 2, RT_BINARY = 3, RT_DWORD = 4, RT_DWORD_BE = 5, RT_LINK = 6, RT_MULTI_SZ = 7, RT_QWORD = 11 };

// "HKLM\SOFTWARE\X" -> "HKEY_LOCAL_MACHINE\SOFTWARE\X" (e o caminho inverso)
WCHAR *reg_long_name(const WCHAR *key);
WCHAR *reg_short_name(const WCHAR *key);

void regtext_header(StrBuf *b);
void regtext_key(StrBuf *b, const WCHAR *key);  // "[HKEY_...\...]"
// name NULL ou "" = valor padrão (@).
void regtext_value(StrBuf *b, const WCHAR *name, uint32_t type, const uint8_t *data, uint32_t size);
void regtext_comment(StrBuf *b, const WCHAR *text);  // "; texto"

#endif
