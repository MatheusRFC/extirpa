// SPDX-License-Identifier: MIT
// Caminhos de arquivo e de chaves do Registro (os dois usam '\' como separador).
#ifndef EXTIRPA_PATHX_H
#define EXTIRPA_PATHX_H

#include "wstr.h"

WCHAR *path_norm(const WCHAR *p);   // tira aspas e espaços nas pontas, '/' -> '\', sem '\' repetida nem no fim
WCHAR *path_join(const WCHAR *a, const WCHAR *b);
const WCHAR *path_leaf(const WCHAR *p);  // o que vem depois da última '\'
WCHAR *path_parent(const WCHAR *p);      // NULL se não houver pai
bool path_eq(const WCHAR *a, const WCHAR *b);            // sem diferenciar maiúsculas
bool path_is_under(const WCHAR *child, const WCHAR *parent);  // estritamente dentro
bool path_is_under_or_eq(const WCHAR *child, const WCHAR *parent);
WCHAR *path_strip_ext(const WCHAR *name);  // "app.exe" -> "app"
const WCHAR *path_ext(const WCHAR *p);     // ".exe" (ou "" se não tiver)

#endif
