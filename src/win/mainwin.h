// SPDX-License-Identifier: MIT
// Janela principal: abas Programas, Apps do Windows, Rastreados, Inicialização e Backups.
#ifndef EXTIRPA_MAINWIN_H
#define EXTIRPA_MAINWIN_H

#include "win32.h"

HWND mainwin_create(void);
// Teclas de atalho globais (Ctrl+F, F5) antes do IsDialogMessage. true = já tratada.
bool mainwin_pretranslate(MSG *m);
HWND mainwin_handle(void);

#endif
