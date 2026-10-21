// SPDX-License-Identifier: MIT
// Fluxos completos: desinstalar (um ou vários), desinstalação forçada, rastrear uma
// instalação e desinstalar pelo rastreamento.
#ifndef EXTIRPA_OPS_H
#define EXTIRPA_OPS_H

#include "programs.h"
#include "win32.h"

// Cada função devolve true se algo pode ter mudado (a lista de programas deve ser relida).
bool op_uninstall(HWND owner, Program *const *progs, size_t n, bool quick);
// prog: o programa selecionado na lista (ou NULL); name/folder: valores iniciais da janela.
bool op_forced(HWND owner, Program *prog, const WCHAR *name, const WCHAR *folder);
bool op_trace_install(HWND owner);
bool op_trace_uninstall(HWND owner, const WCHAR *trace_path, const PtrVec *programs);

#endif
