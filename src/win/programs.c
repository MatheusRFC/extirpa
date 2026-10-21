// SPDX-License-Identifier: MIT
#include "programs.h"

#include "../core/cmdline.h"
#include "../core/lnk.h"
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

static void msi_dirs_free(MsiDirs *m) {
    if (!m) return;
    for (int r = 0; r < 3; r++) xfree(m->lca[r]);
    strings_free(&m->outside);
    xfree(m);
}

void program_free(Program *p) {
    if (!p) return;
    xfree(p->key_path), xfree(p->name), xfree(p->publisher), xfree(p->version), xfree(p->install_location);
    xfree(p->uninstall), xfree(p->quiet_uninstall), xfree(p->display_icon);
    msi_dirs_free(p->msi_dirs);
    xfree(p);
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

// ---------------------------------------------------------------- Windows Installer

#define MSI_USERDATA u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Installer\\UserData"
#define MSI_MAX_OUTSIDE 32

typedef struct {
    WCHAR code[33];  // código do produto compactado
    MsiDirs d;
} MsiAgg;

// Guardado entre chamadas: ler os componentes leva segundos em máquinas com muitos MSI
// (176 mil componentes, 4,4 s num Windows Server do GitHub Actions).
static struct {
    SRWLOCK lock;
    WCHAR *stamp;  // SIDs, número de componentes e data de gravação de cada chave Components
    PtrVec aggs;   // MsiAgg*, por código
} g_msi;

static MsiAgg *agg_find(const PtrVec *v, const WCHAR *code, size_t *pos) {
    size_t lo = 0, hi = v->n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        MsiAgg *a = v->items[mid];
        int c = wicmp(code, a->code);
        if (!c) return a;
        if (c < 0) hi = mid;
        else lo = mid + 1;
    }
    if (pos) *pos = lo;
    return NULL;
}

static void aggs_free(PtrVec *v) {
    for (size_t i = 0; i < v->n; i++) {
        MsiAgg *a = v->items[i];
        for (int r = 0; r < 3; r++) xfree(a->d.lca[r]);
        strings_free(&a->d.outside);
        xfree(a);
    }
    pv_free(v);
}

// Pasta comum mais funda de dois caminhos (NULL se não houver).
static WCHAR *common_dir(const WCHAR *a, const WCHAR *b) {
    size_t i = 0, last = 0;
    while (a[i] && b[i] && wfold(a[i]) == wfold(b[i])) {
        if (a[i] == '\\') last = i;
        i++;
    }
    if ((!a[i] || a[i] == '\\') && (!b[i] || b[i] == '\\')) last = i;
    return last >= 3 ? wdupn(a, last) : NULL;
}

// Raízes de programas (a terceira é alocada: %LOCALAPPDATA%\Programs).
static void msi_roots(const WCHAR *roots[3], WCHAR **user_programs) {
    *user_programs = g_paths.local_appdata ? path_join(g_paths.local_appdata, u"Programs") : NULL;
    roots[0] = g_paths.program_files, roots[1] = g_paths.program_files_x86, roots[2] = *user_programs;
}

// Caminho de arquivo do valor de um componente ("C:\x\a.exe"; "C?\..." quando roda da origem).
// Chaves do Registro ("02:\Software\...") e pastas não contam.
static WCHAR *component_file(const WCHAR *v) {
    if (!v || wlen(v) < 4) return NULL;
    WCHAR c = v[0] | 32;
    if (c < 'a' || c > 'z' || (v[1] != ':' && v[1] != '?') || v[2] != '\\') return NULL;
    if (v[wlen(v) - 1] == '\\') return NULL;
    WCHAR *w = wdup(v);
    w[1] = ':';
    WCHAR *n = path_norm(w);
    xfree(w);
    return n;
}

