// SPDX-License-Identifier: MIT
// Base comum do EXTIRPA: tipos, memória e vetores.
//
// O núcleo (src/core) não usa a biblioteca C: compila igual no Windows (sem CRT nenhum,
// ver src/win/rt.c) e no Linux (onde roda a bateria de testes). Toda string é UTF-16,
// como na API do Windows, com literais u"...".
#ifndef EXTIRPA_BASE_H
#define EXTIRPA_BASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef unsigned short WCHAR;

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define mem_copy __builtin_memcpy
#define mem_move __builtin_memmove
#define mem_set __builtin_memset
#define mem_cmp __builtin_memcmp

// Memória: implementada por cada plataforma (src/win/platform.c no Windows, tests/ no Linux).
// xalloc devolve memória zerada e encerra o programa se faltar memória: nenhum chamador
// precisa testar NULL.
void *xalloc(size_t n);
void *xrealloc(void *p, size_t n);
void xfree(void *p);

// Vetor de ponteiros (a lista de programas, de sobras, de itens de rastreamento...).
typedef struct {
    void **items;
    size_t n, cap;
} PtrVec;

void pv_push(PtrVec *v, void *item);
void pv_insert(PtrVec *v, size_t pos, void *item);  // pos <= n; os seguintes andam uma casa
void pv_free(PtrVec *v);  // libera só o vetor, não os itens
void pv_sort(PtrVec *v, int (*cmp)(const void *a, const void *b, void *ctx), void *ctx);

#endif
