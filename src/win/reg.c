// SPDX-License-Identifier: MIT
#include "reg.h"

#include "../core/pathx.h"
#include "../core/regtext.h"

static const struct {
    const WCHAR *s, *l;
    HKEY key;
} ROOTS[] = {
    {u"HKLM", u"HKEY_LOCAL_MACHINE", HKEY_LOCAL_MACHINE},
    {u"HKCU", u"HKEY_CURRENT_USER", HKEY_CURRENT_USER},
    {u"HKCR", u"HKEY_CLASSES_ROOT", HKEY_CLASSES_ROOT},
    {u"HKU", u"HKEY_USERS", HKEY_USERS},
};

// Raiz e resto do caminho ("HKLM\SOFTWARE\X" -> HKEY_LOCAL_MACHINE, "SOFTWARE\X").
static HKEY split_root(const WCHAR *path, const WCHAR **sub) {
    for (size_t i = 0; i < COUNT(ROOTS); i++) {
        const WCHAR *names[2] = {ROOTS[i].s, ROOTS[i].l};
        for (int k = 0; k < 2; k++) {
            size_t n = wlen(names[k]);
            if (wieqn(path, names[k], n) && (path[n] == 0 || path[n] == '\\')) {
                *sub = path[n] ? path + n + 1 : path + n;
                return ROOTS[i].key;
            }
        }
    }
    return NULL;
}