static void agg_add_file(PtrVec *aggs, const WCHAR *code, const WCHAR *file, const WCHAR *const roots[3]) {
    size_t pos = 0;
    MsiAgg *a = agg_find(aggs, code, &pos);
    if (!a) {
        a = xalloc(sizeof(MsiAgg));
        mem_copy(a->code, code, 33 * sizeof(WCHAR));
        pv_insert(aggs, pos, a);
    }
    WCHAR *parent = path_parent(file);
    if (!parent) return;
    for (int r = 0; r < 3; r++) {
        if (!roots[r] || !path_is_under(file, roots[r])) continue;
        if (!a->d.lca[r]) {
            a->d.lca[r] = parent;
        } else {
            WCHAR *c = common_dir(a->d.lca[r], parent);
            xfree(a->d.lca[r]), xfree(parent);
            a->d.lca[r] = c ? c : wdup(roots[r]);  // só a raiz em comum: não serve
        }
        return;
    }
    bool known = false;
    for (size_t i = 0; i < a->d.outside.n && !known; i++) known = path_eq(a->d.outside.items[i], parent);
    if (!known && a->d.outside.n < MSI_MAX_OUTSIDE) pv_push(&a->d.outside, parent);
    else xfree(parent);
}

// Estado das chaves Components e Products de cada usuário: muda quando um produto MSI é
// instalado ou removido (entram ou saem componentes e produtos).
static WCHAR *msi_stamp(HKEY ud, const PtrVec *sids) {
    static const WCHAR *const SUBS[] = {u"Components", u"Products"};
    WCHAR *s = wdup(u"");
    for (size_t i = 0; i < sids->n; i++) {
        for (size_t j = 0; j < COUNT(SUBS); j++) {
            WCHAR *sub = path_join(sids->items[i], SUBS[j]);
            HKEY k = reg_open_sub(ud, sub);
            xfree(sub);
            if (!k) continue;
            DWORD n = 0;
            FILETIME ft = {0};
            RegQueryInfoKeyW(k, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, &ft);
            RegCloseKey(k);
            WCHAR *part = wfmt(u"%s\\%s:%u:%u:%u;", sids->items[i], SUBS[j], (unsigned)n, (unsigned)ft.dwHighDateTime, (unsigned)ft.dwLowDateTime);
            WCHAR *t = wcat(s, part);
            xfree(s), xfree(part);
            s = t;
        }
    }
    return s;
}

// Uma passada por todos os componentes: só os de um produto (e de mais nenhum) contam, porque um
// componente compartilhado (runtime, pasta comum do editor) não diz onde fica o programa.
static void msi_build(HKEY ud, const PtrVec *sids) {
    const WCHAR *roots[3];
    WCHAR *user_programs;
    msi_roots(roots, &user_programs);
    ULONGLONG t0 = GetTickCount64();
    size_t comps = 0;
    WCHAR name[64], data[MAX_PATH + 64];
    for (size_t s = 0; s < sids->n; s++) {
        WCHAR *sub = path_join(sids->items[s], u"Components");
        HKEY ck = reg_open_sub(ud, sub);
        xfree(sub);
        if (!ck) continue;
        PtrVec list = reg_subkeys(ck);
        for (size_t c = 0; c < list.n; c++) {
            HKEY k = reg_open_sub(ck, list.items[c]);
            if (!k) continue;
            comps++;
            size_t clients = 0;
            WCHAR code[33] = {0}, *file = NULL;
            for (DWORD i = 0; clients < 2; i++) {
                DWORD nn = COUNT(name), dn = sizeof(data) - sizeof(WCHAR), type = 0;
                LSTATUS st = RegEnumValueW(k, i, name, &nn, NULL, &type, (BYTE *)data, &dn);
                if (st == ERROR_MORE_DATA) {  // caminho longo demais para ser útil: só o nome
                    nn = COUNT(name);
                    if (RegEnumValueW(k, i, name, &nn, NULL, &type, NULL, NULL) != ERROR_SUCCESS) break;
                    type = REG_NONE;
                } else if (st != ERROR_SUCCESS) {
                    break;
                }
                if (nn != 32 || wieq(name, u"00000000000000000000000000000000")) continue;
                if (++clients > 1) break;
                mem_copy(code, name, 32 * sizeof(WCHAR));
                if (type == REG_SZ || type == REG_EXPAND_SZ) {
                    data[dn / sizeof(WCHAR)] = 0;
                    file = component_file(data);
                }
            }
            if (clients == 1 && file) agg_add_file(&g_msi.aggs, code, file, roots);
            xfree(file);
            RegCloseKey(k);
        }
        strings_free(&list);
        RegCloseKey(ck);
    }
    xfree(user_programs);
    log_fmt(u"Windows Installer: %u componentes lidos em %U ms (%u produtos com arquivos só deles)", (unsigned)comps, (uint64_t)(GetTickCount64() - t0),
            (unsigned)g_msi.aggs.n);
}

