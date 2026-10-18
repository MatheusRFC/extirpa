// SPDX-License-Identifier: MIT
// Textos da interface em português (Brasil) e inglês. O idioma segue o do Windows:
// português quando a interface do Windows está em português, inglês nos outros casos.
// Os textos ficam em i18n_strings.h, os dois idiomas lado a lado.
#ifndef EXTIRPA_I18N_H
#define EXTIRPA_I18N_H

#include "../core/wstr.h"

typedef enum {
#define X(id, pt, en) id,
#include "i18n_strings.h"
#undef X
    S_COUNT
} StrId;

void i18n_init(void);
const WCHAR *T(StrId id);
bool i18n_pt(void);

#endif
