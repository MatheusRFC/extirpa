// SPDX-License-Identifier: MIT
// Janela das sobras encontradas: o usuário confere, marca e apaga.
#ifndef EXTIRPA_LEFTWIN_H
#define EXTIRPA_LEFTWIN_H

#include "../core/leftover.h"
#include "win32.h"

// label: nome do programa (vai no nome do backup e no log). note: aviso opcional no topo.
void leftovers_show(HWND owner, const WCHAR *label, const WCHAR *note, LeftList *list);

#endif