void programs_load_msi(PtrVec *programs) {
    bool any = false;
    for (size_t i = 0; i < programs->n && !any; i++) any = !((Program *)programs->items[i])->msi_loaded;
    if (!any) return;
    AcquireSRWLockExclusive(&g_msi.lock);
    HKEY ud = reg_open(MSI_USERDATA, KEY_READ);
    PtrVec sids = ud ? reg_subkeys(ud) : (PtrVec){0};
    WCHAR *stamp = ud ? msi_stamp(ud, &sids) : wdup(u"");
    if (!g_msi.stamp || !weq(stamp, g_msi.stamp)) {
        aggs_free(&g_msi.aggs);
        if (ud) msi_build(ud, &sids);
        xfree(g_msi.stamp);
        g_msi.stamp = stamp;
    } else {
        xfree(stamp);
    }
    strings_free(&sids);
    if (ud) RegCloseKey(ud);
    for (size_t i = 0; i < programs->n; i++) {
        Program *p = programs->items[i];
        if (p->msi_loaded) continue;
        p->msi_loaded = true;
        WCHAR code[33];
        MsiAgg *a = p->key_path && guid_pack(path_leaf(p->key_path), code) ? agg_find(&g_msi.aggs, code, NULL) : NULL;
        if (!a) continue;
        MsiDirs *m = xalloc(sizeof(MsiDirs));
        for (int r = 0; r < 3; r++) m->lca[r] = a->d.lca[r] ? wdup(a->d.lca[r]) : NULL;
        for (size_t j = 0; j < a->d.outside.n; j++) pv_push(&m->outside, wdup(a->d.outside.items[j]));
        msi_dirs_free(p->msi_dirs);
        p->msi_dirs = m;
    }
    ReleaseSRWLockExclusive(&g_msi.lock);
}

// Pastas do programa pelos componentes MSI. Em Arquivos de Programas (e em
// %LOCALAPPDATA%\Programs) vale a pasta comum mais funda dos arquivos dele; fora dali, só a
// pasta de um arquivo com o nome do programa.
static void add_msi_dirs(Identity *id, const MsiDirs *m) {
    const WCHAR *roots[3];
    WCHAR *user_programs;
    msi_roots(roots, &user_programs);
    for (int r = 0; r < 3; r++) {
        if (!roots[r] || !m->lca[r]) continue;
        WCHAR *lca = wdup(m->lca[r]);
        // subpastas genéricas ("bin") sobem até a pasta do programa, nunca até a raiz
        for (int up = 0; lca && up < 2 && path_is_under(lca, roots[r]); up++) {
            WCHAR *parent = path_parent(lca), *ln = norm_alnum(path_leaf(lca));
            bool generic = is_generic_word(ln) && parent && path_is_under(parent, roots[r]);
            xfree(ln);
            if (!generic) {
                xfree(parent);
                break;
            }
            xfree(lca);
            lca = parent;
        }
        if (lca && path_is_under(lca, roots[r]) && !protect_dir_blocked(lca)) {
            WCHAR *parent = path_parent(lca);
            identity_add_dir(id, lca, parent && path_eq(parent, roots[r]));
            xfree(parent);
        }
        xfree(lca);
    }
    for (size_t i = 0; i < m->outside.n; i++) {
        const WCHAR *dir = m->outside.items[i];
        if (!protect_dir_blocked(dir) && match_name(id, path_leaf(dir)) >= CONF_MEDIUM) identity_add_dir(id, dir, false);
    }
    xfree(user_programs);
}

Identity *program_identity(const Program *p) {
    Identity *id = identity_new(p->name, p->publisher);
    identity_set_version(id, p->version);
    // O nome da chave de desinstalação também é um nome do programa quando contém o nome
    // dele ("FileZilla Client" para o FileZilla 3.71.1); códigos do Windows Installer ({GUID})
    // não contam, e o "_is1" do Inno Setup sai.
    if (p->key_path) {
        const WCHAR *leaf = path_leaf(p->key_path);
        WCHAR *k = wiends(leaf, u"_is1") ? wdupn(leaf, wlen(leaf) - 4) : wdup(leaf);
        WCHAR *kn = norm_alnum(k);
        bool related = (wlen(id->name_norm) >= 4 && wifind(kn, id->name_norm)) || (id->primary && wifind(kn, id->primary));
        if (k[0] != '{' && related) identity_add_exact(id, k);
        xfree(k), xfree(kn);
    }
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
    if (p->msi_dirs) add_msi_dirs(id, p->msi_dirs);
    identity_add_short_names(id);
    return id;
}

