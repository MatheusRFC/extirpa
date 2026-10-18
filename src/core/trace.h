// SPDX-License-Identifier: MIT
// Rastreamento de instalação: duas "fotos" do sistema (antes e depois de instalar) e a
// diferença entre elas, gravada num arquivo de rastreamento (.xtr, texto UTF-8).
//
// A primeira foto guarda só um hash de 64 bits de cada chave, valor, pasta e arquivo.
// Na segunda, o que não estava na primeira foi criado pela instalação. Uma chave ou pasta
// nova entra inteira (não é preciso descer nela); valores e arquivos novos dentro de
// chaves e pastas que já existiam entram um a um.
#ifndef EXTIRPA_TRACE_H
#define EXTIRPA_TRACE_H

#include "wstr.h"

typedef enum { TR_KEY = 'K', TR_VALUE = 'V', TR_DIR = 'D', TR_FILE = 'F' } TraceKind;

typedef struct {
    TraceKind kind;
    WCHAR *path;   // chave ("HKLM\...") ou caminho de arquivo/pasta
    WCHAR *value;  // nome do valor (só TR_VALUE; "" = valor padrão)
} TraceItem;

typedef struct {
    WCHAR *name;       // nome do programa rastreado
    WCHAR *installer;  // instalador que foi executado
    WCHAR *date;       // "2026-10-18 14:05"
    PtrVec items;      // TraceItem*
} Trace;

typedef struct {
    uint64_t *slots;
    size_t cap, n;
} HashSet;

uint64_t hash_entry(TraceKind kind, const WCHAR *path, const WCHAR *value);  // sem diferenciar maiúsculas
void hs_add(HashSet *s, uint64_t h);
bool hs_has(const HashSet *s, uint64_t h);
void hs_free(HashSet *s);

typedef struct {
    HashSet before;
    Trace *out;
    size_t added;
} Snapshot;

// Primeira foto: registra cada entrada encontrada.
void snap_record(Snapshot *s, TraceKind kind, const WCHAR *path, const WCHAR *value);
// Segunda foto: devolve true se a entrada já existia (para chaves e pastas: pode descer).
// Se é nova, entra no rastreamento e devolve false.
bool snap_check(Snapshot *s, TraceKind kind, const WCHAR *path, const WCHAR *value);

Trace *trace_new(void);
void trace_add(Trace *t, TraceKind kind, const WCHAR *path, const WCHAR *value);
void trace_free(Trace *t);
char *trace_serialize(const Trace *t, size_t *len);  // UTF-8
Trace *trace_parse(const char *data, size_t len);    // NULL se não for um rastreamento

#endif
