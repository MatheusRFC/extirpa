// SPDX-License-Identifier: MIT
#include "programs.h"

#include "../core/cmdline.h"
#include "../core/pathx.h"
#include "../core/protect.h"
#include "platform.h"
#include "reg.h"

static const struct {
    const WCHAR *path;
    Scope scope;
} ROOTS[] = {
    {u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", SCOPE_MACHINE64},
    {u"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall", SCOPE_MACHINE32},
    {u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", SCOPE_USER},
};

void program_free(Program *p) {
    if (!p) return;
    xfree(p->key_path), xfree(p->name), xfree(p->publisher), xfree(p->version), xfree(p->install_location);
    xfree(p->uninstall), xfree(p->quiet_uninstall), xfree(p->display_icon), xfree(p);
}

void programs_free(PtrVec *v) {
    for (size_t i = 0; i < v->n; i++) program_free(v->items[i]);
    pv_free(v);
}

static WCHAR *get_trimmed(HKEY k, const WCHAR *name) {
    WCHAR *s = reg_get_str(k, name);
    if (!s) return NULL;
    WCHAR *t = wtrim_dup(s);
    xfree(s);
    if (!t[0]) {
        xfree(t);
        return NULL;
    }
    return t;
}

static bool is_update(HKEY k) {
    WCHAR *parent = get_trimmed(k, u"ParentKeyName"), *rel = get_trimmed(k, u"ReleaseType");
    bool upd = parent != NULL;
    if (rel && (wieq(rel, u"Update") || wieq(rel, u"Hotfix") || wieq(rel, u"Security Update") || wieq(rel, u"Service Pack"))) upd = true;
    xfree(parent), xfree(rel);
    return upd;
}

static Program *read_program(HKEY k, const WCHAR *key_path, Scope scope) {
    DWORD v = 0;
    if (reg_get_dword(k, u"SystemComponent", &v) && v == 1) return NULL;
    if (is_update(k)) return NULL;
    WCHAR *name = get_trimmed(k, u"DisplayName");
    if (!name) return NULL;
    Program *p = xalloc(sizeof(Program));
    p->key_path = wdup(key_path);
    p->name = name;
    p->publisher = get_trimmed(k, u"Publisher");
    p->version = get_trimmed(k, u"DisplayVersion");
    p->install_location = get_trimmed(k, u"InstallLocation");
    p->uninstall = get_trimmed(k, u"UninstallString");
    p->quiet_uninstall = get_trimmed(k, u"QuietUninstallString");
    p->display_icon = get_trimmed(k, u"DisplayIcon");
    p->scope = scope;
    p->icon = -1;
    if (reg_get_dword(k, u"EstimatedSize", &v)) p->size = (uint64_t)v * 1024;
    if (reg_get_dword(k, u"WindowsInstaller", &v) && v == 1) p->msi = true;
    if (reg_get_dword(k, u"NoRemove", &v) && v == 1) p->no_remove = true;
    WCHAR *date = get_trimmed(k, u"InstallDate");
    if (!date || !parse_install_date(date, &p->year, &p->month, &p->day)) {
        // sem data de instalação: usa a data da última gravação da chave
        uint64_t ft = reg_last_write(k);
        if (ft) {
            FILETIME f = {(DWORD)ft, (DWORD)(ft >> 32)}, local;
            SYSTEMTIME t;
            if (FileTimeToLocalFileTime(&f, &local) && FileTimeToSystemTime(&local, &t)) p->year = t.wYear, p->month = t.wMonth, p->day = t.wDay;
        }
    }
    xfree(date);
    return p;
}

static int cmp_name(const void *a, const void *b, void *ctx) {
    (void)ctx;
    return wicmp(((const Program *)a)->name, ((const Program *)b)->name);
}

static bool same_text(const WCHAR *a, const WCHAR *b) { return wieq(a ? a : u"", b ? b : u""); }

PtrVec programs_load(void) {
    PtrVec all = {0};
    for (size_t r = 0; r < COUNT(ROOTS); r++) {
        HKEY root = reg_open(ROOTS[r].path, KEY_READ);
        if (!root) continue;
        PtrVec subs = reg_subkeys(root);
        for (size_t i = 0; i < subs.n; i++) {
            HKEY k = reg_open_sub(root, subs.items[i]);
            if (!k) continue;
            WCHAR *path = path_join(ROOTS[r].path, subs.items[i]);
            Program *p = read_program(k, path, ROOTS[r].scope);
            xfree(path);
            RegCloseKey(k);
            if (!p) continue;
            // o mesmo programa registrado duas vezes (64 e 32 bits): fica o primeiro
            bool dup = false;
            for (size_t j = 0; j < all.n && !dup; j++) {
                Program *o = all.items[j];
                dup = same_text(o->name, p->name) && same_text(o->version, p->version) && same_text(o->publisher, p->publisher) &&
                      same_text(o->uninstall, p->uninstall);
            }
            if (dup) program_free(p);
            else pv_push(&all, p);
        }
        strings_free(&subs);
        RegCloseKey(root);
    }
    pv_sort(&all, cmp_name, NULL);
    return all;
}

// Executável de uma linha de comando (UninstallString).
static WCHAR *exe_from(const WCHAR *text) {
    if (!text) return NULL;
    WCHAR *x = expand_env(text);
    Cmd c;
    WCHAR *r = NULL;
    if (cmd_split(x, &c)) r = path_norm(c.exe);
    cmd_free(&c);
    xfree(x);
    return r;
}

// A pasta fica direto numa pasta de programas? Arquivos de Programas (64 e 32 bits) ou, nas
// instalações por usuário, %LOCALAPPDATA%\Programs.
static bool in_programs_root(const WCHAR *dir) {
    WCHAR *parent = path_parent(dir), *user_programs = g_paths.local_appdata ? path_join(g_paths.local_appdata, u"Programs") : NULL;
    bool r = parent && ((g_paths.program_files && path_eq(parent, g_paths.program_files)) ||
                        (g_paths.program_files_x86 && path_eq(parent, g_paths.program_files_x86)) || (user_programs && path_eq(parent, user_programs)));
    xfree(parent), xfree(user_programs);
    return r;
}

// A pasta de um executável conta como pasta do programa se tiver o nome dele ou estiver
// direto numa pasta de programas. Subpastas genéricas ("bin", "uninstall") sobem um nível:
// C:\Program Files\Foo\uninstall\unins.exe é do C:\Program Files\Foo.
static void add_exe_dir(Identity *id, const WCHAR *exe) {
    if (!exe) return;
    WCHAR *dir = path_parent(exe);
    for (int up = 0; dir && up < 2; up++) {
        WCHAR *leaf_norm = norm_alnum(path_leaf(dir));
        bool generic = is_generic_word(leaf_norm);
        xfree(leaf_norm);
        if (!generic) break;
        WCHAR *p = path_parent(dir);
        xfree(dir);
        dir = p;
    }
    if (dir && !protect_dir_blocked(dir)) {
        bool root = in_programs_root(dir);
        if (root || match_name(id, path_leaf(dir)) >= CONF_MEDIUM) identity_add_dir(id, dir, root);
    }
    identity_add_exe(id, exe);
    xfree(dir);
}

// Nome curto 8.3 de cada pasta (programas antigos gravam C:\PROGRA~1\... no Registro). Só dá
// para descobrir enquanto a pasta existe: por isso a identidade é montada antes de desinstalar.
void identity_add_short_names(Identity *id) {
    size_t n = id->dirs.n;
    for (size_t i = 0; i < n; i++) {
        WCHAR buf[MAX_PATH + 1];
        DWORD r = GetShortPathNameW(id->dirs.items[i], buf, MAX_PATH);
        if (r && r < MAX_PATH) {
            buf[r] = 0;
            if (!wieq(buf, id->dirs.items[i])) identity_add_alt_dir(id, buf);
        }
    }
}

Identity *program_identity(const Program *p) {
    Identity *id = identity_new(p->name, p->publisher);
    identity_set_version(id, p->version);
    if (p->install_location) {
        WCHAR *loc = expand_env(p->install_location), *n = path_norm(loc);
        // pasta criada pelo instalador em Arquivos de Programas: o nome dela é o do programa
        identity_add_dir(id, n, in_programs_root(n));
        xfree(loc), xfree(n);
    }
    int idx;
    WCHAR *icon = program_icon_source(p, &idx), *unins = exe_from(p->uninstall);
    if (icon && wiends(icon, u".exe")) add_exe_dir(id, icon);
    if (unins) add_exe_dir(id, unins);
    xfree(icon), xfree(unins);
    identity_add_short_names(id);
    return id;
}

Program *programs_find_by_exe(const PtrVec *v, const WCHAR *exe_path) {
    Program *best = NULL;
    size_t best_len = 0;
    for (size_t i = 0; i < v->n; i++) {
        Program *p = v->items[i];
        Identity *id = program_identity(p);
        for (size_t d = 0; d < id->dirs.n; d++) {
            const WCHAR *dir = id->dirs.items[d];
            if (path_is_under(exe_path, dir) && wlen(dir) > best_len) best = p, best_len = wlen(dir);
        }
        identity_free(id);
    }
    return best;
}

bool program_still_registered(const Program *p) { return reg_exists(p->key_path); }

WCHAR *program_icon_source(const Program *p, int *index) {
    *index = 0;
    if (p->display_icon) {
        WCHAR *x = expand_env(p->display_icon), *t = path_norm(x);
        xfree(x);
        const WCHAR *comma = wrfindc(t, ',');
        if (comma) {
            const WCHAR *n = comma + 1;
            bool neg = *n == '-';
            uint64_t v;
            if (w_to_u64(n + (neg ? 1 : 0), &v) && v < 100000) {
                *index = neg ? -(int)v : (int)v;
                WCHAR *r = wdupn(t, (size_t)(comma - t));
                WCHAR *rn = path_norm(r);
                xfree(r), xfree(t);
                return rn;
            }
        }
        return t;
    }
    WCHAR *u = exe_from(p->uninstall);
    if (u && wifind(path_leaf(u), u"msiexec")) {
        xfree(u);
        return NULL;
    }
    return u;
}
