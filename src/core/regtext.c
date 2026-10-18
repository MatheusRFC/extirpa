// SPDX-License-Identifier: MIT
#include "regtext.h"

static const struct {
    const WCHAR *s, *l;
} ROOTS[] = {
    {u"HKLM", u"HKEY_LOCAL_MACHINE"}, {u"HKCU", u"HKEY_CURRENT_USER"}, {u"HKCR", u"HKEY_CLASSES_ROOT"}, {u"HKU", u"HKEY_USERS"}, {u"HKCC", u"HKEY_CURRENT_CONFIG"},
};

static WCHAR *swap_root(const WCHAR *key, bool to_long) {
    for (size_t i = 0; i < COUNT(ROOTS); i++) {
        const WCHAR *from = to_long ? ROOTS[i].s : ROOTS[i].l, *to = to_long ? ROOTS[i].l : ROOTS[i].s;
        size_t n = wlen(from);
        if (wieqn(key, from, n) && (key[n] == 0 || key[n] == '\\')) return wcat(to, key + n);
    }
    return wdup(key);
}

WCHAR *reg_long_name(const WCHAR *key) { return swap_root(key, true); }
WCHAR *reg_short_name(const WCHAR *key) { return swap_root(key, false); }

void regtext_header(StrBuf *b) { sb_add(b, u"Windows Registry Editor Version 5.00\r\n"); }

void regtext_comment(StrBuf *b, const WCHAR *text) {
    sb_add(b, u"\r\n; ");
    for (; *text; text++) sb_addc(b, (*text == '\r' || *text == '\n') ? ' ' : *text);
    sb_add(b, u"\r\n");
}

void regtext_key(StrBuf *b, const WCHAR *key) {
    WCHAR *l = reg_long_name(key);
    sb_add(b, u"\r\n[");
    sb_add(b, l);
    sb_add(b, u"]\r\n");
    xfree(l);
}

static void add_quoted(StrBuf *b, const WCHAR *s, size_t n) {
    sb_addc(b, '"');
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\\' || s[i] == '"') sb_addc(b, '\\');
        sb_addc(b, s[i]);
    }
    sb_addc(b, '"');
}

// Bytes em hexadecimal, quebrando a linha como o Editor do Registro faz (no máximo ~80 colunas).
static void add_hex(StrBuf *b, const uint8_t *data, uint32_t size, size_t line_start) {
    size_t col = b->len - line_start;
    for (uint32_t i = 0; i < size; i++) {
        if (col + 3 > 79) {
            sb_add(b, u"\\\r\n  ");
            col = 2;
        }
        sb_add_hex(b, data[i], 2);
        col += 2;
        if (i + 1 < size) {
            sb_addc(b, ',');
            col++;
        }
    }
}

void regtext_value(StrBuf *b, const WCHAR *name, uint32_t type, const uint8_t *data, uint32_t size) {
    size_t line_start = b->len;
    if (!name || !*name) sb_addc(b, '@');
    else add_quoted(b, name, wlen(name));
    sb_addc(b, '=');

    if (type == RT_SZ && size % 2 == 0) {
        size_t n = size / 2;
        WCHAR *tmp = xalloc((n + 1) * sizeof(WCHAR));
        if (n) mem_copy(tmp, data, n * sizeof(WCHAR));
        if (n && tmp[n - 1] == 0) n--;  // o terminador do Registro
        bool plain = true;
        for (size_t i = 0; i < n; i++)
            if (tmp[i] < 0x20) plain = false;  // zero no meio, quebra de linha...
        if (plain) {
            add_quoted(b, tmp, n);
            sb_add(b, u"\r\n");
            xfree(tmp);
            return;
        }
        xfree(tmp);
    }
    if (type == RT_DWORD && size == 4) {
        uint32_t v = (uint32_t)data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
        sb_add(b, u"dword:");
        sb_add_hex(b, v, 8);
        sb_add(b, u"\r\n");
        return;
    }
    if (type == RT_BINARY) {
        sb_add(b, u"hex:");
    } else {
        sb_add(b, u"hex(");
        int digits = 1;
        while (digits < 8 && (type >> (digits * 4))) digits++;
        sb_add_hex(b, type, digits);
        sb_add(b, u"):");
    }
    add_hex(b, data, size, line_start);
    sb_add(b, u"\r\n");
}
