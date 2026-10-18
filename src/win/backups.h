// SPDX-License-Identifier: MIT
// Backups do Registro (.reg) feitos antes de cada exclusão.
#ifndef EXTIRPA_BACKUPS_H
#define EXTIRPA_BACKUPS_H

#include "../core/wstr.h"
#include "win32.h"

typedef struct {
    WCHAR *path, *file;
    uint64_t size;
    FILETIME mtime;
} BackupFile;

PtrVec backups_list(void);  // do mais novo para o mais antigo
void backups_list_free(PtrVec *v);
// Restaura com o reg.exe do Windows ("reg import"). Devolve NULL se deu certo, senão a mensagem.
WCHAR *backup_restore(const WCHAR *path);

#endif
