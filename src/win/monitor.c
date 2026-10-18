// SPDX-License-Identifier: MIT
#include "monitor.h"

#include "../core/pathx.h"
#include "fsx.h"
#include "i18n.h"
#include "platform.h"
#include "reg.h"

typedef struct {
    Snapshot *s;
    bool second;
    Ctl *ctl;
    uint64_t count;
    PtrVec skip_dirs;  // WCHAR*: pastas que mudam sozinhas o tempo todo
} Walk;

// Chaves fora da foto: o banco de dados do Windows Installer e outras áreas que o EXTIRPA
// nunca apagaria (e que só deixariam a foto mais lenta).
static const WCHAR *const SKIP_KEYS[] = {
    u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Installer",
    u"HKLM\\SOFTWARE\\Classes\\Installer",
    u"HKCU\\Software\\Microsoft\\Installer",
    u"HKCU\\Software\\Classes\\Local Settings",
    u"HKLM\\SOFTWARE\\Microsoft\\Windows Defender",
    u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing",
    u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate",
    u"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Prefetcher",
};

static bool skip_key(const WCHAR *path) {
    for (size_t i = 0; i < COUNT(SKIP_KEYS); i++)
        if (path_eq(path, SKIP_KEYS[i])) return true;
    return false;
}

static void tick(Walk *w, StrId what) {
    if (++w->count % 4096) return;
    WCHAR *t = wfmt(T(what), w->count);
    ctl_progress(w->ctl, t);
    xfree(t);
}

static void walk_key(Walk *w, const WCHAR *path, HKEY k, int depth) {
    if (ctl_cancelled(w->ctl)) return;
    PtrVec names = reg_value_names(k);
    for (size_t i = 0; i < names.n; i++) {
        if (w->second) snap_check(w->s, TR_VALUE, path, names.items[i]);
        else snap_record(w->s, TR_VALUE, path, names.items[i]);
    }
    strings_free(&names);
    if (depth > 48) return;
    PtrVec subs = reg_subkeys(k);
    for (size_t i = 0; i < subs.n && !ctl_cancelled(w->ctl); i++) {
        WCHAR *child = path_join(path, subs.items[i]);
        tick(w, S_TRACE_READING_REG);
        if (!skip_key(child)) {
            bool descend = true;
            if (w->second) descend = snap_check(w->s, TR_KEY, child, NULL);  // nova: entra inteira, sem descer
            else snap_record(w->s, TR_KEY, child, NULL);
            if (descend) {
                HKEY ck = reg_open_sub(k, subs.items[i]);
                if (ck) {
                    walk_key(w, child, ck, depth + 1);
                    RegCloseKey(ck);
                }
            }
        }
        xfree(child);
    }
    strings_free(&subs);
}

static void walk_services(Walk *w) {
    // serviços: só as chaves de primeiro nível (um serviço novo)
    static const WCHAR SERVICES[] = u"HKLM\\SYSTEM\\CurrentControlSet\\Services";
    HKEY k = reg_open(SERVICES, KEY_READ);
    if (!k) return;
    PtrVec subs = reg_subkeys(k);
    for (size_t i = 0; i < subs.n; i++) {
        WCHAR *child = path_join(SERVICES, subs.items[i]);
        if (w->second) snap_check(w->s, TR_KEY, child, NULL);
        else snap_record(w->s, TR_KEY, child, NULL);
        xfree(child);
    }
    strings_free(&subs);
    RegCloseKey(k);
}

static bool skip_dir(Walk *w, const WCHAR *path) {
    for (size_t i = 0; i < w->skip_dirs.n; i++)
        if (path_eq(path, w->skip_dirs.items[i])) return true;
    return false;
}

static void walk_dir(Walk *w, const WCHAR *dir, int depth) {
    if (ctl_cancelled(w->ctl) || depth > 32) return;
    PtrVec v = fs_list(dir);
    for (size_t i = 0; i < v.n && !ctl_cancelled(w->ctl); i++) {
        DirEntry *e = v.items[i];
        WCHAR *path = path_join(dir, e->name);
        tick(w, S_TRACE_READING_FILES);
        if (e->is_dir) {
            if (!skip_dir(w, path)) {
                bool descend = true;
                if (w->second) descend = snap_check(w->s, TR_DIR, path, NULL);
                else snap_record(w->s, TR_DIR, path, NULL);
                if (descend && !e->is_link) walk_dir(w, path, depth + 1);
            }
        } else {
            if (w->second) snap_check(w->s, TR_FILE, path, NULL);
            else snap_record(w->s, TR_FILE, path, NULL);
        }
        xfree(path);
    }
    fs_list_free(&v);
}

static void add_skip(Walk *w, const WCHAR *base, const WCHAR *sub) {
    if (!base) return;
    pv_push(&w->skip_dirs, sub ? path_join(base, sub) : path_norm(base));
}

