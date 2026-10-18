// SPDX-License-Identifier: MIT
#include "wstr.h"

size_t wlen(const WCHAR *s) {
    size_t n = 0;
    if (s)
        while (s[n]) n++;
    return n;
}

WCHAR *wdupn(const WCHAR *s, size_t n) {
    WCHAR *r = xalloc((n + 1) * sizeof(WCHAR));
    if (s && n) mem_copy(r, s, n * sizeof(WCHAR));
    r[n] = 0;
    return r;
}

WCHAR *wdup(const WCHAR *s) { return wdupn(s, wlen(s)); }

WCHAR *wcat(const WCHAR *a, const WCHAR *b) { return wcat3(a, b, NULL); }

WCHAR *wcat3(const WCHAR *a, const WCHAR *b, const WCHAR *c) {
    size_t la = wlen(a), lb = wlen(b), lc = wlen(c);
    WCHAR *r = xalloc((la + lb + lc + 1) * sizeof(WCHAR));
    if (la) mem_copy(r, a, la * sizeof(WCHAR));
    if (lb) mem_copy(r + la, b, lb * sizeof(WCHAR));
    if (lc) mem_copy(r + la + lb, c, lc * sizeof(WCHAR));
    return r;
}

WCHAR *w_from_ascii(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    WCHAR *r = xalloc((n + 1) * sizeof(WCHAR));
    for (size_t i = 0; i < n; i++) r[i] = (unsigned char)s[i];
    return r;
}

WCHAR wfold(WCHAR c) {
    if (c < 0x80) return (c >= 'A' && c <= 'Z') ? (WCHAR)(c + 32) : c;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return (WCHAR)(c + 32);
    if (c >= 0x100 && c <= 0x137) return (WCHAR)(c | 1);  // pares maiúscula/minúscula
    if (c >= 0x139 && c <= 0x148) return (c & 1) ? (WCHAR)(c + 1) : c;
    if (c >= 0x14A && c <= 0x177) return (WCHAR)(c | 1);
    if (c >= 0x391 && c <= 0x3AB && c != 0x3A2) return (WCHAR)(c + 32);  // grego
    if (c >= 0x410 && c <= 0x42F) return (WCHAR)(c + 32);                  // cirílico
    if (c >= 0x400 && c <= 0x40F) return (WCHAR)(c + 80);
    return c;
}

int wcmp(const WCHAR *a, const WCHAR *b) {
    for (;; a++, b++) {
        if (*a != *b) return *a < *b ? -1 : 1;
        if (!*a) return 0;
    }
}

int wicmp(const WCHAR *a, const WCHAR *b) {
    for (;; a++, b++) {
        WCHAR x = wfold(*a), y = wfold(*b);
        if (x != y) return x < y ? -1 : 1;
        if (!x) return 0;
    }
}

bool weq(const WCHAR *a, const WCHAR *b) { return wcmp(a, b) == 0; }
bool wieq(const WCHAR *a, const WCHAR *b) { return wicmp(a, b) == 0; }

bool wieqn(const WCHAR *a, const WCHAR *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        WCHAR x = wfold(a[i]), y = wfold(b[i]);
        if (x != y) return false;
        if (!x) return true;
    }
    return true;
}

bool wistarts(const WCHAR *s, const WCHAR *prefix) {
    size_t n = wlen(prefix);
    return wlen(s) >= n && wieqn(s, prefix, n);
}

bool wiends(const WCHAR *s, const WCHAR *suffix) {
    size_t ls = wlen(s), lx = wlen(suffix);
    return ls >= lx && wieqn(s + ls - lx, suffix, lx);
}

const WCHAR *wifind(const WCHAR *hay, const WCHAR *needle) {
    size_t n = wlen(needle);
    if (!n) return hay;
    WCHAR first = wfold(needle[0]);
    for (const WCHAR *p = hay; *p; p++)
        if (wfold(*p) == first && wieqn(p, needle, n)) return p;
    return NULL;
}

const WCHAR *wfindc(const WCHAR *s, WCHAR c) {
    for (; *s; s++)
        if (*s == c) return s;
    return NULL;
}

const WCHAR *wrfindc(const WCHAR *s, WCHAR c) {
    const WCHAR *r = NULL;
    for (; *s; s++)
        if (*s == c) r = s;
    return r;
}

bool w_is_space(WCHAR c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 0xA0 || c == 0x3000; }
bool w_is_digit(WCHAR c) { return c >= '0' && c <= '9'; }

bool w_is_alnum(WCHAR c) {
    if (c < 0x80) return (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'z');
    if (c < 0xC0) return c == 0xAA || c == 0xB5 || c == 0xBA;
    if (c == 0xD7 || c == 0xF7) return false;
    if (c >= 0x2000 && c <= 0x2BFF) return false;  // pontuação, símbolos, setas, desenhos
    if (c >= 0x3000 && c <= 0x303F) return false;  // pontuação CJK
    if (c >= 0xD800 && c <= 0xDFFF) return true;   // fora do plano básico: conta como letra
    if (c >= 0xFE30 && c <= 0xFE6F) return false;
    if (c >= 0xFF00 && c <= 0xFF0F) return false;
    return true;
}

