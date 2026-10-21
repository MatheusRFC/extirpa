// SPDX-License-Identifier: MIT
#include "fmt.h"

#include <stdarg.h>

WCHAR *fmt_size(uint64_t bytes, const FmtStyle *st) {
    static const WCHAR *const units[] = {u"B", u"KB", u"MB", u"GB", u"TB"};
    StrBuf b = {0};
    if (bytes < 1024) {
        sb_add_u64(&b, bytes);
        sb_add(&b, u" B");
        return sb_take(&b);
    }
    // valor em centésimos da unidade, arredondado
    int u = 0;
    uint64_t div = 1;
    while (u < 4 && bytes / div >= 1024) div *= 1024, u++;
    uint64_t cent = (bytes * 100 + div / 2) / div;
    if (cent >= 102400 && u < 4) {  // 1023,999 KB arredondaria para 1024,0 KB
        div *= 1024, u++;
        cent = (bytes * 100 + div / 2) / div;
    }
    uint64_t whole = cent / 100;
    if (whole >= 100) {  // 123 MB
        sb_add_u64(&b, (cent + 50) / 100);
    } else if (whole >= 10) {  // 23,5 MB
        uint64_t t = (cent + 5) / 10;
        sb_add_u64(&b, t / 10);
        sb_addc(&b, st->decimal);
        sb_add_u64(&b, t % 10);
    } else {  // 1,02 GB
        sb_add_u64(&b, whole);
        sb_addc(&b, st->decimal);
        sb_add_u64_pad(&b, cent % 100, 2);
    }
    sb_addc(&b, ' ');
    sb_add(&b, units[u]);
    return sb_take(&b);
}

WCHAR *fmt_date(int y, int m, int d, const FmtStyle *st) {
    StrBuf b = {0};
    if (st->day_first) {
        sb_add_u64_pad(&b, (uint64_t)d, 2), sb_addc(&b, '/');
        sb_add_u64_pad(&b, (uint64_t)m, 2), sb_addc(&b, '/');
        sb_add_u64_pad(&b, (uint64_t)y, 4);
    } else {
        sb_add_u64_pad(&b, (uint64_t)y, 4), sb_addc(&b, '-');
        sb_add_u64_pad(&b, (uint64_t)m, 2), sb_addc(&b, '-');
        sb_add_u64_pad(&b, (uint64_t)d, 2);
    }
    return sb_take(&b);
}

WCHAR *fmt_datetime(int y, int mo, int d, int h, int mi, const FmtStyle *st) {
    WCHAR *date = fmt_date(y, mo, d, st);
    StrBuf b = {0};
    sb_add(&b, date);
    sb_addc(&b, ' ');
    sb_add_u64_pad(&b, (uint64_t)h, 2), sb_addc(&b, ':');
    sb_add_u64_pad(&b, (uint64_t)mi, 2);
    xfree(date);
    return sb_take(&b);
}

static int digits_at(const WCHAR *s, int n) {
    int v = 0;
    for (int i = 0; i < n; i++) {
        if (!w_is_digit(s[i])) return -1;
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

static bool valid_date(int y, int m, int d) { return y >= 1980 && y <= 2200 && m >= 1 && m <= 12 && d >= 1 && d <= 31; }

bool parse_install_date(const WCHAR *s, int *y, int *m, int *d) {
    if (!s) return false;
    size_t n = wlen(s);
    int yy = -1, mm = -1, dd = -1;
    if (n == 8) yy = digits_at(s, 4), mm = digits_at(s + 4, 2), dd = digits_at(s + 6, 2);
    else if (n == 10 && s[4] == '-' && s[7] == '-') yy = digits_at(s, 4), mm = digits_at(s + 5, 2), dd = digits_at(s + 8, 2);
    else if (n == 10 && s[2] == '/' && s[5] == '/') dd = digits_at(s, 2), mm = digits_at(s + 3, 2), yy = digits_at(s + 6, 4);
    if (!valid_date(yy, mm, dd)) return false;
    *y = yy, *m = mm, *d = dd;
    return true;
}

WCHAR *wfmt(const WCHAR *pattern, ...) {
    va_list ap;
    va_start(ap, pattern);
    StrBuf b = {0};
    for (const WCHAR *p = pattern; *p; p++) {
        if (*p != '%' || !p[1]) {
            sb_addc(&b, *p);
            continue;
        }
        p++;
        switch (*p) {
        case 's': {
            const WCHAR *s = va_arg(ap, const WCHAR *);
            sb_add(&b, s ? s : u"");
            break;
        }
        case 'd': {
            int v = va_arg(ap, int);
            if (v < 0) sb_addc(&b, '-'), sb_add_u64(&b, (uint64_t)(-(int64_t)v));
            else sb_add_u64(&b, (uint64_t)v);
            break;
        }
        case 'u': sb_add_u64(&b, va_arg(ap, unsigned)); break;
        case 'U': sb_add_u64(&b, va_arg(ap, uint64_t)); break;
        case 'c': sb_addc(&b, (WCHAR)va_arg(ap, int)); break;
        case '%': sb_addc(&b, '%'); break;
        default: sb_addc(&b, '%'), sb_addc(&b, *p); break;
        }
    }
    va_end(ap);
    return sb_take(&b);
}

WCHAR *base64_utf16le(const WCHAR *s) {
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t n = wlen(s) * 2;
    uint8_t *bytes = xalloc(n + 1);
    for (size_t i = 0; i < n / 2; i++) bytes[2 * i] = (uint8_t)(s[i] & 0xFF), bytes[2 * i + 1] = (uint8_t)(s[i] >> 8);
    StrBuf b = {0};
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)bytes[i] << 16;
        if (i + 1 < n) v |= (uint32_t)bytes[i + 1] << 8;
        if (i + 2 < n) v |= bytes[i + 2];
        sb_addc(&b, (WCHAR)tbl[(v >> 18) & 63]);
        sb_addc(&b, (WCHAR)tbl[(v >> 12) & 63]);
        sb_addc(&b, i + 1 < n ? (WCHAR)tbl[(v >> 6) & 63] : '=');
        sb_addc(&b, i + 2 < n ? (WCHAR)tbl[v & 63] : '=');
    }
    xfree(bytes);
    return sb_take(&b);
}