void monitor_snapshot(Snapshot *s, bool second, Ctl *ctl) {
    Walk w = {s, second, ctl, 0, {0}};
    const Paths *p = &g_paths;
    add_skip(&w, p->temp, NULL);
    add_skip(&w, p->data_dir, NULL);
    add_skip(&w, p->local_appdata, u"Packages");
    add_skip(&w, p->local_appdata, u"Microsoft\\Windows\\INetCache");
    add_skip(&w, p->local_appdata, u"Microsoft\\Windows\\Explorer");
    add_skip(&w, p->local_appdata, u"Microsoft\\Edge\\User Data");
    add_skip(&w, p->local_appdata, u"Google\\Chrome\\User Data");
    add_skip(&w, p->local_appdata, u"BraveSoftware\\Brave-Browser\\User Data");
    add_skip(&w, p->local_appdata, u"Mozilla\\Firefox\\Profiles");
    add_skip(&w, p->appdata, u"Mozilla\\Firefox\\Profiles");
    add_skip(&w, p->local_appdata, u"NVIDIA\\DXCache");
    add_skip(&w, p->local_appdata, u"NVIDIA\\GLCache");
    add_skip(&w, p->local_appdata, u"D3DSCache");
    add_skip(&w, p->local_appdata, u"CrashDumps");
    add_skip(&w, p->program_data, u"Microsoft\\Windows Defender");
    add_skip(&w, p->program_data, u"Microsoft\\Windows\\WER");
    add_skip(&w, p->program_files, u"WindowsApps");

    static const WCHAR *const KEYS[] = {u"HKLM\\SOFTWARE", u"HKCU\\Software"};
    for (size_t i = 0; i < COUNT(KEYS) && !ctl_cancelled(ctl); i++) {
        HKEY k = reg_open(KEYS[i], KEY_READ);
        if (!k) continue;
        walk_key(&w, KEYS[i], k, 0);
        RegCloseKey(k);
    }
    walk_services(&w);

    const WCHAR *dirs[] = {p->program_files, p->program_files_x86, p->program_data, p->appdata, p->local_appdata, p->locallow, p->desktop, p->public_desktop};
    for (size_t i = 0; i < COUNT(dirs) && !ctl_cancelled(ctl); i++) {
        if (!dirs[i]) continue;
        bool dup = false;  // a Área de Trabalho pode estar dentro de outra pasta da lista
        for (size_t j = 0; j < i; j++)
            if (dirs[j] && path_is_under_or_eq(dirs[i], dirs[j])) dup = true;
        if (!dup) walk_dir(&w, dirs[i], 0);
    }
    strings_free(&w.skip_dirs);
    log_fmt(u"Foto do sistema (%s): %U entradas lidas", second ? u"depois" : u"antes", w.count);
}

WCHAR *trace_guess_name(const Trace *t) {
    for (size_t i = 0; i < t->items.n; i++) {
        const TraceItem *it = t->items.items[i];
        if (it->kind != TR_KEY || !wifind(it->path, u"\\CurrentVersion\\Uninstall\\")) continue;
        HKEY k = reg_open(it->path, KEY_READ);
        if (!k) continue;
        WCHAR *name = reg_get_str(k, u"DisplayName");
        RegCloseKey(k);
        if (name && name[0]) return name;
        xfree(name);
    }
    return NULL;
}

// ---------------------------------------------------------------- arquivos .xtr

Trace *trace_load(const WCHAR *path) {
    uint8_t *data;
    size_t len;
    if (!file_read_all(path, &data, &len)) return NULL;
    Trace *t = trace_parse((const char *)data, len);
    xfree(data);
    return t;
}

PtrVec traces_list(void) {
    PtrVec out = {0};
    PtrVec v = fs_list(g_paths.traces_dir);
    for (size_t i = 0; i < v.n; i++) {
        DirEntry *e = v.items[i];
        if (e->is_dir || !wiends(e->name, u".xtr")) continue;
        WCHAR *path = path_join(g_paths.traces_dir, e->name);
        Trace *t = trace_load(path);
        if (!t) {
            xfree(path);
            continue;
        }
        TraceFile *f = xalloc(sizeof(TraceFile));
        f->path = path;
        f->name = wdup(t->name[0] ? t->name : e->name);
        f->date = wdup(t->date);
        f->items = t->items.n;
        trace_free(t);
        pv_push(&out, f);
    }
    fs_list_free(&v);
    return out;
}

void traces_list_free(PtrVec *v) {
    for (size_t i = 0; i < v->n; i++) {
        TraceFile *f = v->items[i];
        xfree(f->path), xfree(f->name), xfree(f->date), xfree(f);
    }
    pv_free(v);
}

bool trace_save(const Trace *t, WCHAR **saved_path) {
    StrBuf b = {0};
    for (const WCHAR *p = t->name; *p && b.len < 60; p++) {
        WCHAR c = *p;
        if (c < 32 || c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
        sb_addc(&b, c);
    }
    WCHAR *stamp = now_text(true), *file = wfmt(u"%s - %s.xtr", stamp, b.len ? sb_str(&b) : u"trace");
    WCHAR *path = path_join(g_paths.traces_dir, file);
    xfree(stamp), xfree(file);
    sb_free(&b);
    size_t len;
    char *data = trace_serialize(t, &len);
    dir_create_all(g_paths.traces_dir);
    bool ok = file_write_all(path, data, len);
    xfree(data);
    if (ok && saved_path) *saved_path = path;
    else xfree(path);
    return ok;
}
