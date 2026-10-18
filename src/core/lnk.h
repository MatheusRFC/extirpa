// SPDX-License-Identifier: MIT
// Leitura de atalhos do Windows (.lnk, formato MS-SHLLINK) sem COM: só o necessário para
// saber para onde o atalho aponta.
#ifndef EXTIRPA_LNK_H
#define EXTIRPA_LNK_H

#include "wstr.h"

typedef struct {
    WCHAR *target;   // caminho local do alvo (NULL se o atalho não tiver, como os "anunciados" do MSI)
    WCHAR *workdir;  // pasta de trabalho
    WCHAR *args;     // argumentos
    WCHAR *icon;     // local do ícone
} LnkInfo;

bool lnk_parse(const uint8_t *data, size_t size, LnkInfo *out);
void lnk_free(LnkInfo *l);

#endif
