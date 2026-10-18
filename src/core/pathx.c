// SPDX-License-Identifier: MIT
#include "pathx.h"

static bool is_sep(WCHAR c) { return c == '\\' || c == '/'; }

WCHAR *path_norm(const WCHAR *p) {
    if (!p) return wdup(NULL);
    while (*p && (w_is_space(*p) || *p == '"')) p++;
    size_t n = wlen(p);
    while (n && (w_is_space(p[n - 1]) || p[n - 1] == '"')) n--;
    StrBuf b = {0};
    size_t i = 0;
    // prefixos que começam com duas barras ficam como estão (\\servidor\..., \\?\...)
    if (n >= 2 && is_sep(p[0]) && is_sep(p[1])) {
        sb_add(&b, u"\\\\");
        i = 2;
    }
    for (; i < n; i++) {
        WCHAR c = is_sep(p[i]) ? '\\' : p[i];
        if (c == '\\' && b.len && b.p[b.len - 1] == '\\') continue;
        sb_addc(&b, c);
    }
    // "C:\" mantém a barra; "C:\pasta\" perde
    while (b.len > 1 && b.p[b.len - 1] == '\\' && !(b.len == 3 && b.p[1] == ':') && !(b.len == 2 && b.p[0] == '\\')) b.p[--b.len] = 0;
    return sb_take(&b);
}

WCHAR *path_join(const WCHAR *a, const WCHAR *b) {
    size_t la = wlen(a);
    while (*b && is_sep(*b)) b++;
    if (!la) return wdup(b);
    if (is_sep(a[la - 1])) return wcat(a, b);
    return wcat3(a, u"\\", b);
}

const WCHAR *path_leaf(const WCHAR *p) {
    const WCHAR *r = p;
    for (; *p; p++)
        if (is_sep(*p)) r = p + 1;
    return r;
}

WCHAR *path_parent(const WCHAR *p) {
    const WCHAR *leaf = path_leaf(p);
    if (leaf == p) return NULL;
    size_t n = (size_t)(leaf - p);
    while (n > 1 && is_sep(p[n - 1])) n--;
    if (n == 2 && p[1] == ':') n = 3;  // pai de "C:\x" é "C:\"
    return wdupn(p, n);
}

bool path_eq(const WCHAR *a, const WCHAR *b) { return wieq(a, b); }

bool path_is_under(const WCHAR *child, const WCHAR *parent) {
    size_t lp = wlen(parent);
    if (!lp || wlen(child) <= lp || !wieqn(child, parent, lp)) return false;
    if (is_sep(parent[lp - 1])) return true;  // pai como "C:\"
    return is_sep(child[lp]);
}

bool path_is_under_or_eq(const WCHAR *child, const WCHAR *parent) { return path_eq(child, parent) || path_is_under(child, parent); }

const WCHAR *path_ext(const WCHAR *p) {
    const WCHAR *leaf = path_leaf(p), *dot = wrfindc(leaf, '.');
    return (dot && dot != leaf) ? dot : leaf + wlen(leaf);
}

WCHAR *path_strip_ext(const WCHAR *name) {
    const WCHAR *e = path_ext(name);
    return wdupn(name, (size_t)(e - name));
}
