// SPDX-License-Identifier: MIT
// Registro do Windows com caminhos em texto ("HKLM\SOFTWARE\...").
//
// O EXTIRPA é um programa de 64 bits e abre tudo com KEY_WOW64_64KEY: a visão de 32 bits
// é acessada pelo caminho explícito (HKLM\SOFTWARE\WOW6432Node\...), sem redirecionamento
// escondido.
#ifndef EXTIRPA_REG_H
#define EXTIRPA_REG_H

#include "../core/wstr.h"
#include "win32.h"

typedef struct {
    WCHAR *name;
    DWORD type;
    uint8_t *data;
    DWORD size;
} RegValue;

HKEY reg_open(const WCHAR *path, REGSAM access);  // NULL se não abrir
HKEY reg_open_sub(HKEY parent, const WCHAR *sub);  // só leitura
bool reg_exists(const WCHAR *path);
WCHAR *reg_get_str(HKEY k, const WCHAR *name);   // REG_SZ ou REG_EXPAND_SZ (sem expandir); NULL se não houver
bool reg_get_dword(HKEY k, const WCHAR *name, DWORD *out);
uint64_t reg_last_write(HKEY k);                  // FILETIME em 100 ns
PtrVec reg_subkeys(HKEY k);                       // WCHAR* (nomes)
PtrVec reg_values(HKEY k);                        // RegValue*
PtrVec reg_value_names(HKEY k);                   // WCHAR* (só os nomes, mais rápido)
void reg_values_free(PtrVec *v);
void strings_free(PtrVec *v);                     // libera um PtrVec de WCHAR*
bool reg_key_empty(const WCHAR *path);            // sem subchaves e sem valores

// Backup: acrescenta ao texto .reg o conteúdo da chave (com subchaves) ou de um valor.
bool reg_export_key(StrBuf *out, const WCHAR *path);
bool reg_export_value(StrBuf *out, const WCHAR *path, const WCHAR *value);

DWORD reg_delete_key(const WCHAR *path);
DWORD reg_delete_value(const WCHAR *path, const WCHAR *value);
DWORD reg_set_binary(const WCHAR *path, const WCHAR *value, const uint8_t *data, DWORD size);  // cria a chave se precisar

#endif
