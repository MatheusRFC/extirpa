// SPDX-License-Identifier: MIT
#include "trace.h"

uint64_t hash_entry(TraceKind kind, const WCHAR *path, const WCHAR *value) {
    // FNV-1a de 64 bits sobre o tipo, o caminho em minúsculas e o nome do valor
    uint64_t h = 1469598103934665603ULL;
#define MIX(c)                                \
    do {                                      \
        h ^= (uint64_t)(c);                   \
        h *= 1099511628211ULL;                \
    } while (0)
    MIX(kind);
    for (const WCHAR *p = path; p && *p; p++) {
        MIX(wfold(*p) & 0xFF);
        MIX(wfold(*p) >> 8);
    }
    if (value) {
        MIX(0x1F);
        for (const WCHAR *p = value; *p; p++) {
            MIX(wfold(*p) & 0xFF);
            MIX(wfold(*p) >> 8);
        }
    }
#undef MIX
    return h ? h : 1;  // 0 marca posição vazia na tabela
}

static void hs_insert(uint64_t *slots, size_t cap, uint64_t h) {
    size_t i = (size_t)(h ^ (h >> 29)) & (cap - 1);
    while (slots[i] && slots[i] != h) i = (i + 1) & (cap - 1);
    slots[i] = h;
}

void hs_add(HashSet *s, uint64_t h) {
    if (!h) h = 1;
    if ((s->n + 1) * 2 > s->cap) {  // mantém no máximo metade ocupada
        size_t cap = s->cap ? s->cap * 2 : 1024;
        uint64_t *slots = xalloc(cap * sizeof(uint64_t));
        for (size_t i = 0; i < s->cap; i++)
            if (s->slots[i]) hs_insert(slots, cap, s->slots[i]);
        xfree(s->slots);
        s->slots = slots;
        s->cap = cap;
    }
    if (!hs_has(s, h)) {
        hs_insert(s->slots, s->cap, h);
        s->n++;
    }
}

bool hs_has(const HashSet *s, uint64_t h) {
    if (!s->cap) return false;
    if (!h) h = 1;
    size_t i = (size_t)(h ^ (h >> 29)) & (s->cap - 1);
    while (s->slots[i]) {
        if (s->slots[i] == h) return true;
        i = (i + 1) & (s->cap - 1);
    }
    return false;
}

void hs_free(HashSet *s) {
    xfree(s->slots);
    s->slots = NULL;
    s->cap = s->n = 0;
}

void snap_record(Snapshot *s, TraceKind kind, const WCHAR *path, const WCHAR *value) { hs_add(&s->before, hash_entry(kind, path, value)); }

bool snap_check(Snapshot *s, TraceKind kind, const WCHAR *path, const WCHAR *value) {
    if (hs_has(&s->before, hash_entry(kind, path, value))) return true;
    if (s->out) trace_add(s->out, kind, path, value);
    s->added++;
    return false;
}

Trace *trace_new(void) { return xalloc(sizeof(Trace)); }

void trace_add(Trace *t, TraceKind kind, const WCHAR *path, const WCHAR *value) {
    TraceItem *it = xalloc(sizeof(TraceItem));
    it->kind = kind;
    it->path = wdup(path);
    it->value = kind == TR_VALUE ? wdup(value) : NULL;
    pv_push(&t->items, it);
}

void trace_free(Trace *t) {
    if (!t) return;
    for (size_t i = 0; i < t->items.n; i++) {
        TraceItem *it = t->items.items[i];
        xfree(it->path), xfree(it->value), xfree(it);
    }
    pv_free(&t->items);
    xfree(t->name), xfree(t->installer), xfree(t->date), xfree(t);
}

// Campos com tabulação, quebra de linha ou '%' são escritos como %09, %0A, %0D, %25.
static void add_field(StrBuf *b, const WCHAR *s) {
    for (; s && *s; s++) {
        if (*s == '\t') sb_add(b, u"%09");
        else if (*s == '\n') sb_add(b, u"%0A");
        else if (*s == '\r') sb_add(b, u"%0D");
        else if (*s == '%') sb_add(b, u"%25");
        else sb_addc(b, *s);
    }
}