WCHAR *wtrim_dup(const WCHAR *s) {
    if (!s) return wdup(NULL);
    while (*s && w_is_space(*s)) s++;
    size_t n = wlen(s);
    while (n && w_is_space(s[n - 1])) n--;
    return wdupn(s, n);
}

bool w_to_u64(const WCHAR *s, uint64_t *out) {
    uint64_t v = 0;
    if (!s || !*s) return false;
    for (; *s; s++) {
        if (!w_is_digit(*s)) return false;
        uint64_t d = (uint64_t)(*s - '0');
        if (v > (UINT64_MAX - d) / 10) return false;
        v = v * 10 + d;
    }
    *out = v;
    return true;
}

static void sb_grow(StrBuf *b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) return;
    size_t cap = b->cap ? b->cap : 64;
    while (cap < b->len + extra + 1) cap *= 2;
    b->p = xrealloc(b->p, cap * sizeof(WCHAR));
    b->cap = cap;
}

void sb_addn(StrBuf *b, const WCHAR *s, size_t n) {
    sb_grow(b, n);
    if (n) mem_copy(b->p + b->len, s, n * sizeof(WCHAR));
    b->len += n;
    b->p[b->len] = 0;
}

void sb_add(StrBuf *b, const WCHAR *s) { sb_addn(b, s, wlen(s)); }

void sb_addc(StrBuf *b, WCHAR c) { sb_addn(b, &c, 1); }

void sb_add_ascii(StrBuf *b, const char *s) {
    for (; *s; s++) sb_addc(b, (unsigned char)*s);
}

void sb_add_u64_pad(StrBuf *b, uint64_t v, int width) {
    WCHAR tmp[24];
    int n = 0;
    do {
        tmp[n++] = (WCHAR)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n < width && n < 24) tmp[n++] = '0';
    while (n) sb_addc(b, tmp[--n]);
}

void sb_add_u64(StrBuf *b, uint64_t v) { sb_add_u64_pad(b, v, 0); }

void sb_add_hex(StrBuf *b, uint64_t v, int digits) {
    static const char hx[] = "0123456789abcdef";
    for (int i = digits - 1; i >= 0; i--) sb_addc(b, (WCHAR)hx[(v >> (i * 4)) & 15]);
}

const WCHAR *sb_str(StrBuf *b) {
    if (!b->p) sb_grow(b, 0), b->p[0] = 0;
    return b->p;
}

WCHAR *sb_take(StrBuf *b) {
    sb_str(b);
    WCHAR *r = b->p;
    b->p = NULL;
    b->len = b->cap = 0;
    return r;
}

void sb_free(StrBuf *b) {
    xfree(b->p);
    b->p = NULL;
    b->len = b->cap = 0;
}

char *w_to_utf8(const WCHAR *s, size_t n, size_t *out_len) {
    char *r = xalloc(n * 3 + 1);
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t c = s[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            i++;
        } else if (c >= 0xD800 && c <= 0xDFFF) {
            c = 0xFFFD;  // metade de par solta
        }
        if (c < 0x80) {
            r[o++] = (char)c;
        } else if (c < 0x800) {
            r[o++] = (char)(0xC0 | (c >> 6));
            r[o++] = (char)(0x80 | (c & 63));
        } else if (c < 0x10000) {
            r[o++] = (char)(0xE0 | (c >> 12));
            r[o++] = (char)(0x80 | ((c >> 6) & 63));
            r[o++] = (char)(0x80 | (c & 63));
        } else {
            r[o++] = (char)(0xF0 | (c >> 18));
            r[o++] = (char)(0x80 | ((c >> 12) & 63));
            r[o++] = (char)(0x80 | ((c >> 6) & 63));
            r[o++] = (char)(0x80 | (c & 63));
        }
    }
    r[o] = 0;
    if (out_len) *out_len = o;
    return r;
}

WCHAR *utf8_to_w(const char *s, size_t n, size_t *out_len) {
    const unsigned char *u = (const unsigned char *)s;
    WCHAR *r = xalloc((n + 1) * sizeof(WCHAR));
    size_t o = 0, i = 0;
    while (i < n) {
        uint32_t c = u[i];
        int extra;
        uint32_t min;
        if (c < 0x80) {
            r[o++] = (WCHAR)c;
            i++;
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            extra = 1, min = 0x80, c &= 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2, min = 0x800, c &= 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3, min = 0x10000, c &= 0x07;
        } else {
            r[o++] = 0xFFFD;
            i++;
            continue;
        }
        bool ok = true;
        for (int k = 1; k <= extra; k++) {
            if (i + (size_t)k >= n || (u[i + k] & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            c = (c << 6) | (u[i + k] & 63);
        }
        if (!ok || c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
            r[o++] = 0xFFFD;
            i++;
            continue;
        }
        i += (size_t)extra + 1;
        if (c >= 0x10000) {
            c -= 0x10000;
            r[o++] = (WCHAR)(0xD800 + (c >> 10));
            r[o++] = (WCHAR)(0xDC00 + (c & 0x3FF));
        } else {
            r[o++] = (WCHAR)c;
        }
    }
    r[o] = 0;
    if (out_len) *out_len = o;
    return r;
}
