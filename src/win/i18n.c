// SPDX-License-Identifier: MIT
#include "i18n.h"

#include "platform.h"

static const WCHAR *const PT[] = {
#define X(id, pt, en) pt,
#include "i18n_strings.h"
#undef X
};

static const WCHAR *const EN[] = {
#define X(id, pt, en) en,
#include "i18n_strings.h"
#undef X
};

static bool g_pt;

void i18n_init(void) {
    g_pt = (GetUserDefaultUILanguage() & 0x3FF) == LANG_PORTUGUESE;
    g_fmt.decimal = g_pt ? ',' : '.';
    g_fmt.day_first = g_pt;
}

const WCHAR *T(StrId id) { return (unsigned)id < S_COUNT ? (g_pt ? PT[id] : EN[id]) : u""; }

bool i18n_pt(void) { return g_pt; }
