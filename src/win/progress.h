// SPDX-License-Identifier: MIT
// Janela de progresso modal com uma thread de trabalho: a interface continua respondendo
// enquanto o desinstalador roda ou a varredura acontece.
#ifndef EXTIRPA_PROGRESS_H
#define EXTIRPA_PROGRESS_H

#include "scan.h"
#include "win32.h"

typedef struct Progress Progress;
typedef void (*WorkFn)(Progress *pg, void *arg);

// Roda fn(pg, arg) numa thread e mostra a janela até terminar.
void progress_run(HWND owner, const WCHAR *title, WorkFn fn, void *arg);

// Chamadas da thread de trabalho:
void pg_step(Progress *pg, const WCHAR *text);    // etapa (texto em negrito)
void pg_detail(Progress *pg, const WCHAR *text);  // detalhe da etapa
void pg_button(Progress *pg, const WCHAR *label);  // mostra o botão extra (NULL esconde)
HANDLE pg_button_event(Progress *pg);             // sinalizado quando o botão extra é clicado
HANDLE pg_cancel_event(Progress *pg);             // sinalizado quando "Cancelar" é clicado
void pg_allow_cancel(Progress *pg, bool allow);
bool pg_cancelled(Progress *pg);
Ctl *pg_ctl(Progress *pg);                        // para scan_*/delete_* (progresso no detalhe)
int pg_ask(Progress *pg, const WCHAR *text, UINT flags);  // MessageBox mostrado pela interface
HWND pg_window(Progress *pg);

#endif
