// SPDX-License-Identifier: MIT
#include "leftover.h"

#include "pathx.h"
#include "protect.h"

bool left_is_registry(LeftKind k) { return k == LK_KEY || k == LK_VALUE || k == LK_KEY_IF_EMPTY; }
bool left_is_file(LeftKind k) { return k == LK_DIR || k == LK_FILE || k == LK_DIR_IF_EMPTY; }

static bool blocked(LeftKind kind, const WCHAR *path) {
    switch (kind) {
    case LK_KEY:
    case LK_KEY_IF_EMPTY: return protect_key_blocked(path);
    case LK_VALUE: return protect_value_blocked(path);
    case LK_DIR:
    case LK_FILE:
    case LK_DIR_IF_EMPTY: return protect_dir_blocked(path);
    case LK_SERVICE: return !path[0] || wfindc(path, '\\') != NULL;
    }
    return true;
}

Leftover *left_add(LeftList *l, LeftKind kind, const WCHAR *path, const WCHAR *value, int conf, LeftReason reason, const WCHAR *detail) {
    WCHAR *p = kind == LK_SERVICE ? wtrim_dup(path) : path_norm(path);
    if (blocked(kind, p)) {
        xfree(p);
        return NULL;
    }
    for (size_t i = 0; i < l->items.n; i++) {
        Leftover *o = l->items.items[i];
        bool same_kind = o->kind == kind || (left_is_registry(o->kind) && left_is_registry(kind) && o->kind != LK_VALUE && kind != LK_VALUE) ||
                         (left_is_file(o->kind) && left_is_file(kind) && o->kind != LK_FILE && kind != LK_FILE);
        if (same_kind && wieq(o->path, p) && (kind != LK_VALUE || wieq(o->value, value ? value : u""))) {
            // o mesmo item achado por dois caminhos: fica a versão mais forte
            if (conf > o->conf) o->conf = conf, o->reason = reason;
            if ((o->kind == LK_KEY_IF_EMPTY && kind == LK_KEY) || (o->kind == LK_DIR_IF_EMPTY && kind == LK_DIR)) o->kind = kind;
            xfree(p);
            return o;
        }
    }
    Leftover *it = xalloc(sizeof(Leftover));
    it->kind = kind;
    it->path = p;
    it->value = kind == LK_VALUE ? wdup(value) : NULL;
    it->conf = conf;
    it->reason = reason;
    it->detail = detail ? wdup(detail) : NULL;
    pv_push(&l->items, it);
    return it;
}

// a cobre b? (a é uma chave/pasta inteira que contém b)
static bool covers(const Leftover *a, const Leftover *b) {
    if (a == b || a->kind == LK_KEY_IF_EMPTY || a->kind == LK_DIR_IF_EMPTY) return false;
    if (a->kind == LK_KEY && left_is_registry(b->kind)) return path_is_under(b->path, a->path) || (b->kind == LK_VALUE && path_eq(b->path, a->path));
    if (a->kind == LK_DIR && left_is_file(b->kind)) return path_is_under(b->path, a->path);
    return false;
}

static int order_kind(LeftKind k) {
    switch (k) {
    case LK_SERVICE: return 0;
    case LK_KEY: return 1;
    case LK_VALUE: return 1;
    case LK_KEY_IF_EMPTY: return 2;
    case LK_DIR: return 3;
    case LK_FILE: return 3;
    case LK_DIR_IF_EMPTY: return 4;
    }
    return 5;
}

static int cmp_left(const void *pa, const void *pb, void *ctx) {
    (void)ctx;
    const Leftover *a = pa, *b = pb;
    int ka = order_kind(a->kind), kb = order_kind(b->kind);
    if (ka != kb) return ka - kb;
    int c = wicmp(a->path, b->path);
    if (c) return c;
    return wicmp(a->value ? a->value : u"", b->value ? b->value : u"");
}

