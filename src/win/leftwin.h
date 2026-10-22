// SPDX-License-Identifier: MIT
// Janela das sobras encontradas: o usuário confere, marca e apaga.
#ifndef EXTIRPA_LEFTWIN_H
#define EXTIRPA_LEFTWIN_H

#include "../core/leftover.h"
#include "win32.h"

// label: nome do programa (vai no nome do backup e no log). note: aviso opcional no topo.
// Duas etapas: primeiro o Registro e o sistema, depois os arquivos e pastas.
void leftovers_show(HWND owner, const WCHAR *label, const WCHAR *note, LeftList *list);
// Etapa em que a próxima janela abre (0 = Registro, 1 = arquivos e pastas); só o autoteste usa.
void leftovers_set_start_page(int page);

#endif
