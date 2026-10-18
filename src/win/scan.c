// SPDX-License-Identifier: MIT
#include "scan.h"

#include "../core/lnk.h"
#include "../core/pathx.h"
#include "../core/protect.h"
#include "../core/regtext.h"
#include "fsx.h"
#include "i18n.h"
#include "platform.h"
#include "reg.h"

void ctl_progress(Ctl *c, const WCHAR *text) {
    if (c && c->progress) c->progress(c->ctx, text);
}

bool ctl_cancelled(const Ctl *c) { return c && c->cancel; }

static int threshold(ScanMode m) { return m == SCAN_SAFE ? CONF_HIGH : m == SCAN_MODERATE ? CONF_MEDIUM : CONF_LOW; }

// Texto de um valor de Registro com variáveis expandidas e sem o prefixo "\??\" dos drivers.
static WCHAR *value_text(HKEY k, const WCHAR *name) {
    WCHAR *s = reg_get_str(k, name);
    if (!s) return NULL;
    WCHAR *x = expand_env(s);
    xfree(s);
    if (wistarts(x, u"\\??\\")) {
        WCHAR *t = wdup(x + 4);
        xfree(x);
        x = t;
    }
    return x;
}

// ---------------------------------------------------------------- Registro por nome

static const WCHAR *const SOFTWARE_ROOTS[] = {u"HKLM\\SOFTWARE", u"HKLM\\SOFTWARE\\WOW6432Node", u"HKCU\\Software"};
static const WCHAR *const SKIP_L1[] = {u"Classes", u"Microsoft", u"Policies", u"WOW6432Node", u"Clients", u"RegisteredApplications"};

static bool skip_l1(const WCHAR *name) {
    for (size_t i = 0; i < COUNT(SKIP_L1); i++)
        if (wieq(name, SKIP_L1[i])) return true;
    return false;
}

static void scan_reg_names(LeftList *out, const Identity *id, int thr) {
    bool microsoft = wistarts(id->publisher, u"Microsoft");
    for (size_t r = 0; r < COUNT(SOFTWARE_ROOTS); r++) {
        HKEY root = reg_open(SOFTWARE_ROOTS[r], KEY_READ);
        if (!root) continue;
        PtrVec subs = reg_subkeys(root);
        for (size_t i = 0; i < subs.n; i++) {
            const WCHAR *s = subs.items[i];
            WCHAR *path = path_join(SOFTWARE_ROOTS[r], s);
            if (microsoft && wieq(s, u"Microsoft")) {
                // programa da Microsoft: só nomes exatos dentro de ...\Microsoft
                HKEY k = reg_open_sub(root, s);
                if (k) {
                    PtrVec subs2 = reg_subkeys(k);
                    for (size_t j = 0; j < subs2.n; j++)
                        if (match_name(id, subs2.items[j]) == CONF_HIGH) {
                            WCHAR *p2 = path_join(path, subs2.items[j]);
                            left_add(out, LK_KEY, p2, NULL, CONF_HIGH, LR_NAME, NULL);
                            xfree(p2);
                        }
                    strings_free(&subs2);
                    RegCloseKey(k);
                }
            } else if (!skip_l1(s)) {
                int c = match_name(id, s);
                if (c && c >= thr) {
                    left_add(out, LK_KEY, path, NULL, c, LR_NAME, NULL);
                } else if (match_publisher(id, s)) {
                    // chave do editor: procura o programa um nível abaixo
                    HKEY k = reg_open_sub(root, s);
                    if (k) {
                        PtrVec subs2 = reg_subkeys(k);
                        bool any = false;
                        for (size_t j = 0; j < subs2.n; j++) {
                            int c2 = match_name(id, subs2.items[j]);
                            if (c2 && c2 >= thr) {
                                WCHAR *p2 = path_join(path, subs2.items[j]);
                                any = left_add(out, LK_KEY, p2, NULL, c2, LR_NAME, NULL) != NULL || any;
                                xfree(p2);
                            }
                        }
                        if (any) left_add(out, LK_KEY_IF_EMPTY, path, NULL, CONF_MEDIUM, LR_PUBLISHER, NULL);
                        strings_free(&subs2);
                        RegCloseKey(k);
                    }
                }
            }
            xfree(path);
        }
        strings_free(&subs);
        RegCloseKey(root);
    }
}

// ---------------------------------------------------------------- entradas de desinstalação