void left_finalize(LeftList *l) {
    bool *covered = xalloc(l->items.n + 1);
    for (size_t i = 0; i < l->items.n; i++)
        for (size_t j = 0; j < l->items.n && !covered[i]; j++) {
            const Leftover *a = l->items.items[j], *b = l->items.items[i];
            if (covers(a, b) && a->conf >= b->conf) covered[i] = true;
        }
    PtrVec keep = {0};
    for (size_t i = 0; i < l->items.n; i++) {
        Leftover *b = l->items.items[i];
        if (covered[i]) {
            xfree(b->path), xfree(b->value), xfree(b->detail), xfree(b);
        } else {
            b->checked = b->conf >= CONF_HIGH || b->kind == LK_KEY_IF_EMPTY || b->kind == LK_DIR_IF_EMPTY;
            pv_push(&keep, b);
        }
    }
    xfree(covered);
    pv_free(&l->items);
    l->items = keep;
    pv_sort(&l->items, cmp_left, NULL);
}

void left_free(LeftList *l) {
    for (size_t i = 0; i < l->items.n; i++) {
        Leftover *it = l->items.items[i];
        xfree(it->path), xfree(it->value), xfree(it->detail), xfree(it);
    }
    pv_free(&l->items);
}

// Lugares onde o Windows escreve sozinho o tempo todo: o que aparecer ali durante o
// rastreamento pode não ser da instalação, então entra desmarcado.
static const WCHAR *const NOISY_FILES[] = {
    u"\\AppData\\Local\\Microsoft\\", u"\\AppData\\Roaming\\Microsoft\\", u"\\ProgramData\\Microsoft\\", u"\\AppData\\Local\\Temp\\",
    u"\\AppData\\Local\\D3DSCache\\", u"\\AppData\\Local\\CrashDumps\\", u"\\AppData\\Local\\Packages\\", u"\\Windows\\",
};

static bool reg_sensitive(const WCHAR *key) {
    // tudo dentro de ...\Microsoft, menos os pontos onde instaladores se registram de propósito
    static const WCHAR *const OK[] = {
        u"\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\",
        u"\\Microsoft\\Windows\\CurrentVersion\\App Paths\\",
    };
    for (size_t i = 0; i < COUNT(OK); i++)
        if (wifind(key, OK[i])) return false;
    return wifind(key, u"\\Microsoft\\") || wiends(key, u"\\Microsoft");
}

static bool reg_value_ok(const WCHAR *key) {
    return wiends(key, u"\\Microsoft\\Windows\\CurrentVersion\\Run") || wiends(key, u"\\Microsoft\\Windows\\CurrentVersion\\RunOnce") ||
           wiends(key, u"\\Microsoft\\Windows\\CurrentVersion\\SharedDLLs");
}

void left_add_trace(LeftList *l, const Trace *t) {
    static const WCHAR SERVICES[] = u"HKLM\\SYSTEM\\CurrentControlSet\\Services\\";
    for (size_t i = 0; i < t->items.n; i++) {
        const TraceItem *it = t->items.items[i];
        int conf = CONF_HIGH;
        switch (it->kind) {
        case TR_KEY:
            if (wistarts(it->path, SERVICES) && !wfindc(it->path + wlen(SERVICES), '\\')) {
                left_add(l, LK_SERVICE, it->path + wlen(SERVICES), NULL, CONF_HIGH, LR_SERVICE, NULL);
                continue;
            }
            if (reg_sensitive(it->path)) conf = CONF_LOW;
            left_add(l, LK_KEY, it->path, NULL, conf, LR_TRACE, NULL);
            break;
        case TR_VALUE:
            if (reg_sensitive(it->path) && !reg_value_ok(it->path)) conf = CONF_LOW;
            left_add(l, LK_VALUE, it->path, it->value, conf, LR_TRACE, NULL);
            break;
        case TR_DIR:
        case TR_FILE:
            for (size_t k = 0; k < COUNT(NOISY_FILES); k++)
                if (wifind(it->path, NOISY_FILES[k])) conf = CONF_LOW;
            left_add(l, it->kind == TR_DIR ? LK_DIR : LK_FILE, it->path, NULL, conf, LR_TRACE, NULL);
            break;
        }
    }
}
