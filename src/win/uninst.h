// SPDX-License-Identifier: MIT
// Execução do desinstalador do próprio programa e ponto de restauração do sistema.
#ifndef EXTIRPA_UNINST_H
#define EXTIRPA_UNINST_H

#include "programs.h"
#include "win32.h"

// Linha de comando para desinstalar. quiet: a variante silenciosa (desinstalação rápida),
// quando existe. NULL se o programa não tiver desinstalador registrado.
WCHAR *uninstall_command(const Program *p, bool quiet, bool *is_quiet);

typedef struct {
    bool started;
    DWORD error;      // erro ao iniciar (started == false)
    DWORD exit_code;  // código de saída do desinstalador
    bool skipped;     // o usuário mandou seguir sem esperar
} RunResult;

// Roda o comando e espera ele e todos os processos que ele abrir terminarem (job object).
// skip_event: quando sinalizado, para de esperar (o desinstalador continua rodando).
void run_and_wait(const WCHAR *cmdline, HANDLE skip_event, RunResult *r);

// Ponto de restauração do Windows. Devolve false (com o erro) se a Restauração do
// Sistema estiver desligada ou falhar.
bool restore_point_begin(const WCHAR *description, INT64 *sequence, DWORD *error);
void restore_point_end(INT64 sequence, bool cancelled);

#endif
