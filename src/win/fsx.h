// SPDX-License-Identifier: MIT
// Arquivos e pastas: listar, percorrer, medir e apagar (pela Lixeira).
#ifndef EXTIRPA_FSX_H
#define EXTIRPA_FSX_H

#include "../core/wstr.h"
#include "win32.h"

typedef struct {
    WCHAR *name;
    bool is_dir;
    bool is_link;  // junção ou link simbólico: nunca é seguido
    uint64_t size;
    FILETIME mtime;
} DirEntry;

PtrVec fs_list(const WCHAR *dir);  // DirEntry* (sem "." e "..")
void fs_list_free(PtrVec *v);

// Tamanho de uma pasta, parando depois de max_files arquivos (para não travar em pastas
// gigantes). Junções não são seguidas.
uint64_t fs_dir_size(const WCHAR *dir, size_t max_files);
bool fs_dir_empty(const WCHAR *dir);

// Manda para a Lixeira (sem perguntas). Em unidades sem Lixeira o Windows apaga direto.
DWORD fs_recycle(const WCHAR *path);
// Agenda a exclusão para a próxima reinicialização (pastas: o conteúdo antes).
bool fs_delete_on_reboot(const WCHAR *path);

#endif