// Executável apontado pelo atalho, se o atalho tiver o nome exato do programa.
static WCHAR *lnk_target_named(const Identity *id, const WCHAR *path, const WCHAR *file_name) {
    WCHAR *base = path_strip_ext(file_name);
    bool named = match_name(id, base) == CONF_HIGH;
    xfree(base);
    if (!named || file_size(path) > (1u << 20)) return NULL;
    uint8_t *data;
    size_t len;
    WCHAR *r = NULL;
    if (file_read_all(path, &data, &len)) {
        LnkInfo l;
        if (lnk_parse(data, len, &l)) {
            if (l.target && wiends(l.target, u".exe")) {
                WCHAR *x = expand_env(l.target);
                r = path_norm(x);
                xfree(x);
            }
            lnk_free(&l);
        }
        xfree(data);
    }
    return r;
}

static void hint_shortcuts(Identity *id, const WCHAR *dir, int depth) {
    if (!dir || depth > 2) return;
    WCHAR *pat = path_join(dir, u"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(pat, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
    xfree(pat);
    if (h == INVALID_HANDLE_VALUE) return;
    PtrVec subdirs = {0};
    do {
        if (fd.cFileName[0] == '.') continue;
        WCHAR *path = path_join(dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) pv_push(&subdirs, wdup(path));
        } else if (wiends(fd.cFileName, u".lnk")) {
            WCHAR *t = lnk_target_named(id, path, fd.cFileName);
            if (t) add_exe_dir(id, t);
            xfree(t);
        }
        xfree(path);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    for (size_t i = 0; i < subdirs.n; i++) hint_shortcuts(id, subdirs.items[i], depth + 1);
    strings_free(&subdirs);
}

static void hint_app_paths(Identity *id) {
    static const WCHAR *const ROOTS[] = {
        u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths",
        u"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\App Paths",
        u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\App Paths",
    };
    for (size_t r = 0; r < COUNT(ROOTS); r++) {
        HKEY root = reg_open(ROOTS[r], KEY_READ);
        if (!root) continue;
        PtrVec subs = reg_subkeys(root);
        for (size_t i = 0; i < subs.n; i++) {
            const WCHAR *leaf = subs.items[i];
            if (!wiends(leaf, u".exe")) continue;
            WCHAR *base = path_strip_ext(leaf);
            bool named = match_name(id, base) == CONF_HIGH;
            xfree(base);
            if (!named) continue;
            HKEY k = reg_open_sub(root, leaf);
            WCHAR *v = k ? reg_get_str(k, u"") : NULL;
            if (k) RegCloseKey(k);
            if (v) {
                // o valor às vezes vem entre aspas
                WCHAR *x = expand_env(v);
                Cmd c;
                if (cmd_split(x, &c)) {
                    WCHAR *n = path_norm(c.exe);
                    if (wiends(n, u".exe") && n[1] == ':') add_exe_dir(id, n);
                    xfree(n);
                    cmd_free(&c);
                }
                xfree(x);
            }
            xfree(v);
        }
        strings_free(&subs);
        RegCloseKey(root);
    }
}

Identity *program_identity_target(Program *p) {
    PtrVec one = {0};
    pv_push(&one, p);
    programs_load_msi(&one);
    pv_free(&one);
    Identity *id = program_identity(p);
    if (!id->dirs.n) {
        // sem InstallLocation, sem ícone e com desinstalador fora da pasta (msiexec, cache do
        // instalador): o App Paths e os atalhos com o nome do programa dizem onde ele está
        hint_app_paths(id);
        hint_shortcuts(id, g_paths.common_programs, 0);
        hint_shortcuts(id, g_paths.programs, 0);
        identity_add_short_names(id);
        for (size_t i = 0; i < id->dirs.n; i++) log_fmt(u"  pasta deduzida pelo App Paths ou pelos atalhos: %s", id->dirs.items[i]);
    }
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
