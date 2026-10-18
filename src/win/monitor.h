// SPDX-License-Identifier: MIT
// Rastreamento de instalação: fotos do Registro e das pastas antes e depois de instalar.
#ifndef EXTIRPA_MONITOR_H
#define EXTIRPA_MONITOR_H

#include "../core/trace.h"
#include "scan.h"

// Primeira foto (before == true) ou segunda (comparação). Na segunda, o que é novo vai
// para s->out.
void monitor_snapshot(Snapshot *s, bool second, Ctl *ctl);

// Nome sugerido para o rastreamento: o DisplayName de uma entrada de desinstalação criada
// durante a instalação, se houver.
WCHAR *trace_guess_name(const Trace *t);

// Arquivos de rastreamento (.xtr) salvos
typedef struct {
    WCHAR *path, *name, *date;
    size_t items;
} TraceFile;

PtrVec traces_list(void);  // TraceFile*
void traces_list_free(PtrVec *v);
bool trace_save(const Trace *t, WCHAR **saved_path);
Trace *trace_load(const WCHAR *path);

#endif