static const WCHAR *const UNINSTALL_ROOTS[] = {
    u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
    u"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
    u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
};

static void scan_uninstall_entries(LeftList *out, const Identity *id, const WCHAR *own_key, ScanMode mode) {
    if (own_key && reg_exists(own_key)) left_add(out, LK_KEY, own_key, NULL, CONF_HIGH, LR_UNINSTALL_KEY, NULL);
    if (mode < SCAN_MODERATE || !id->dirs.n) return;
    // outras entradas do mesmo programa que ficaram para trás (outra versão, outro idioma)
    for (size_t r = 0; r < COUNT(UNINSTALL_ROOTS); r++) {
        HKEY root = reg_open(UNINSTALL_ROOTS[r], KEY_READ);
        if (!root) continue;
        PtrVec subs = reg_subkeys(root);
        for (size_t i = 0; i < subs.n; i++) {
            WCHAR *path = path_join(UNINSTALL_ROOTS[r], subs.items[i]);
            HKEY k = (own_key && path_eq(path, own_key)) ? NULL : reg_open_sub(root, subs.items[i]);
            if (k) {
                WCHAR *name = reg_get_str(k, u"DisplayName");
                WCHAR *loc = value_text(k, u"InstallLocation"), *un = value_text(k, u"UninstallString");
                if (name && match_name(id, name) >= CONF_MEDIUM && (match_path_ref(id, loc) || match_path_ref(id, un)))
                    left_add(out, LK_KEY, path, NULL, CONF_MEDIUM, LR_UNINSTALL_KEY, name);
                xfree(name), xfree(loc), xfree(un);
                RegCloseKey(k);
            }
            xfree(path);
        }
        strings_free(&subs);
        RegCloseKey(root);
    }
}

// ---------------------------------------------------------------- inicialização, App Paths, DLLs, serviços

static bool exe_missing(const WCHAR *cmdline) {
    // o arquivo apontado pelo comando não existe mais?
    if (!cmdline || !cmdline[0]) return false;
    const WCHAR *s = cmdline;
    WCHAR *exe;
    if (*s == '"') {
        const WCHAR *q = wfindc(s + 1, '"');
        exe = q ? wdupn(s + 1, (size_t)(q - s - 1)) : wdup(s + 1);
    } else {
        const WCHAR *e = wifind(s, u".exe");
        exe = e ? wdupn(s, (size_t)(e - s) + 4) : wdup(s);
    }
    bool missing = wfindc(exe, '\\') && !file_exists(exe);
    xfree(exe);
    return missing;
}