HKEY reg_open(const WCHAR *path, REGSAM access) {
    const WCHAR *sub;
    HKEY root = split_root(path, &sub), k = NULL;
    if (!root) return NULL;
    if (RegOpenKeyExW(root, sub, 0, access | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return NULL;
    return k;
}

HKEY reg_open_sub(HKEY parent, const WCHAR *sub) {
    HKEY k = NULL;
    if (RegOpenKeyExW(parent, sub, 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return NULL;
    return k;
}

bool reg_exists(const WCHAR *path) {
    HKEY k = reg_open(path, KEY_QUERY_VALUE);
    if (!k) return false;
    RegCloseKey(k);
    return true;
}

static uint8_t *query(HKEY k, const WCHAR *name, DWORD *type, DWORD *size) {
    DWORD sz = 0, t = 0;
    if (RegQueryValueExW(k, name, NULL, &t, NULL, &sz) != ERROR_SUCCESS) return NULL;
    for (int tries = 0; tries < 4; tries++) {
        uint8_t *buf = xalloc(sz + 4);  // +4: garante o terminador de strings mal gravadas
        DWORD got = sz;
        LSTATUS st = RegQueryValueExW(k, name, NULL, &t, buf, &got);
        if (st == ERROR_SUCCESS) {
            *type = t;
            *size = got;
            return buf;
        }
        xfree(buf);
        if (st != ERROR_MORE_DATA) return NULL;
        sz = got > sz ? got : sz * 2 + 64;  // o valor mudou entre as duas leituras
    }
    return NULL;
}

WCHAR *reg_get_str(HKEY k, const WCHAR *name) {
    DWORD type, size;
    uint8_t *data = query(k, name, &type, &size);
    if (!data) return NULL;
    if (type != REG_SZ && type != REG_EXPAND_SZ) {
        xfree(data);
        return NULL;
    }
    WCHAR *s = wdupn((const WCHAR *)data, size / 2);
    xfree(data);
    // tira o terminador (e lixo depois dele, se o valor foi gravado errado)
    size_t n = wlen(s);
    s[n] = 0;
    return s;
}

bool reg_get_dword(HKEY k, const WCHAR *name, DWORD *out) {
    DWORD type, size;
    uint8_t *data = query(k, name, &type, &size);
    if (!data) return false;
    bool ok = type == REG_DWORD && size == 4;
    if (ok) *out = (DWORD)data[0] | (DWORD)data[1] << 8 | (DWORD)data[2] << 16 | (DWORD)data[3] << 24;
    xfree(data);
    return ok;
}

uint64_t reg_last_write(HKEY k) {
    FILETIME ft = {0, 0};
    if (RegQueryInfoKeyW(k, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ft) != ERROR_SUCCESS) return 0;
    return ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

PtrVec reg_subkeys(HKEY k) {
    PtrVec v = {0};
    WCHAR name[260];
    for (DWORD i = 0;; i++) {
        DWORD n = COUNT(name);
        LSTATUS st = RegEnumKeyExW(k, i, name, &n, NULL, NULL, NULL, NULL);
        if (st == ERROR_NO_MORE_ITEMS) break;
        if (st != ERROR_SUCCESS) {
            if (st == ERROR_MORE_DATA) continue;  // nome maior que o limite do Registro: ignora
            break;
        }
        pv_push(&v, wdupn(name, n));
    }
    return v;
}

PtrVec reg_values(HKEY k) {
    PtrVec v = {0};
    DWORD max_name = 0, max_data = 0;
    if (RegQueryInfoKeyW(k, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &max_name, &max_data, NULL, NULL) != ERROR_SUCCESS) return v;
    DWORD name_cap = max_name + 2, data_cap = max_data + 4;
    WCHAR *name = xalloc(name_cap * sizeof(WCHAR));
    uint8_t *data = xalloc(data_cap);
    for (DWORD i = 0;;) {
        DWORD nn = name_cap, dn = data_cap, type = 0;
        LSTATUS st = RegEnumValueW(k, i, name, &nn, NULL, &type, data, &dn);
        if (st == ERROR_NO_MORE_ITEMS) break;
        if (st == ERROR_MORE_DATA) {  // cresceu desde a consulta: aumenta e tenta de novo
            name_cap = name_cap * 2 + 64;
            data_cap = (dn > data_cap ? dn : data_cap * 2) + 64;
            name = xrealloc(name, name_cap * sizeof(WCHAR));
            data = xrealloc(data, data_cap);
            if (name_cap > 65536) break;
            continue;
        }
        if (st != ERROR_SUCCESS) break;
        RegValue *rv = xalloc(sizeof(RegValue));
        rv->name = wdupn(name, nn);
        rv->type = type;
        rv->size = dn;
        rv->data = xalloc(dn + 4);
        mem_copy(rv->data, data, dn);
        pv_push(&v, rv);
        i++;
    }
    xfree(name), xfree(data);
    return v;
}

PtrVec reg_value_names(HKEY k) {
    PtrVec v = {0};
    DWORD max_name = 0;
    if (RegQueryInfoKeyW(k, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &max_name, NULL, NULL, NULL) != ERROR_SUCCESS) return v;
    DWORD cap = max_name + 2;
    WCHAR *name = xalloc(cap * sizeof(WCHAR));
    for (DWORD i = 0;;) {
        DWORD nn = cap;
        LSTATUS st = RegEnumValueW(k, i, name, &nn, NULL, NULL, NULL, NULL);
        if (st == ERROR_NO_MORE_ITEMS) break;
        if (st == ERROR_MORE_DATA) {
            if (cap > 65536) break;
            cap = cap * 2 + 64;
            name = xrealloc(name, cap * sizeof(WCHAR));
            continue;
        }
        if (st != ERROR_SUCCESS) break;
        pv_push(&v, wdupn(name, nn));
        i++;
    }
    xfree(name);
    return v;
}

void reg_values_free(PtrVec *v) {
    for (size_t i = 0; i < v->n; i++) {
        RegValue *rv = v->items[i];
        xfree(rv->name), xfree(rv->data), xfree(rv);
    }
    pv_free(v);
}

void strings_free(PtrVec *v) {
    for (size_t i = 0; i < v->n; i++) xfree(v->items[i]);
    pv_free(v);
}

bool reg_key_empty(const WCHAR *path) {
    HKEY k = reg_open(path, KEY_READ);
    if (!k) return false;
    DWORD subkeys = 1, values = 1;
    LSTATUS st = RegQueryInfoKeyW(k, NULL, NULL, NULL, &subkeys, NULL, NULL, &values, NULL, NULL, NULL, NULL);
    RegCloseKey(k);
    return st == ERROR_SUCCESS && subkeys == 0 && values == 0;
}

static bool export_rec(StrBuf *out, const WCHAR *path, HKEY k, int depth) {
    regtext_key(out, path);
    PtrVec vals = reg_values(k);
    for (size_t i = 0; i < vals.n; i++) {
        RegValue *rv = vals.items[i];
        regtext_value(out, rv->name, rv->type, rv->data, rv->size);
    }
    reg_values_free(&vals);
    if (depth > 64) return false;
    bool ok = true;
    PtrVec subs = reg_subkeys(k);
    for (size_t i = 0; i < subs.n; i++) {
        HKEY s = reg_open_sub(k, subs.items[i]);
        if (!s) {
            ok = false;
            continue;
        }
        WCHAR *p = path_join(path, subs.items[i]);
        ok = export_rec(out, p, s, depth + 1) && ok;
        xfree(p);
        RegCloseKey(s);
    }
    strings_free(&subs);
    return ok;
}

bool reg_export_key(StrBuf *out, const WCHAR *path) {
    HKEY k = reg_open(path, KEY_READ);
    if (!k) return false;
    bool ok = export_rec(out, path, k, 0);
    RegCloseKey(k);
    return ok;
}

bool reg_export_value(StrBuf *out, const WCHAR *path, const WCHAR *value) {
    HKEY k = reg_open(path, KEY_READ);
    if (!k) return false;
    DWORD type, size;
    uint8_t *data = query(k, value, &type, &size);
    RegCloseKey(k);
    if (!data) return false;
    regtext_key(out, path);
    regtext_value(out, value, type, data, size);
    xfree(data);
    return true;
}

DWORD reg_delete_key(const WCHAR *path) {
    WCHAR *parent = path_parent(path);
    if (!parent) return ERROR_ACCESS_DENIED;  // uma raiz
    HKEY p = reg_open(parent, KEY_ALL_ACCESS);
    xfree(parent);
    if (!p) return reg_exists(path) ? ERROR_ACCESS_DENIED : ERROR_SUCCESS;
    const WCHAR *leaf = path_leaf(path);
    LSTATUS st = RegDeleteTreeW(p, leaf);
    LSTATUS st2 = RegDeleteKeyExW(p, leaf, KEY_WOW64_64KEY, 0);  // a própria chave, se ainda existir
    RegCloseKey(p);
    if (!reg_exists(path)) return ERROR_SUCCESS;
    return st != ERROR_SUCCESS ? (DWORD)st : (DWORD)st2;
}

DWORD reg_delete_value(const WCHAR *path, const WCHAR *value) {
    HKEY k = reg_open(path, KEY_SET_VALUE | KEY_QUERY_VALUE);
    if (!k) return reg_exists(path) ? ERROR_ACCESS_DENIED : ERROR_SUCCESS;
    LSTATUS st = RegDeleteValueW(k, value ? value : u"");
    RegCloseKey(k);
    return (st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND) ? ERROR_SUCCESS : (DWORD)st;
}

DWORD reg_set_binary(const WCHAR *path, const WCHAR *value, const uint8_t *data, DWORD size) {
    const WCHAR *sub;
    HKEY root = split_root(path, &sub), k = NULL;
    if (!root) return ERROR_PATH_NOT_FOUND;
    LSTATUS st = RegCreateKeyExW(root, sub, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &k, NULL);
    if (st != ERROR_SUCCESS) return (DWORD)st;
    st = RegSetValueExW(k, value, 0, REG_BINARY, data, size);
    RegCloseKey(k);
    return (DWORD)st;
}
