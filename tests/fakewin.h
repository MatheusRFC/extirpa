// SPDX-License-Identifier: MIT
// Registro e disco simulados para os testes da parte Windows (ver fakewin.c).
#ifndef EXTIRPA_FAKEWIN_H
#define EXTIRPA_FAKEWIN_H

#include "../src/win/win32.h"

void fake_reset(void);     // Registro e disco vazios
void fake_fs_reset(void);  // só o disco

// Registro
void fake_key(const WCHAR *path);
void fake_val(const WCHAR *path, const WCHAR *name, DWORD type, const void *data, DWORD size);
void fake_str(const WCHAR *path, const WCHAR *name, const WCHAR *value);
void fake_dword(const WCHAR *path, const WCHAR *name, DWORD value);
bool fake_key_exists(const WCHAR *path);
bool fake_val_exists(const WCHAR *path, const WCHAR *name);

// Disco (só a unidade C:)
void fake_dir(const WCHAR *path);
void fake_file(const WCHAR *path, const void *data, size_t size);
bool fake_exists(const WCHAR *path);
bool fake_read(const WCHAR *path, uint8_t **data, size_t *len);
size_t fake_recycled_count(void);
void fake_short_name(const WCHAR *long_path, const WCHAR *short_path);  // para GetShortPathNameW
size_t fake_command_count(void);                                          // programas chamados (schtasks, PowerShell)
const WCHAR *fake_command(size_t i);
const WCHAR *fake_recycled(size_t i);

#endif