char *trace_serialize(const Trace *t, size_t *len) {
    StrBuf b = {0};
    sb_add(&b, u"EXTIRPA-TRACE 1\n");
    sb_add(&b, u"name\t"), add_field(&b, t->name), sb_addc(&b, '\n');
    sb_add(&b, u"installer\t"), add_field(&b, t->installer), sb_addc(&b, '\n');
    sb_add(&b, u"date\t"), add_field(&b, t->date), sb_addc(&b, '\n');
    for (size_t i = 0; i < t->items.n; i++) {
        const TraceItem *it = t->items.items[i];
        sb_addc(&b, (WCHAR)it->kind);
        sb_addc(&b, '\t');
        add_field(&b, it->path);
        if (it->kind == TR_VALUE) {
            sb_addc(&b, '\t');
            add_field(&b, it->value);
        }
        sb_addc(&b, '\n');
    }
    char *r = w_to_utf8(sb_str(&b), b.len, len);
    sb_free(&b);
    return r;
}

static int hexval(WCHAR c) {
    if (c >= '0' && c <= '9') return c - '0';
    if ((c | 32) >= 'a' && (c | 32) <= 'f') return (c | 32) - 'a' + 10;
    return -1;
}

static WCHAR *unescape(const WCHAR *s, size_t n) {
    StrBuf b = {0};
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '%' && i + 2 < n && hexval(s[i + 1]) >= 0 && hexval(s[i + 2]) >= 0) {
            sb_addc(&b, (WCHAR)(hexval(s[i + 1]) * 16 + hexval(s[i + 2])));
            i += 2;
        } else {
            sb_addc(&b, s[i]);
        }
    }
    return sb_take(&b);
}

Trace *trace_parse(const char *data, size_t len) {
    size_t wn;
    WCHAR *w = utf8_to_w(data, len, &wn);
    const WCHAR *p = w, *end = w + wn;
    if (wn && p[0] == 0xFEFF) p++;  // BOM, se alguém salvou no Bloco de Notas
    Trace *t = NULL;
    bool first = true;
    while (p < end) {
        const WCHAR *eol = p;
        while (eol < end && *eol != '\n') eol++;
        size_t n = (size_t)(eol - p);
        if (n && p[n - 1] == '\r') n--;
        if (first) {
            if (n < 15 || !wieqn(p, u"EXTIRPA-TRACE ", 14) || p[14] != '1') break;
            t = trace_new();
            first = false;
        } else if (n) {
            // campos separados por tabulação
            const WCHAR *f[3] = {p, NULL, NULL};
            size_t fl[3] = {n, 0, 0};
            int nf = 1;
            for (size_t i = 0; i < n && nf < 3; i++)
                if (p[i] == '\t') {
                    fl[nf - 1] = (size_t)(p + i - f[nf - 1]);
                    f[nf] = p + i + 1;
                    fl[nf] = (size_t)(p + n - f[nf]);
                    nf++;
                }
            if (nf >= 2) {
                WCHAR *k = wdupn(f[0], fl[0]);
                if (wieq(k, u"name")) xfree(t->name), t->name = unescape(f[1], fl[1]);
                else if (wieq(k, u"installer")) xfree(t->installer), t->installer = unescape(f[1], fl[1]);
                else if (wieq(k, u"date")) xfree(t->date), t->date = unescape(f[1], fl[1]);
                else if (fl[0] == 1 && (k[0] == TR_KEY || k[0] == TR_DIR || k[0] == TR_FILE || (k[0] == TR_VALUE && nf == 3))) {
                    WCHAR *path = unescape(f[1], fl[1]), *value = nf == 3 ? unescape(f[2], fl[2]) : NULL;
                    if (path[0]) trace_add(t, (TraceKind)k[0], path, value ? value : u"");
                    xfree(path), xfree(value);
                }
                xfree(k);
            }
        }
        p = eol + 1;
    }
    xfree(w);
    if (t) {
        if (!t->name) t->name = wdup(u"");
        if (!t->installer) t->installer = wdup(u"");
        if (!t->date) t->date = wdup(u"");
    }
    return t;
}