static void scan_run(LeftList *out, const Identity *id) {
    static const struct {
        const WCHAR *key, *approved;
    } RUN[] = {
        {u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run"},
        {u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce", NULL},
        {u"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run",
         u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run32"},
        {u"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\RunOnce", NULL},
        {u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run"},
        {u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce", NULL},
    };
    for (size_t r = 0; r < COUNT(RUN); r++) {
        HKEY k = reg_open(RUN[r].key, KEY_READ);
        if (!k) continue;
        PtrVec vals = reg_values(k);
        for (size_t i = 0; i < vals.n; i++) {
            RegValue *v = vals.items[i];
            if (v->type != REG_SZ && v->type != REG_EXPAND_SZ) continue;
            WCHAR *raw = wdupn((const WCHAR *)v->data, v->size / 2), *data = expand_env(raw);
            int conf = CONF_NONE;
            if (match_path_ref(id, data)) conf = CONF_HIGH;
            else if (match_name(id, v->name) == CONF_HIGH && exe_missing(data)) conf = CONF_MEDIUM;
            if (conf) {
                left_add(out, LK_VALUE, RUN[r].key, v->name, conf, LR_RUN, data);
                if (RUN[r].approved) {
                    HKEY a = reg_open(RUN[r].approved, KEY_READ);
                    if (a) {
                        DWORD t, sz = 0;
                        if (RegQueryValueExW(a, v->name, NULL, &t, NULL, &sz) == ERROR_SUCCESS)
                            left_add(out, LK_VALUE, RUN[r].approved, v->name, conf, LR_RUN, NULL);
                        RegCloseKey(a);
                    }
                }
            }
            xfree(raw), xfree(data);
        }
        reg_values_free(&vals);
        RegCloseKey(k);
    }
}

static void scan_app_paths(LeftList *out, const Identity *id) {
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
            HKEY k = reg_open_sub(root, subs.items[i]);
            if (!k) continue;
            WCHAR *def = value_text(k, u""), *path = value_text(k, u"Path");
            if (match_path_ref(id, def) || match_path_ref(id, path)) {
                WCHAR *full = path_join(ROOTS[r], subs.items[i]);
                left_add(out, LK_KEY, full, NULL, CONF_HIGH, LR_APP_PATHS, def);
                xfree(full);
            }
            xfree(def), xfree(path);
            RegCloseKey(k);
        }
        strings_free(&subs);
        RegCloseKey(root);
    }
}

static void scan_shared_dlls(LeftList *out, const Identity *id) {
    static const WCHAR *const ROOTS[] = {
        u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\SharedDLLs",
        u"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\SharedDLLs",
    };
    for (size_t r = 0; r < COUNT(ROOTS); r++) {
        HKEY k = reg_open(ROOTS[r], KEY_READ);
        if (!k) continue;
        PtrVec vals = reg_values(k);
        for (size_t i = 0; i < vals.n; i++) {
            RegValue *v = vals.items[i];
            if (match_path_ref(id, v->name)) left_add(out, LK_VALUE, ROOTS[r], v->name, CONF_HIGH, LR_SHARED_DLL, NULL);
        }
        reg_values_free(&vals);
        RegCloseKey(k);
    }
}

static void scan_services(LeftList *out, const Identity *id) {
    static const WCHAR SERVICES[] = u"HKLM\\SYSTEM\\CurrentControlSet\\Services";
    HKEY root = reg_open(SERVICES, KEY_READ);
    if (!root) return;
    PtrVec subs = reg_subkeys(root);
    for (size_t i = 0; i < subs.n; i++) {
        HKEY k = reg_open_sub(root, subs.items[i]);
        if (!k) continue;
        WCHAR *img = value_text(k, u"ImagePath");
        if (img) {
            if (match_path_ref(id, img)) left_add(out, LK_SERVICE, subs.items[i], NULL, CONF_HIGH, LR_SERVICE, img);
            else if (match_name(id, subs.items[i]) == CONF_HIGH && exe_missing(img))
                left_add(out, LK_SERVICE, subs.items[i], NULL, CONF_MEDIUM, LR_SERVICE, img);
        }
        xfree(img);
        RegCloseKey(k);
    }
    strings_free(&subs);
    RegCloseKey(root);
}

static void scan_applications(LeftList *out, const Identity *id) {
    static const WCHAR *const ROOTS[] = {u"HKLM\\SOFTWARE\\Classes\\Applications", u"HKCU\\Software\\Classes\\Applications"};
    for (size_t r = 0; r < COUNT(ROOTS); r++) {
        HKEY root = reg_open(ROOTS[r], KEY_READ);
        if (!root) continue;
        PtrVec subs = reg_subkeys(root);
        for (size_t i = 0; i < subs.n; i++) {
            WCHAR *sub = path_join(subs.items[i], u"shell\\open\\command");
            HKEY k = reg_open_sub(root, sub);
            xfree(sub);
            if (!k) continue;
            WCHAR *cmd = value_text(k, u"");
            if (match_path_ref(id, cmd)) {
                WCHAR *full = path_join(ROOTS[r], subs.items[i]);
                left_add(out, LK_KEY, full, NULL, CONF_HIGH, LR_FILE_ASSOC, cmd);
                xfree(full);
            }
            xfree(cmd);
            RegCloseKey(k);
        }
        strings_free(&subs);
        RegCloseKey(root);
    }
}

// Modo avançado: tipos de arquivo (ProgIDs) e componentes COM que apontam para o programa.
static void scan_classes(LeftList *out, const Identity *id, Ctl *ctl) {
    static const WCHAR *const CLASSES[] = {u"HKLM\\SOFTWARE\\Classes", u"HKCU\\Software\\Classes", u"HKLM\\SOFTWARE\\WOW6432Node\\Classes"};
    static const WCHAR *const COMMANDS[] = {u"shell\\open\\command", u"DefaultIcon"};
    for (size_t r = 0; r < COUNT(CLASSES) && !ctl_cancelled(ctl); r++) {
        HKEY root = reg_open(CLASSES[r], KEY_READ);
        if (!root) continue;
        PtrVec subs = reg_subkeys(root);
        for (size_t i = 0; i < subs.n && !ctl_cancelled(ctl); i++) {
            const WCHAR *name = subs.items[i];
            if (name[0] == '.' || wieq(name, u"CLSID") || wieq(name, u"Interface") || wieq(name, u"TypeLib") || wieq(name, u"Applications")) continue;
            for (size_t c = 0; c < COUNT(COMMANDS); c++) {
                WCHAR *sub = path_join(name, COMMANDS[c]);
                HKEY k = reg_open_sub(root, sub);
                xfree(sub);
                if (!k) continue;
                WCHAR *v = value_text(k, u"");
                RegCloseKey(k);
                bool hit = match_path_ref(id, v);
                if (hit) {
                    WCHAR *full = path_join(CLASSES[r], name);
                    left_add(out, LK_KEY, full, NULL, CONF_MEDIUM, LR_FILE_ASSOC, v);
                    xfree(full);
                }
                xfree(v);
                if (hit) break;
            }
        }
        strings_free(&subs);
        // componentes COM
        HKEY clsid = reg_open_sub(root, u"CLSID");
        if (clsid) {
            PtrVec ids = reg_subkeys(clsid);
            for (size_t i = 0; i < ids.n && !ctl_cancelled(ctl); i++) {
                static const WCHAR *const SERVERS[] = {u"InprocServer32", u"LocalServer32"};
                for (size_t s = 0; s < COUNT(SERVERS); s++) {
                    WCHAR *sub = path_join(ids.items[i], SERVERS[s]);
                    HKEY k = reg_open_sub(clsid, sub);
                    xfree(sub);
                    if (!k) continue;
                    WCHAR *v = value_text(k, u"");
                    RegCloseKey(k);
                    bool hit = match_path_ref(id, v);
                    if (hit) {
                        WCHAR *base = path_join(CLASSES[r], u"CLSID"), *full = path_join(base, ids.items[i]);
                        left_add(out, LK_KEY, full, NULL, CONF_MEDIUM, LR_COM, v);
                        xfree(base), xfree(full);
                    }
                    xfree(v);
                    if (hit) break;
                }
            }
            strings_free(&ids);
            RegCloseKey(clsid);
        }
        RegCloseKey(root);
    }
}

// ---------------------------------------------------------------- pastas e atalhos

static void scan_dir_names(LeftList *out, const Identity *id, const WCHAR *root, int thr, bool microsoft_only) {
    if (!root || !dir_exists(root)) return;
    PtrVec v = fs_list(root);
    for (size_t i = 0; i < v.n; i++) {
        DirEntry *e = v.items[i];
        if (!e->is_dir || e->is_link) continue;
        WCHAR *path = path_join(root, e->name);
        if (microsoft_only) {
            if (match_name(id, e->name) == CONF_HIGH) left_add(out, LK_DIR, path, NULL, CONF_HIGH, LR_NAME, NULL);
        } else {
            int c = match_name(id, e->name);
            if (c && c >= thr) {
                left_add(out, LK_DIR, path, NULL, c, LR_NAME, NULL);
            } else if (match_publisher(id, e->name)) {
                PtrVec v2 = fs_list(path);
                bool any = false;
                for (size_t j = 0; j < v2.n; j++) {
                    DirEntry *e2 = v2.items[j];
                    if (!e2->is_dir || e2->is_link) continue;
                    int c2 = match_name(id, e2->name);
                    if (c2 && c2 >= thr) {
                        WCHAR *p2 = path_join(path, e2->name);
                        any = left_add(out, LK_DIR, p2, NULL, c2, LR_NAME, NULL) != NULL || any;
                        xfree(p2);
                    }
                }
                if (any) left_add(out, LK_DIR_IF_EMPTY, path, NULL, CONF_MEDIUM, LR_PUBLISHER, NULL);
                fs_list_free(&v2);
            }
        }
        xfree(path);
    }
    fs_list_free(&v);
}

static void scan_shortcuts_in(LeftList *out, const Identity *id, const WCHAR *dir, int thr, int depth) {
    if (!dir || depth > 3) return;
    PtrVec v = fs_list(dir);
    for (size_t i = 0; i < v.n; i++) {
        DirEntry *e = v.items[i];
        WCHAR *path = path_join(dir, e->name);
        if (e->is_dir) {
            if (!e->is_link) {
                int c = match_name(id, e->name);
                if (c && c >= thr) left_add(out, LK_DIR, path, NULL, c, LR_NAME, NULL);
                else scan_shortcuts_in(out, id, path, thr, depth + 1);
            }
        } else if (wiends(e->name, u".lnk") && e->size < (1u << 20)) {
            uint8_t *data;
            size_t len;
            if (file_read_all(path, &data, &len)) {
                LnkInfo l;
                if (lnk_parse(data, len, &l)) {
                    WCHAR *target = l.target ? expand_env(l.target) : NULL, *wd = l.workdir ? expand_env(l.workdir) : NULL;
                    if (match_path_ref(id, target) || match_path_ref(id, wd)) {
                        left_add(out, LK_FILE, path, NULL, CONF_HIGH, LR_SHORTCUT, target);
                    } else if (target && !file_exists(target)) {
                        // atalho quebrado com o nome do programa
                        WCHAR *base = path_strip_ext(e->name);
                        if (match_name(id, base) == CONF_HIGH) left_add(out, LK_FILE, path, NULL, CONF_MEDIUM, LR_SHORTCUT, target);
                        xfree(base);
                    }
                    xfree(target), xfree(wd);
                    lnk_free(&l);
                }
                xfree(data);
            }
        }
        xfree(path);
    }
    fs_list_free(&v);
}

static void scan_files(LeftList *out, const Identity *id, ScanMode mode) {
    int thr = threshold(mode);
    const Paths *p = &g_paths;
    // pastas de instalação que ainda existem
    for (size_t i = 0; i < id->dirs.n; i++)
        if (dir_exists(id->dirs.items[i])) left_add(out, LK_DIR, id->dirs.items[i], NULL, CONF_HIGH, LR_INSTALL_DIR, NULL);

    WCHAR *local_programs = p->local_appdata ? path_join(p->local_appdata, u"Programs") : NULL;
    const WCHAR *roots[] = {p->program_files, p->program_files_x86, p->common_files, p->common_files_x86, p->program_data,
                            p->appdata,       p->local_appdata,     p->locallow,     local_programs};
    for (size_t i = 0; i < COUNT(roots); i++) scan_dir_names(out, id, roots[i], thr, false);
    if (mode == SCAN_ADVANCED) {
        const WCHAR *more[] = {p->documents, p->public_documents, p->temp};
        for (size_t i = 0; i < COUNT(more); i++) scan_dir_names(out, id, more[i], thr, false);
    }
    if (wistarts(id->publisher, u"Microsoft")) {
        const WCHAR *ms[] = {p->appdata, p->local_appdata, p->program_data};
        for (size_t i = 0; i < COUNT(ms); i++) {
            if (!ms[i]) continue;
            WCHAR *d = path_join(ms[i], u"Microsoft");
            scan_dir_names(out, id, d, thr, true);
            xfree(d);
        }
    }
    if (mode >= SCAN_MODERATE && p->windows) {
        // pastas na raiz da unidade do sistema (C:\Python312, C:\Tools\...): só nome exato
        WCHAR drive[4] = {p->windows[0], ':', '\\', 0};
        PtrVec v = fs_list(drive);
        for (size_t i = 0; i < v.n; i++) {
            DirEntry *e = v.items[i];
            if (e->is_dir && !e->is_link && match_name(id, e->name) == CONF_HIGH) {
                WCHAR *path = path_join(drive, e->name);
                left_add(out, LK_DIR, path, NULL, CONF_HIGH, LR_NAME, NULL);
                xfree(path);
            }
        }
        fs_list_free(&v);
    }
    xfree(local_programs);
}

static void scan_shortcuts(LeftList *out, const Identity *id, ScanMode mode) {
    int thr = threshold(mode);
    const Paths *p = &g_paths;
    const WCHAR *deep[] = {p->programs, p->common_programs};
    for (size_t i = 0; i < COUNT(deep); i++) scan_shortcuts_in(out, id, deep[i], thr, 0);
    const WCHAR *flat[] = {p->desktop, p->public_desktop, p->startup, p->common_startup};
    for (size_t i = 0; i < COUNT(flat); i++) scan_shortcuts_in(out, id, flat[i], CONF_HIGH + 1, 3);  // só os .lnk, sem pastas
}

// Pastas de instalação dos OUTROS programas registrados. Uma sobra que contém uma delas
// (o programa registrou a pasta do editor como pasta de instalação, por exemplo) desce
// para confiança baixa e fica desmarcada: apagá-la levaria outro programa junto.
static PtrVec other_locations(const WCHAR *own_key) {
    PtrVec v = {0};
    for (size_t r = 0; r < COUNT(UNINSTALL_ROOTS); r++) {
        HKEY root = reg_open(UNINSTALL_ROOTS[r], KEY_READ);
        if (!root) continue;
        PtrVec subs = reg_subkeys(root);
        for (size_t i = 0; i < subs.n; i++) {
            WCHAR *path = path_join(UNINSTALL_ROOTS[r], subs.items[i]);
            bool own = own_key && path_eq(path, own_key);
            xfree(path);
            if (own) continue;
            HKEY k = reg_open_sub(root, subs.items[i]);
            if (!k) continue;
            WCHAR *loc = value_text(k, u"InstallLocation");
            if (loc && loc[0]) pv_push(&v, path_norm(loc));
            xfree(loc);
            RegCloseKey(k);
        }
        strings_free(&subs);
        RegCloseKey(root);
    }
    return v;
}

static void demote_shared_dirs(LeftList *out, const PtrVec *others) {
    // (chamada por leftovers_guard_others)
    for (size_t i = 0; i < out->items.n; i++) {
        Leftover *it = out->items.items[i];
        if (it->kind != LK_DIR) continue;
        for (size_t j = 0; j < others->n; j++) {
            const WCHAR *o = others->items[j];
            if (wlen(o) >= 4 && path_is_under_or_eq(o, it->path)) {
                it->conf = CONF_LOW;
                xfree(it->detail);
                it->detail = wfmt(T(S_CONTAINS_OTHER), o);
                break;
            }
        }
    }
}

void leftovers_guard_others(LeftList *l, const WCHAR *own_key) {
    PtrVec others = other_locations(own_key);
    demote_shared_dirs(l, &others);
    strings_free(&others);
}

// ---------------------------------------------------------------- busca completa

void scan_leftovers(LeftList *out, const Identity *id, const WCHAR *uninstall_key, ScanMode mode, Ctl *ctl) {
    log_fmt(u"Busca de sobras: \"%s\" (modo %d, %u pastas do programa)", id->display, (int)mode, (unsigned)id->dirs.n);
    ctl_progress(ctl, T(S_SCAN_REGISTRY));
    scan_uninstall_entries(out, id, uninstall_key, mode);
    scan_reg_names(out, id, threshold(mode));
    if (ctl_cancelled(ctl)) return;
    if (mode >= SCAN_MODERATE) {
        ctl_progress(ctl, T(S_SCAN_STARTUP));
        scan_run(out, id);
        scan_app_paths(out, id);
        scan_shared_dlls(out, id);
        scan_services(out, id);
        scan_applications(out, id);
        scan_shortcuts(out, id, mode);
    }
    if (ctl_cancelled(ctl)) return;
    ctl_progress(ctl, T(S_SCAN_FILES));
    scan_files(out, id, mode);
    leftovers_guard_others(out, uninstall_key);
    if (mode == SCAN_ADVANCED && !ctl_cancelled(ctl)) {
        ctl_progress(ctl, T(S_SCAN_COM));
        scan_classes(out, id, ctl);
    }
    log_fmt(u"Busca de sobras: %u itens encontrados", (unsigned)out->items.n);
}

void leftovers_measure(LeftList *l, Ctl *ctl) {
    ctl_progress(ctl, T(S_SCAN_SIZES));
    for (size_t i = 0; i < l->items.n && !ctl_cancelled(ctl); i++) {
        Leftover *it = l->items.items[i];
        if (it->kind == LK_DIR || it->kind == LK_DIR_IF_EMPTY) it->size = fs_dir_size(it->path, 50000);
        else if (it->kind == LK_FILE) it->size = file_size(it->path);
    }
}

// ---------------------------------------------------------------- exclusão

static WCHAR *safe_file_label(const WCHAR *label) {
    StrBuf b = {0};
    for (const WCHAR *p = label ? label : u""; *p && b.len < 60; p++) {
        WCHAR c = *p;
        if (c < 32 || c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
        sb_addc(&b, c);
    }
    while (b.len && (b.p[b.len - 1] == ' ' || b.p[b.len - 1] == '.')) b.p[--b.len] = 0;
    if (!b.len) sb_add(&b, u"backup");
    return sb_take(&b);
}

static WCHAR *service_key(const WCHAR *name) { return wcat(u"HKLM\\SYSTEM\\CurrentControlSet\\Services\\", name); }

static DWORD delete_service(const WCHAR *name) {
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) return GetLastError();
    SC_HANDLE svc = OpenServiceW(scm, name, SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE);
    DWORD err = ERROR_SUCCESS;
    if (!svc) {
        err = GetLastError();
        CloseServiceHandle(scm);
        return err == ERROR_SERVICE_DOES_NOT_EXIST ? ERROR_SUCCESS : err;
    }
    SERVICE_STATUS st;
    if (ControlService(svc, SERVICE_CONTROL_STOP, &st)) {
        for (int i = 0; i < 40; i++) {  // até 10 s para parar
            if (!QueryServiceStatus(svc, &st) || st.dwCurrentState == SERVICE_STOPPED) break;
            Sleep(250);
        }
    }
    if (!DeleteService(svc)) {
        err = GetLastError();
        if (err == ERROR_SERVICE_MARKED_FOR_DELETE) err = ERROR_SUCCESS;
    }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return err;
}

static size_t depth_of(const WCHAR *p) {
    size_t n = 0;
    for (; *p; p++)
        if (*p == '\\') n++;
    return n;
}

static int cmp_deepest_first(const void *a, const void *b, void *ctx) {
    (void)ctx;
    return (int)depth_of(((const Leftover *)b)->path) - (int)depth_of(((const Leftover *)a)->path);
}

static void finish(Leftover *it, DWORD err, DelResult *res, const WCHAR *what) {
    it->error = err;
    if (err == ERROR_SUCCESS) {
        it->status = LS_DELETED;
        res->deleted++;
        log_fmt(u"  apagado: %s %s%s%s", what, it->path, it->value ? u" -> " : u"", it->value ? it->value : u"");
    } else {
        it->status = LS_FAILED;
        res->failed++;
        WCHAR *e = win_error_text(err);
        log_fmt(u"  FALHOU: %s %s%s%s: %s", what, it->path, it->value ? u" -> " : u"", it->value ? it->value : u"", e);
        xfree(e);
    }
}

static bool still_blocked(const Leftover *it) {
    switch (it->kind) {
    case LK_KEY:
    case LK_KEY_IF_EMPTY: return protect_key_blocked(it->path);
    case LK_VALUE: return protect_value_blocked(it->path);
    case LK_DIR:
    case LK_FILE:
    case LK_DIR_IF_EMPTY: return protect_dir_blocked(it->path);
    case LK_SERVICE: return false;
    }
    return true;
}

void delete_leftovers(LeftList *l, const WCHAR *label, Ctl *ctl, DelResult *res) {
    mem_set(res, 0, sizeof(*res));
    log_fmt(u"Exclusão de sobras: \"%s\"", label);
    PtrVec services = {0}, values = {0}, keys = {0}, keys_if_empty = {0}, files = {0}, dirs_if_empty = {0};
    for (size_t i = 0; i < l->items.n; i++) {
        Leftover *it = l->items.items[i];
        if (!it->checked || it->status == LS_DELETED) continue;
        if (still_blocked(it)) {  // defesa extra: nunca deveria acontecer
            finish(it, ERROR_ACCESS_DENIED, res, u"(protegido)");
            continue;
        }
        switch (it->kind) {
        case LK_SERVICE: pv_push(&services, it); break;
        case LK_VALUE: pv_push(&values, it); break;
        case LK_KEY: pv_push(&keys, it); break;
        case LK_KEY_IF_EMPTY: pv_push(&keys_if_empty, it); break;
        case LK_DIR:
        case LK_FILE: pv_push(&files, it); break;
        case LK_DIR_IF_EMPTY: pv_push(&dirs_if_empty, it); break;
        }
    }

    // 1. backup de tudo o que sai do Registro, num .reg que restaura com um clique duplo
    if (services.n + values.n + keys.n + keys_if_empty.n) {
        ctl_progress(ctl, T(S_DEL_BACKUP));
        StrBuf b = {0};
        regtext_header(&b);
        WCHAR *stamp = now_text(false), *comment = wfmt(u"EXTIRPA %s - %s - %s", EXTIRPA_VERSION, label, stamp);
        regtext_comment(&b, comment);
        xfree(comment), xfree(stamp);
        for (size_t i = 0; i < services.n; i++) {
            WCHAR *k = service_key(((Leftover *)services.items[i])->path);
            reg_export_key(&b, k);
            xfree(k);
        }
        for (size_t i = 0; i < values.n; i++) reg_export_value(&b, ((Leftover *)values.items[i])->path, ((Leftover *)values.items[i])->value);
        for (size_t i = 0; i < keys.n; i++) reg_export_key(&b, ((Leftover *)keys.items[i])->path);
        for (size_t i = 0; i < keys_if_empty.n; i++) regtext_key(&b, ((Leftover *)keys_if_empty.items[i])->path);
        WCHAR *stampf = now_text(true), *safe = safe_file_label(label), *name = wfmt(u"%s - %s.reg", stampf, safe);
        res->backup_path = path_join(g_paths.backups_dir, name);
        xfree(stampf), xfree(safe), xfree(name);
        dir_create_all(g_paths.backups_dir);
        if (!file_write_utf16(res->backup_path, sb_str(&b))) {
            // sem backup, nada do Registro é apagado
            res->backup_failed = true;
            log_fmt(u"  backup NÃO gravado em %s: nada do Registro será apagado", res->backup_path);
            PtrVec *all[] = {&services, &values, &keys, &keys_if_empty};
            for (size_t a = 0; a < COUNT(all); a++)
                for (size_t i = 0; i < all[a]->n; i++) finish(all[a]->items[i], ERROR_CANCELLED, res, u"(sem backup)");
            pv_free(&services), pv_free(&values), pv_free(&keys), pv_free(&keys_if_empty);
        } else {
            log_fmt(u"  backup do Registro: %s", res->backup_path);
        }
        sb_free(&b);
    }

    // 2. serviços (pelo Gerenciador de Serviços: para e remove)
    if (services.n) ctl_progress(ctl, T(S_DEL_SERVICES));
    for (size_t i = 0; i < services.n; i++) finish(services.items[i], delete_service(((Leftover *)services.items[i])->path), res, u"serviço");

    // 3. Registro: valores, depois chaves (as mais fundas primeiro), depois as do editor se vazias
    if (values.n + keys.n) ctl_progress(ctl, T(S_DEL_REGISTRY));
    for (size_t i = 0; i < values.n; i++) {
        Leftover *it = values.items[i];
        finish(it, reg_delete_value(it->path, it->value), res, u"valor");
    }
    pv_sort(&keys, cmp_deepest_first, NULL);
    for (size_t i = 0; i < keys.n; i++) {
        Leftover *it = keys.items[i];
        finish(it, reg_delete_key(it->path), res, u"chave");
    }
    pv_sort(&keys_if_empty, cmp_deepest_first, NULL);
    for (size_t i = 0; i < keys_if_empty.n; i++) {
        Leftover *it = keys_if_empty.items[i];
        if (!reg_exists(it->path)) finish(it, ERROR_SUCCESS, res, u"chave");
        else if (reg_key_empty(it->path)) finish(it, reg_delete_key(it->path), res, u"chave vazia");
        else it->status = LS_KEPT, res->kept++;
    }

    // 4. arquivos e pastas: para a Lixeira
    if (files.n) ctl_progress(ctl, T(S_DEL_FILES));
    for (size_t i = 0; i < files.n && !ctl_cancelled(ctl); i++) {
        Leftover *it = files.items[i];
        DWORD err = file_exists(it->path) ? fs_recycle(it->path) : ERROR_SUCCESS;
        finish(it, err, res, it->kind == LK_DIR ? u"pasta" : u"arquivo");
    }
    pv_sort(&dirs_if_empty, cmp_deepest_first, NULL);
    for (size_t i = 0; i < dirs_if_empty.n; i++) {
        Leftover *it = dirs_if_empty.items[i];
        if (!dir_exists(it->path)) finish(it, ERROR_SUCCESS, res, u"pasta");
        else if (fs_dir_empty(it->path)) finish(it, RemoveDirectoryW(it->path) ? ERROR_SUCCESS : GetLastError(), res, u"pasta vazia");
        else it->status = LS_KEPT, res->kept++;
    }
    log_fmt(u"Exclusão de sobras: %d apagados, %d falharam, %d mantidos", res->deleted, res->failed, res->kept);
    pv_free(&services), pv_free(&values), pv_free(&keys), pv_free(&keys_if_empty), pv_free(&files), pv_free(&dirs_if_empty);
}

void del_result_free(DelResult *r) {
    xfree(r->backup_path);
    r->backup_path = NULL;
}
