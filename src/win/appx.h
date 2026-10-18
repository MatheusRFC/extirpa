// SPDX-License-Identifier: MIT
// Aplicativos do Windows (pacotes da Microsoft Store / AppX), via PowerShell do próprio
// Windows (Get-AppxPackage e Remove-AppxPackage).
#ifndef EXTIRPA_APPX_H
#define EXTIRPA_APPX_H

#include "../core/wstr.h"
#include "win32.h"

typedef struct {
    WCHAR *name, *version, *publisher, *full_name, *location;
    bool removable;  // false: componente do sistema (NonRemovable ou assinatura "System")
} AppxPackage;

// NULL em *error_text se deu certo
PtrVec appx_list(WCHAR **error_text);
void appx_list_free(PtrVec *v);
// Remove o pacote (do usuário atual ou de todos os usuários). Devolve NULL se deu certo,
// senão o texto do erro.
WCHAR *appx_remove(const WCHAR *full_name, bool all_users);
bool appx_valid_name(const WCHAR *full_name);

#endif
