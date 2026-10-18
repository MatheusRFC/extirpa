// SPDX-License-Identifier: MIT
// Formatação de tamanhos e datas para a interface.
#ifndef EXTIRPA_FMT_H
#define EXTIRPA_FMT_H

#include "wstr.h"

typedef struct {
    WCHAR decimal;      // ',' (pt-BR) ou '.' (en-US)
    bool day_first;     // 18/10/2026 (pt-BR) ou 2026-10-18 (en-US)
} FmtStyle;

WCHAR *fmt_size(uint64_t bytes, const FmtStyle *st);         // "512 B", "1,4 KB", "23,5 MB", "1,02 GB"
WCHAR *fmt_date(int y, int m, int d, const FmtStyle *st);
WCHAR *fmt_datetime(int y, int mo, int d, int h, int mi, const FmtStyle *st);
// InstallDate do Registro ("20261018"); aceita também "2026-10-18" e "18/10/2026".
bool parse_install_date(const WCHAR *s, int *y, int *m, int *d);

#endif
