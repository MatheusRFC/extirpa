// SPDX-License-Identifier: MIT
// Janelas de confirmação e de entrada de dados.
#ifndef EXTIRPA_DIALOGS_H
#define EXTIRPA_DIALOGS_H

#include "programs.h"
#include "scan.h"

typedef struct {
    ScanMode mode;
    bool restore_point;
    bool quiet;  // desinstalação rápida: variante silenciosa quando houver
} UninstallOpts;

bool dlg_confirm_uninstall(HWND owner, Program *const *progs, size_t n, bool quick, UninstallOpts *opts);
// Desinstalação forçada: nome e/ou pasta do programa. Entra com os valores iniciais (podem ser NULL).
bool dlg_forced(HWND owner, WCHAR **name, WCHAR **folder, ScanMode *mode);
WCHAR *dlg_input(HWND owner, const WCHAR *title, const WCHAR *prompt, const WCHAR *initial);
WCHAR *dlg_pick_folder(HWND owner, const WCHAR *title);
WCHAR *dlg_pick_file(HWND owner, const WCHAR *title, const WCHAR *filter);

#endif
