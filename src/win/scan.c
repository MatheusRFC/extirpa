// SPDX-License-Identifier: MIT
// Busca de sobras. Cada achado entra com uma confiança:
// - alta (marcado): tem o nome exato do programa, aponta para a pasta dele, ou é a própria
//   entrada dele no Windows;
// - média (desmarcado): nome parecido, ou referência indireta;
// - baixa (desmarcado): ambíguo, pode ser de outro programa.
//
// Três regras protegem os outros programas instalados:
// 1. uma referência a um caminho é de quem tem a pasta MAIS ESPECÍFICA citada nele (um
//    programa dentro da pasta de outro continua sendo dele);
// 2. um nome que outro programa instalado também reivindica (Python 3.11 e 3.12 dividem
//    "Python") desce para baixa, e a busca entra nele atrás só da parte deste programa;
// 3. uma pasta que contém a pasta de outro programa vira "remover se ficar vazia", e o que
//    há dentro dela (menos o outro programa) entra item por item.
#include "scan.h"

#include "appx.h"

#include "../core/cmdline.h"
#include "../core/lnk.h"
#include "../core/pathx.h"
#include "../core/protect.h"
#include "../core/regtext.h"
#include "fsx.h"
#include "i18n.h"
#include "platform.h"
#include "programs.h"
#include "reg.h"

void ctl_progress(Ctl *c, const WCHAR *text) {
    if (c && c->progress) c->progress(c->ctx, text);
}

bool ctl_cancelled(const Ctl *c) { return c && c->cancel; }

typedef struct {
    LeftList *out;
    const Identity *id;
    ScanMode mode;
    int thr;  // confiança mínima de um nome para entrar na lista
    Ctl *ctl;
    PtrVec other_dirs;  // WCHAR*: pastas de outros programas registrados
    PtrVec others;      // Identity*: outros programas instalados
    PtrVec progids;     // WCHAR*: nomes de tipos de arquivo (ProgIDs) do programa
    PtrVec clsids;      // WCHAR*: componentes COM do programa ("{GUID}")
    unsigned flags;     // SCANF_*
} Scan;

static bool name_is_ours(const Scan *sc, const WCHAR *name);
static bool progid_named(const Scan *sc, const WCHAR *progid);
static int exe_name_conf(const Scan *sc, const WCHAR *exe_leaf);

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

// Texto de um valor já lido (REG_SZ, REG_EXPAND_SZ ou REG_MULTI_SZ, com as partes separadas
// por '|'); NULL para os outros tipos.
static WCHAR *regvalue_text(const RegValue *v) {
    if (v->type != REG_SZ && v->type != REG_EXPAND_SZ && v->type != REG_MULTI_SZ) return NULL;
    size_t n = v->size / 2;
    WCHAR *raw = wdupn((const WCHAR *)v->data, n);
    for (size_t i = 0; i + 1 < n; i++)
        if (!raw[i] && raw[i + 1]) raw[i] = '|';
    WCHAR *x = expand_env(raw);
    xfree(raw);
    return x;
}

// A referência no texto é deste programa? (regra 1: a pasta mais específica ganha)
static bool owns(const Scan *sc, const WCHAR *text) {
    if (!text || !text[0]) return false;
    size_t ours = MAX(path_ref_len(&sc->id->dirs, text), path_ref_len(&sc->id->alt_dirs, text));
    if (!ours) return false;
    return ours > path_ref_len(&sc->other_dirs, text);
}

// A referência é de outro programa registrado?
static bool foreign(const Scan *sc, const WCHAR *text) {
    if (!text || !text[0]) return false;
    size_t theirs = path_ref_len(&sc->other_dirs, text);
    if (!theirs) return false;
    return theirs >= MAX(path_ref_len(&sc->id->dirs, text), path_ref_len(&sc->id->alt_dirs, text));
}

static bool vec_has_i(const PtrVec *v, const WCHAR *s) {
    for (size_t i = 0; i < v->n; i++)
        if (wieq(v->items[i], s)) return true;
    return false;
}

static void remember(PtrVec *v, const WCHAR *s) {
    if (!vec_has_i(v, s)) pv_push(v, wdup(s));
}

// O executável do comando não existe mais?
static bool exe_missing(const WCHAR *cmdline) {
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

static bool value_exists(const WCHAR *key, const WCHAR *name) {
    HKEY k = reg_open(key, KEY_QUERY_VALUE);
    if (!k) return false;
    DWORD t, sz = 0;
    bool ok = RegQueryValueExW(k, name, NULL, &t, NULL, &sz) == ERROR_SUCCESS;
    RegCloseKey(k);
    return ok;
}

// O atalho é do programa (destino ou pasta de trabalho dentro da pasta dele)? Atalhos
// "anunciados" do Windows Installer não têm destino, só a pasta de trabalho.
static bool lnk_owned(const Scan *sc, const WCHAR *path) {
    uint8_t *data;
    size_t len;
    bool r = false;
    if (file_size(path) > (1u << 20) || !file_read_all(path, &data, &len)) return false;
    LnkInfo l;
    if (lnk_parse(data, len, &l)) {
        WCHAR *target = l.target ? expand_env(l.target) : NULL, *wd = l.workdir ? expand_env(l.workdir) : NULL;
        r = owns(sc, target) || (!target && owns(sc, wd));
        xfree(target), xfree(wd);
        lnk_free(&l);
    }
    xfree(data);
    return r;
}

// O primeiro executável (ou, sem executável, o atalho) citado no texto tem o nome exato deste programa (e de
// nenhum outro instalado) e não existe mais? É como os rastros de uso de um programa já
// desinstalado aparecem quando o EXTIRPA não sabe a pasta dele. Um atalho que ainda existe
// vale se for do programa.
static bool exe_gone_named(const Scan *sc, const WCHAR *text) {
    if (!text) return false;
    // com executável no texto vale o executável; sem ele, o atalho
    const WCHAR *e = wifind(text, u".exe");
    if (!e) e = wifind(text, u".lnk");
    if (!e) return false;
    const WCHAR *start = text;
    for (const WCHAR *p = text; p < e; p++)
        if (*p == '|' || *p == '"') start = p + 1;
    WCHAR *raw = wdupn(start, (size_t)(e + 4 - start)), *t = wtrim_dup(raw);
    bool r = false;
    if (wlen(t) > 7 && t[1] == ':' && t[2] == '\\') {
        WCHAR *base = path_strip_ext(path_leaf(t));
        if (name_is_ours(sc, base)) r = !file_exists(t) || (wiends(t, u".lnk") && lnk_owned(sc, t));
        xfree(base);
    }
    xfree(raw), xfree(t);
    return r;
}

// ---------------------------------------------------------------- outros programas instalados

static const WCHAR *const UNINSTALL_ROOTS[] = {
    u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
    u"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
    u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
};

// Entrada de desinstalação que é deste programa (a própria, ou outra que ficou para trás).
static bool own_entry(const LeftList *l, const WCHAR *key) {
    for (size_t i = 0; i < l->items.n; i++) {
        const Leftover *it = l->items.items[i];
        if (it->reason == LR_UNINSTALL_KEY && it->kind == LK_KEY && path_eq(it->path, key)) return true;
    }
    return false;
}

static void load_others(Scan *sc, const WCHAR *own_key) {
    // programas visíveis: nome e pastas, como na lista principal
    PtrVec progs = programs_load();
    programs_load_msi(&progs);
    for (size_t i = 0; i < progs.n; i++) {
        Program *p = progs.items[i];
        if ((own_key && path_eq(p->key_path, own_key)) || own_entry(sc->out, p->key_path)) continue;
        Identity *o = program_identity(p);
        identity_set_version(o, p->version);
        for (size_t d = 0; d < o->dirs.n; d++) remember(&sc->other_dirs, o->dirs.items[d]);
        pv_push(&sc->others, o);
    }
    programs_free(&progs);
    // entradas ocultas (componentes): só a pasta de instalação, que também não pode ser apagada
    for (size_t r = 0; r < COUNT(UNINSTALL_ROOTS); r++) {
        HKEY root = reg_open(UNINSTALL_ROOTS[r], KEY_READ);
        if (!root) continue;
        PtrVec subs = reg_subkeys(root);
        for (size_t i = 0; i < subs.n; i++) {
            WCHAR *path = path_join(UNINSTALL_ROOTS[r], subs.items[i]);
            bool skip = (own_key && path_eq(path, own_key)) || own_entry(sc->out, path);
            xfree(path);
            if (skip) continue;
            HKEY k = reg_open_sub(root, subs.items[i]);
            if (!k) continue;
            WCHAR *loc = value_text(k, u"InstallLocation");
            if (loc && loc[0]) {
                WCHAR *n = path_norm(loc);
                if (wlen(n) >= 4 && !protect_dir_blocked(n)) remember(&sc->other_dirs, n);
                xfree(n);
            }
            xfree(loc);
            RegCloseKey(k);
        }
        strings_free(&subs);
        RegCloseKey(root);
    }
}

static void free_others(Scan *sc) {
    strings_free(&sc->other_dirs);
    for (size_t i = 0; i < sc->others.n; i++) identity_free(sc->others.items[i]);
    pv_free(&sc->others);
    strings_free(&sc->progids);
    strings_free(&sc->clsids);
}

// ---------------------------------------------------------------- entradas de desinstalação

static void scan_uninstall_entries(Scan *sc, const WCHAR *own_key) {
    if (own_key && reg_exists(own_key)) left_add(sc->out, LK_KEY, own_key, NULL, CONF_HIGH, LR_UNINSTALL_KEY, NULL);
    if (sc->mode < SCAN_MODERATE || !sc->id->dirs.n) return;
    // Outras entradas deste programa que ficaram para trás (outra versão, outro idioma, um
    // componente do mesmo editor): apontam para a pasta dele e têm o nome ou o editor dele.
    for (size_t r = 0; r < COUNT(UNINSTALL_ROOTS); r++) {
        HKEY root = reg_open(UNINSTALL_ROOTS[r], KEY_READ);
        if (!root) continue;
        PtrVec subs = reg_subkeys(root);
        for (size_t i = 0; i < subs.n; i++) {
            WCHAR *path = path_join(UNINSTALL_ROOTS[r], subs.items[i]);
            HKEY k = (own_key && path_eq(path, own_key)) ? NULL : reg_open_sub(root, subs.items[i]);
            if (k) {
                WCHAR *name = reg_get_str(k, u"DisplayName"), *pub = reg_get_str(k, u"Publisher");
                WCHAR *loc = value_text(k, u"InstallLocation"), *un = value_text(k, u"UninstallString");
                bool ref = match_path_ref(sc->id, loc) || match_path_ref(sc->id, un);
                bool same = (name && match_name(sc->id, name) >= CONF_MEDIUM) || (pub && match_publisher(sc->id, pub));
                if (ref && same) left_add(sc->out, LK_KEY, path, NULL, CONF_MEDIUM, LR_UNINSTALL_KEY, name);
                xfree(name), xfree(pub), xfree(loc), xfree(un);
                RegCloseKey(k);
            }
            xfree(path);
        }
        strings_free(&subs);
        RegCloseKey(root);
    }
}

// ---------------------------------------------------------------- Registro por nome

static const WCHAR *const SOFTWARE_ROOTS[] = {u"HKLM\\SOFTWARE", u"HKLM\\SOFTWARE\\WOW6432Node", u"HKCU\\Software", u"HKCU\\Software\\WOW6432Node"};
static const WCHAR *const SKIP_L1[] = {u"Classes", u"Microsoft", u"Policies", u"WOW6432Node", u"Clients", u"RegisteredApplications"};

static bool skip_l1(const WCHAR *name) {
    for (size_t i = 0; i < COUNT(SKIP_L1); i++)
        if (wieq(name, SKIP_L1[i])) return true;
    return false;
}

static void scan_reg_names(Scan *sc) {
    const Identity *id = sc->id;
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
                            left_add(sc->out, LK_KEY, p2, NULL, CONF_HIGH, LR_NAME, NULL);
                            xfree(p2);
                        }
                    strings_free(&subs2);
                    RegCloseKey(k);
                }
            } else if (!skip_l1(s)) {
                int c = match_name(id, s);
                if (c && c >= sc->thr) {
                    left_add(sc->out, LK_KEY, path, NULL, c, LR_NAME, NULL);
                } else if (match_publisher(id, s)) {
                    // chave do editor: procura o programa um nível abaixo
                    HKEY k = reg_open_sub(root, s);
                    if (k) {
                        PtrVec subs2 = reg_subkeys(k);
                        bool any = false;
                        for (size_t j = 0; j < subs2.n; j++) {
                            int c2 = match_name(id, subs2.items[j]);
                            if (c2 < CONF_HIGH && match_name_versioned(id, subs2.items[j])) c2 = CONF_HIGH;
                            if (c2 && c2 >= sc->thr) {
                                WCHAR *p2 = path_join(path, subs2.items[j]);
                                any = left_add(sc->out, LK_KEY, p2, NULL, c2, LR_NAME, NULL) != NULL || any;
                                xfree(p2);
                            }
                        }
                        if (any) left_add(sc->out, LK_KEY_IF_EMPTY, path, NULL, CONF_MEDIUM, LR_PUBLISHER, NULL);
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

// ---------------------------------------------------------------- inicialização, App Paths, DLLs, serviços

static void scan_run(Scan *sc) {
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
            WCHAR *data = regvalue_text(v);
            if (!data) continue;
            int conf = CONF_NONE;
            if (owns(sc, data)) conf = CONF_HIGH;
            else if (match_name(sc->id, v->name) == CONF_HIGH && exe_missing(data)) conf = CONF_MEDIUM;
            if (conf) {
                left_add(sc->out, LK_VALUE, RUN[r].key, v->name, conf, LR_RUN, data);
                if (RUN[r].approved && value_exists(RUN[r].approved, v->name)) left_add(sc->out, LK_VALUE, RUN[r].approved, v->name, conf, LR_RUN, NULL);
            }
            xfree(data);
        }
        reg_values_free(&vals);
        RegCloseKey(k);
    }
    // Registros que o Windows guarda por entrada de inicialização e que ficam depois que o
    // desinstalador apaga a entrada: o "ativado/desativado" (StartupApproved) e o aviso de
    // "novo aplicativo de inicialização" (RunNotification, "StartupTNoti" + nome)
    static const WCHAR *const ORPHAN[] = {u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run",
                                          u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run32",
                                          u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run",
                                          u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\RunNotification"};
    for (size_t o = 0; o < COUNT(ORPHAN); o++) {
        HKEY k = reg_open(ORPHAN[o], KEY_READ);
        if (!k) continue;
        PtrVec names = reg_value_names(k);
        bool noti = wiends(ORPHAN[o], u"RunNotification");
        for (size_t i = 0; i < names.n; i++) {
            const WCHAR *n = names.items[i];
            if (noti && !wistarts(n, u"StartupTNoti")) continue;
            const WCHAR *run_name = noti ? n + 12 : n;
            bool alive = false, ours = false;
            for (size_t r = 0; r < COUNT(RUN); r++) {
                if (value_exists(RUN[r].key, run_name)) alive = true;
                for (size_t j = 0; j < sc->out->items.n && !ours; j++) {
                    const Leftover *it = sc->out->items.items[j];
                    ours = it->kind == LK_VALUE && it->reason == LR_RUN && path_eq(it->path, RUN[r].key) && wieq(it->value, run_name);
                }
            }
            int conf = ours ? CONF_HIGH : CONF_NONE;
            if (!conf && !alive) {
                conf = match_name(sc->id, run_name);
                if (conf == CONF_HIGH && !name_is_ours(sc, run_name)) conf = CONF_LOW;
            }
            if (conf && conf >= sc->thr) left_add(sc->out, LK_VALUE, ORPHAN[o], n, conf, LR_RUN, NULL);
        }
        strings_free(&names);
        RegCloseKey(k);
    }
}

static void scan_app_paths(Scan *sc) {
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
            if (owns(sc, def) || (!def && owns(sc, path)) || exe_gone_named(sc, def)) {
                WCHAR *full = path_join(ROOTS[r], subs.items[i]);
                left_add(sc->out, LK_KEY, full, NULL, CONF_HIGH, LR_APP_PATHS, def);
                xfree(full);
            }
            xfree(def), xfree(path);
            RegCloseKey(k);
        }
        strings_free(&subs);
        RegCloseKey(root);
    }
}

static void scan_shared_dlls(Scan *sc) {
    static const WCHAR *const ROOTS[] = {
        u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\SharedDLLs",
        u"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\SharedDLLs",
    };
    for (size_t r = 0; r < COUNT(ROOTS); r++) {
        HKEY k = reg_open(ROOTS[r], KEY_READ);
        if (!k) continue;
        PtrVec names = reg_value_names(k);
        for (size_t i = 0; i < names.n; i++) {
            WCHAR *x = expand_env(names.items[i]);
            if (owns(sc, x)) left_add(sc->out, LK_VALUE, ROOTS[r], names.items[i], CONF_HIGH, LR_SHARED_DLL, NULL);
            xfree(x);
        }
        strings_free(&names);
        RegCloseKey(k);
    }
}

static void scan_services(Scan *sc) {
    static const WCHAR SERVICES[] = u"HKLM\\SYSTEM\\CurrentControlSet\\Services";
    HKEY root = reg_open(SERVICES, KEY_READ);
    if (!root) return;
    PtrVec subs = reg_subkeys(root);
    for (size_t i = 0; i < subs.n; i++) {
        HKEY k = reg_open_sub(root, subs.items[i]);
        if (!k) continue;
        WCHAR *img = value_text(k, u"ImagePath");
        if (img) {
            if (owns(sc, img)) left_add(sc->out, LK_SERVICE, subs.items[i], NULL, CONF_HIGH, LR_SERVICE, img);
            else if (match_name(sc->id, subs.items[i]) == CONF_HIGH && exe_missing(img))
                left_add(sc->out, LK_SERVICE, subs.items[i], NULL, CONF_MEDIUM, LR_SERVICE, img);
        }
        xfree(img);
        RegCloseKey(k);
    }
    strings_free(&subs);
    RegCloseKey(root);
}

#define UFH_SHC_KEY u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\UFH\\SHC"

// Valores com o caminho de um executável no nome, onde o Windows guarda rastros de uso.
static void scan_usage(Scan *sc) {
    static const WCHAR *const KEYS[] = {
        u"HKCU\\Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\Shell\\MuiCache",
        u"HKCU\\Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Compatibility Assistant\\Store",
        u"HKCU\\Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Compatibility Assistant\\Persisted",
        u"HKCU\\Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers",
        u"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers",
    };
    for (size_t r = 0; r < COUNT(KEYS); r++) {
        HKEY k = reg_open(KEYS[r], KEY_READ);
        if (!k) continue;
        PtrVec names = reg_value_names(k);
        for (size_t i = 0; i < names.n; i++)
            if (owns(sc, names.items[i]) || exe_gone_named(sc, names.items[i])) left_add(sc->out, LK_VALUE, KEYS[r], names.items[i], CONF_HIGH, LR_USAGE, NULL);
        strings_free(&names);
        RegCloseKey(k);
    }
    // Atalhos usados pelo menu Iniciar (UFH\SHC): um valor por atalho, com o .lnk e o
    // executável no conteúdo
    HKEY k = reg_open(UFH_SHC_KEY, KEY_READ);
    if (!k) return;
    PtrVec vals = reg_values(k);
    for (size_t i = 0; i < vals.n; i++) {
        RegValue *v = vals.items[i];
        WCHAR *t = regvalue_text(v);
        if (t && (owns(sc, t) || exe_gone_named(sc, t))) left_add(sc->out, LK_VALUE, UFH_SHC_KEY, v->name, CONF_HIGH, LR_USAGE, t);
        xfree(t);
    }
    reg_values_free(&vals);
    RegCloseKey(k);
}

// Caminho gravado com uma pasta conhecida na frente ("{7C5A40EF-...}\WinSCP\WinSCP.exe", como
// fazem o UserAssist e as Jump Lists) com a pasta de verdade. Sem pasta conhecida, uma cópia.
static WCHAR *expand_known_folder(const WCHAR *s) {
    static const struct {
        const WCHAR *guid;
        int which;
    } KF[] = {
        {u"{6D809377-6AF0-444B-8957-A3773F02200E}", 0},  // FOLDERID_ProgramFilesX64
        {u"{905E63B6-C1BF-494E-B29C-65B732D3D21A}", 0},  // FOLDERID_ProgramFiles
        {u"{7C5A40EF-A0FB-4BFC-874A-C0F2E0B9FA8E}", 1},  // FOLDERID_ProgramFilesX86
        {u"{6365D5A7-0F0D-45E5-87F6-0DA56B6A4F7D}", 2},  // FOLDERID_ProgramFilesCommonX64
        {u"{DE974D24-D9C6-4D3E-BF91-F4455120B917}", 3},  // FOLDERID_ProgramFilesCommonX86
        {u"{F1B32785-6FBA-4FCF-9D55-7B8E7F157091}", 4},  // FOLDERID_LocalAppData
        {u"{3EB685DB-65F9-4CF6-A03A-E3EF65729F3D}", 5},  // FOLDERID_RoamingAppData
        {u"{62AB5D82-FDC1-4DC3-A9DD-070D1D495D97}", 6},  // FOLDERID_ProgramData
        {u"{5CD7AEE2-2219-4A67-B85D-6C9CE15660CB}", 7},  // FOLDERID_UserProgramFiles
        {u"{0139D44E-6AFE-49F2-8690-3DAFCAE6FFB8}", 8},  // FOLDERID_CommonPrograms (atalhos do menu Iniciar)
        {u"{A77F5D77-2E2B-44C3-A6A2-ABA601054A51}", 9},  // FOLDERID_Programs
        {u"{B4BFCC3A-DB2C-424C-B029-7FE99A87C641}", 10}, // FOLDERID_Desktop
        {u"{C4AA340D-F20F-4863-AFEF-F87EF2E6BA25}", 11}, // FOLDERID_PublicDesktop
    };
    const Paths *p = &g_paths;
    for (size_t i = 0; i < COUNT(KF); i++) {
        size_t lg = wlen(KF[i].guid);
        if (!wistarts(s, KF[i].guid) || (s[lg] != '\\' && s[lg] != 0)) continue;
        WCHAR *base = NULL;
        switch (KF[i].which) {
        case 0: base = wdup(p->program_files); break;
        case 1: base = wdup(p->program_files_x86); break;
        case 2: base = wdup(p->common_files); break;
        case 3: base = wdup(p->common_files_x86); break;
        case 4: base = wdup(p->local_appdata); break;
        case 5: base = wdup(p->appdata); break;
        case 6: base = wdup(p->program_data); break;
        case 7: base = p->local_appdata ? path_join(p->local_appdata, u"Programs") : NULL; break;
        case 8: base = wdup(p->common_programs); break;
        case 9: base = wdup(p->programs); break;
        case 10: base = wdup(p->desktop); break;
        case 11: base = wdup(p->public_desktop); break;
        }
        if (!base) return wdup(s);
        WCHAR *r = s[lg] ? path_join(base, s + lg + 1) : wdup(base);
        xfree(base);
        return r;
    }
    return wdup(s);
}

static WCHAR *rot13(const WCHAR *s) {
    WCHAR *r = wdup(s);
    for (WCHAR *p = r; *p; p++) {
        if (*p >= 'a' && *p <= 'z') *p = (WCHAR)('a' + (*p - 'a' + 13) % 26);
        else if (*p >= 'A' && *p <= 'Z') *p = (WCHAR)('A' + (*p - 'A' + 13) % 26);
    }
    return r;
}

// Jump Lists da busca e UserAssist (contagem de programas abertos, nomes em ROT13): um valor
// por programa, com o caminho do executável no nome.
static void scan_usage_named(Scan *sc) {
    static const WCHAR JUMP[] = u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Search\\JumplistData";
    HKEY k = reg_open(JUMP, KEY_READ);
    if (k) {
        PtrVec names = reg_value_names(k);
        for (size_t i = 0; i < names.n; i++) {
            WCHAR *x = expand_known_folder(names.items[i]);
            if (owns(sc, x) || exe_gone_named(sc, x) || (!wfindc(x, '\\') && progid_named(sc, x))) left_add(sc->out, LK_VALUE, JUMP, names.items[i], CONF_HIGH, LR_USAGE, x);
            xfree(x);
        }
        strings_free(&names);
        RegCloseKey(k);
    }
    static const WCHAR UA[] = u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\UserAssist";
    HKEY ua = reg_open(UA, KEY_READ);
    if (!ua) return;
    PtrVec guids = reg_subkeys(ua);
    for (size_t g = 0; g < guids.n; g++) {
        WCHAR *sub = path_join(guids.items[g], u"Count"), *key = path_join(UA, sub);
        HKEY c = reg_open_sub(ua, sub);
        if (c) {
            PtrVec names = reg_value_names(c);
            for (size_t i = 0; i < names.n; i++) {
                WCHAR *plain = rot13(names.items[i]), *x = expand_known_folder(plain);
                // caminho de executável, ou ID de aplicativo ("FileZilla.Client.AppID")
                if (owns(sc, x) || exe_gone_named(sc, x) || (!wfindc(plain, '\\') && progid_named(sc, plain)))
                    left_add(sc->out, LK_VALUE, key, names.items[i], CONF_HIGH, LR_USAGE, x);
                xfree(plain), xfree(x);
            }
            strings_free(&names);
            RegCloseKey(c);
        }
        xfree(sub), xfree(key);
    }
    strings_free(&guids);
    RegCloseKey(ua);
}

// Reprodução automática (AutoPlay): Handlers\<nome> com ícone ou comando do programa, e as
// entradas com esse nome nos eventos (EventHandlers\<evento>, um valor por handler).
static void scan_autoplay(Scan *sc) {
    static const WCHAR *const ROOTS[] = {u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\AutoplayHandlers",
                                         u"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Explorer\\AutoplayHandlers",
                                         u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\AutoplayHandlers"};
    PtrVec mine = {0};
    for (size_t r = 0; r < COUNT(ROOTS); r++) {
        WCHAR *hk = path_join(ROOTS[r], u"Handlers");
        HKEY h = reg_open(hk, KEY_READ);
        if (h) {
            PtrVec subs = reg_subkeys(h);
            for (size_t i = 0; i < subs.n; i++) {
                HKEY k = reg_open_sub(h, subs.items[i]);
                if (!k) continue;
                PtrVec vals = reg_values(k);
                const WCHAR *hit = NULL;
                WCHAR *hit_text = NULL;
                for (size_t j = 0; j < vals.n && !hit; j++) {
                    RegValue *v = vals.items[j];
                    WCHAR *t = regvalue_text(v);
                    if (owns(sc, t)) hit = v->name, hit_text = t;
                    else if (t && (wieq(v->name, u"InvokeProgID") || wieq(v->name, u"ProgID")) && vec_has_i(&sc->progids, t)) hit = v->name, hit_text = t;
                    else xfree(t);
                }
                if (hit) {
                    WCHAR *p = path_join(hk, subs.items[i]);
                    left_add(sc->out, LK_KEY, p, NULL, CONF_HIGH, LR_SHELL_EXT, hit_text);
                    remember(&mine, subs.items[i]);
                    xfree(p);
                }
                xfree(hit_text);
                reg_values_free(&vals);
                RegCloseKey(k);
            }
            strings_free(&subs);
            RegCloseKey(h);
        }
        xfree(hk);
    }
    // o nome do handler também pode ter ficado só nos eventos (o desinstalador apagou o handler)
    for (size_t r = 0; r < COUNT(ROOTS); r++) {
        WCHAR *ek = path_join(ROOTS[r], u"EventHandlers");
        HKEY e = reg_open(ek, KEY_READ);
        if (e) {
            PtrVec events = reg_subkeys(e);
            for (size_t i = 0; i < events.n; i++) {
                HKEY k = reg_open_sub(e, events.items[i]);
                if (!k) continue;
                PtrVec names = reg_value_names(k);
                WCHAR *key = path_join(ek, events.items[i]);
                for (size_t j = 0; j < names.n; j++) {
                    const WCHAR *n = names.items[j];
                    if (vec_has_i(&mine, n)) {
                        left_add(sc->out, LK_VALUE, key, n, CONF_HIGH, LR_REFERENCE, NULL);
                    } else {
                        // handler que não existe mais, com o nome começando pelo do programa
                        // ("VLCPlayBlurayOnArrival" do VLC): desmarcado
                        WCHAR *hp = path_join(ROOTS[r], u"Handlers"), *hpath = path_join(hp, n);
                        WCHAR *nn = norm_alnum(n);
                        bool prefix = false;
                        for (size_t x = 0; x < sc->id->exact.n && !prefix; x++) {
                            const WCHAR *ex = sc->id->exact.items[x];
                            prefix = wlen(ex) >= 3 && wistarts(nn, ex) && wlen(nn) > wlen(ex);
                        }
                        if (prefix && !reg_exists(hpath)) left_add(sc->out, LK_VALUE, key, n, CONF_MEDIUM, LR_NAME, NULL);
                        xfree(hp), xfree(hpath), xfree(nn);
                    }
                }
                xfree(key);
                strings_free(&names);
                RegCloseKey(k);
            }
            strings_free(&events);
            RegCloseKey(e);
        }
        xfree(ek);
    }
    strings_free(&mine);
}

// Pacote esparso: o manifesto (AppxManifest.xml na pasta do pacote) permite conteúdo externo,
// ou seja, os arquivos são os do programa comum, fora de WindowsApps.
static bool appx_sparse(const WCHAR *dir) {
    WCHAR *m = path_join(dir, u"AppxManifest.xml");
    uint8_t *d = NULL;
    size_t n = 0;
    bool r = false;
    if (file_size(m) < 4u * 1024 * 1024 && file_read_all(m, &d, &n)) {
        static const char NEEDLE[] = "AllowExternalContent>true<";
        size_t ln = sizeof(NEEDLE) - 1;
        for (size_t i = 0; !r && i + ln <= n; i++) r = mem_cmp(d + i, NEEDLE, ln) == 0;
        xfree(d);
    }
    xfree(m);
    return r;
}

// Pacotes do Windows registrados pelo programa. O caso típico é o pacote "esparso" que um
// programa comum registra para aparecer no menu de contexto do Windows 11 (o Notepad++ faz
// isso): o desinstalador às vezes não o remove, e as chaves dele ficam espalhadas por áreas do
// Registro que só o próprio Windows deve mexer. Sai pelo Remove-AppxPackage.
static void scan_appx(Scan *sc) {
    HKEY root = reg_open(APPX_PACKAGES_KEY, KEY_READ);
    if (!root) return;
    PtrVec subs = reg_subkeys(root);
    for (size_t i = 0; i < subs.n; i++) {
        const WCHAR *full = subs.items[i];
        if (!left_appx_name_ok(full)) continue;
        const WCHAR *us = wfindc(full, '_');
        WCHAR *name = us ? wdupn(full, (size_t)(us - full)) : wdup(full);
        HKEY k = reg_open_sub(root, full);
        WCHAR *path = k ? value_text(k, u"Path") : NULL;
        if (k) RegCloseKey(k);
        int conf = CONF_NONE;
        if (owns(sc, path)) conf = CONF_HIGH;
        else if (name_is_ours(sc, name) && !foreign(sc, path)) {
            // só pelo nome: desmarcado se for um aplicativo da Loja de verdade (arquivos em
            // WindowsApps); marcado se a pasta já não existe (registro órfão) ou está fora dali
            // (pacote esparso, cujos arquivos são os do próprio programa)
            bool store = path && wifind(path, u"\\WindowsApps\\") && dir_exists(path) && !appx_sparse(path);
            conf = store ? CONF_MEDIUM : CONF_HIGH;
        }
        if (conf) left_add(sc->out, LK_APPX, full, NULL, conf, LR_PACKAGE, path);
        xfree(name), xfree(path);
    }
    strings_free(&subs);
    RegCloseKey(root);
}

// Pastas do programa na variável PATH (do sistema e do usuário).
static void scan_path_var(Scan *sc) {
    static const WCHAR *const KEYS[] = {ENV_KEY_MACHINE, ENV_KEY_USER};
    for (size_t r = 0; r < COUNT(KEYS); r++) {
        HKEY k = reg_open(KEYS[r], KEY_READ);
        if (!k) continue;
        WCHAR *path = reg_get_str(k, u"Path");
        RegCloseKey(k);
        for (const WCHAR *p = path; p && *p;) {
            const WCHAR *e = wfindc(p, ';');
            if (!e) e = p + wlen(p);
            WCHAR *raw = wdupn(p, (size_t)(e - p)), *entry = wtrim_dup(raw), *x = expand_env(entry), *n = path_norm(x);
            if (n[0] && owns(sc, n) && !protect_dir_blocked(n)) left_add(sc->out, LK_PATHENTRY, KEYS[r], entry, CONF_HIGH, LR_PATH_VAR, NULL);
            xfree(raw), xfree(entry), xfree(x), xfree(n);
            p = *e ? e + 1 : e;
        }
        xfree(path);
    }
}

// Regras do Firewall do Windows: o valor é "v2.x|Action=...|App=<programa>|...".
static void scan_firewall(Scan *sc) {
    HKEY k = reg_open(FIREWALL_RULES_KEY, KEY_READ);
    if (!k) return;
    PtrVec vals = reg_values(k);
    for (size_t i = 0; i < vals.n; i++) {
        RegValue *v = vals.items[i];
        if (v->type != REG_SZ) continue;
        WCHAR *data = wdupn((const WCHAR *)v->data, v->size / 2);
        const WCHAR *app = wifind(data, u"|App=");
        if (app) {
            app += 5;
            const WCHAR *e = wfindc(app, '|');
            WCHAR *raw = wdupn(app, e ? (size_t)(e - app) : wlen(app)), *x = expand_env(raw);
            if (owns(sc, x)) left_add(sc->out, LK_FWRULE, v->name, NULL, CONF_HIGH, LR_FIREWALL, x);
            xfree(raw), xfree(x);
        }
        xfree(data);
    }
    reg_values_free(&vals);
    RegCloseKey(k);
}

// ---------------------------------------------------------------- tipos de arquivo, COM e menus do Explorer

static const WCHAR *const CLASSES_ROOTS[] = {u"HKLM\\SOFTWARE\\Classes", u"HKCU\\Software\\Classes", u"HKLM\\SOFTWARE\\WOW6432Node\\Classes"};

// Alguma linha de comando ou ícone da chave (DefaultIcon e shell\<verbo>\command) é do programa?
static WCHAR *owned_command(Scan *sc, HKEY k) {
    HKEY ic = reg_open_sub(k, u"DefaultIcon");
    if (ic) {
        WCHAR *v = value_text(ic, u"");
        RegCloseKey(ic);
        if (owns(sc, v)) return v;
        xfree(v);
    }
    HKEY shell = reg_open_sub(k, u"shell");
    if (!shell) return NULL;
    WCHAR *hit = NULL;
    PtrVec verbs = reg_subkeys(shell);
    for (size_t i = 0; i < verbs.n && !hit && i < 64; i++) {
        WCHAR *sub = path_join(verbs.items[i], u"command");
        HKEY c = reg_open_sub(shell, sub);
        xfree(sub);
        if (!c) continue;
        WCHAR *v = value_text(c, u"");
        RegCloseKey(c);
        if (owns(sc, v)) hit = v;
        else xfree(v);
    }
    strings_free(&verbs);
    RegCloseKey(shell);
    return hit;
}

// Primeiro comando (shell\<verbo>\command) ou ícone da chave, ou NULL.
static WCHAR *any_command(HKEY k) {
    HKEY shell = reg_open_sub(k, u"shell");
    WCHAR *r = NULL;
    if (shell) {
        PtrVec verbs = reg_subkeys(shell);
        for (size_t i = 0; i < verbs.n && !r && i < 64; i++) {
            WCHAR *sub = path_join(verbs.items[i], u"command");
            HKEY c = reg_open_sub(shell, sub);
            xfree(sub);
            if (!c) continue;
            r = value_text(c, u"");
            RegCloseKey(c);
        }
        strings_free(&verbs);
        RegCloseKey(shell);
    }
    if (!r) {
        HKEY ic = reg_open_sub(k, u"DefaultIcon");
        if (ic) {
            r = value_text(ic, u"");
            RegCloseKey(ic);
        }
    }
    return r;
}

// A chave tem comando ou ícone que NÃO é do programa (um verbo de outro, o ícone do Windows)?
static bool shared_class(Scan *sc, HKEY k) {
    bool other = false;
    HKEY ic = reg_open_sub(k, u"DefaultIcon");
    if (ic) {
        WCHAR *v = value_text(ic, u"");
        RegCloseKey(ic);
        other = v && v[0] && !owns(sc, v);
        xfree(v);
    }
    HKEY shell = reg_open_sub(k, u"shell");
    if (shell) {
        PtrVec verbs = reg_subkeys(shell);
        for (size_t i = 0; i < verbs.n && !other && i < 64; i++) {
            WCHAR *sub = path_join(verbs.items[i], u"command");
            HKEY c = reg_open_sub(shell, sub);
            xfree(sub);
            if (!c) continue;
            WCHAR *v = value_text(c, u"");
            RegCloseKey(c);
            other = v && v[0] && !owns(sc, v);
            xfree(v);
        }
        strings_free(&verbs);
        RegCloseKey(shell);
    }
    return other;
}

// Os verbos (shell\<verbo>) do programa dentro de uma classe que não é dele.
static void add_owned_verbs(Scan *sc, HKEY k, const WCHAR *root, const WCHAR *cls) {
    HKEY shell = reg_open_sub(k, u"shell");
    if (!shell) return;
    PtrVec verbs = reg_subkeys(shell);
    for (size_t i = 0; i < verbs.n && i < 64; i++) {
        WCHAR *sub = path_join(verbs.items[i], u"command");
        HKEY c = reg_open_sub(shell, sub);
        xfree(sub);
        if (!c) continue;
        WCHAR *v = value_text(c, u"");
        RegCloseKey(c);
        if (owns(sc, v)) {
            WCHAR *full = wfmt(u"%s\\%s\\shell\\%s", root, cls, verbs.items[i]);
            left_add(sc->out, LK_KEY, full, NULL, CONF_HIGH, LR_SHELL_EXT, v);
            xfree(full);
        }
        xfree(v);
    }
    strings_free(&verbs);
    RegCloseKey(shell);
}

static bool skip_class(const WCHAR *name) {
    static const WCHAR *const SKIP[] = {u"CLSID", u"Interface", u"TypeLib", u"AppID", u"Installer", u"Local Settings", u"WOW6432Node", u"*", u"Directory",
                                        u"Folder", u"Drive", u"AllFilesystemObjects", u"SystemFileAssociations", u"Applications", u"MIME", u"Protocols"};
    if (name[0] == '.') return true;
    for (size_t i = 0; i < COUNT(SKIP); i++)
        if (wieq(name, SKIP[i])) return true;
    return false;
}

static void scan_classes(Scan *sc) {
    for (size_t r = 0; r < COUNT(CLASSES_ROOTS) && !ctl_cancelled(sc->ctl); r++) {
        HKEY root = reg_open(CLASSES_ROOTS[r], KEY_READ);
        if (!root) continue;
        // tipos de arquivo (ProgIDs) e protocolos cujos comandos ou ícone apontam para o programa
        PtrVec subs = reg_subkeys(root);
        for (size_t i = 0; i < subs.n && !ctl_cancelled(sc->ctl); i++) {
            const WCHAR *name = subs.items[i];
            if (skip_class(name)) continue;
            HKEY k = reg_open_sub(root, name);
            if (!k) continue;
            WCHAR *hit = owned_command(sc, k);
            int conf = hit ? CONF_HIGH : CONF_NONE;
            if (hit && !progid_named(sc, name) && shared_class(sc, k)) {
                // classe de outro dono (AudioCD, DVD...) em que o programa pôs um verbo: só o verbo sai
                add_owned_verbs(sc, k, CLASSES_ROOTS[r], name);
                xfree(hit);
                hit = NULL;
                conf = CONF_NONE;
            }
            if (!hit && conf == CONF_NONE && progid_named(sc, name)) {
                // tipo de arquivo com o nome do programa ("GridCraft.Workbook"): marcado se o
                // executável dele não existe mais (ou se não tem comando), senão só sugerido
                hit = any_command(k);
                conf = (!hit || exe_missing(hit)) ? CONF_HIGH : CONF_MEDIUM;
            }
            RegCloseKey(k);
            if (conf && conf >= sc->thr) {
                WCHAR *full = path_join(CLASSES_ROOTS[r], name);
                if (left_add(sc->out, LK_KEY, full, NULL, conf, LR_FILE_ASSOC, hit) && conf == CONF_HIGH) remember(&sc->progids, name);
                xfree(full);
            }
            xfree(hit);
        }
        strings_free(&subs);
        // Applications\<programa.exe>
        HKEY apps = reg_open_sub(root, u"Applications");
        if (apps) {
            PtrVec names = reg_subkeys(apps);
            for (size_t i = 0; i < names.n; i++) {
                HKEY k = reg_open_sub(apps, names.items[i]);
                if (!k) continue;
                WCHAR *hit = owned_command(sc, k);
                int conf = hit ? CONF_HIGH : CONF_NONE;
                if (!hit && (conf = exe_name_conf(sc, names.items[i]))) {
                    // "Applications\gridcraft.exe" de um executável do programa: com o comando
                    // apontando para um executável que ainda existe (e não é da pasta dele), só sugerido
                    hit = any_command(k);
                    if (hit && !exe_missing(hit)) conf = MIN(conf, CONF_MEDIUM);
                }
                RegCloseKey(k);
                if (conf && conf >= sc->thr) {
                    WCHAR *base = path_join(CLASSES_ROOTS[r], u"Applications"), *full = path_join(base, names.items[i]);
                    left_add(sc->out, LK_KEY, full, NULL, conf, LR_FILE_ASSOC, hit);
                    xfree(base), xfree(full);
                }
                xfree(hit);
            }
            strings_free(&names);
            RegCloseKey(apps);
        }
        // componentes COM
        HKEY clsid = reg_open_sub(root, u"CLSID");
        if (clsid) {
            static const WCHAR *const SERVERS[] = {u"InprocServer32", u"LocalServer32", u"InprocHandler32", u"DefaultIcon"};
            PtrVec ids = reg_subkeys(clsid);
            for (size_t i = 0; i < ids.n && !ctl_cancelled(sc->ctl); i++) {
                for (size_t s = 0; s < COUNT(SERVERS); s++) {
                    WCHAR *sub = path_join(ids.items[i], SERVERS[s]);
                    HKEY k = reg_open_sub(clsid, sub);
                    xfree(sub);
                    if (!k) continue;
                    WCHAR *v = value_text(k, u"");
                    RegCloseKey(k);
                    bool hit = owns(sc, v);
                    if (hit) {
                        WCHAR *base = path_join(CLASSES_ROOTS[r], u"CLSID"), *full = path_join(base, ids.items[i]);
                        if (left_add(sc->out, LK_KEY, full, NULL, CONF_HIGH, LR_COM, v)) remember(&sc->clsids, ids.items[i]);
                        xfree(base), xfree(full);
                    }
                    xfree(v);
                    if (hit) break;
                }
            }
            strings_free(&ids);
            RegCloseKey(clsid);
        }
        // bibliotecas de tipos: TypeLib\{guid}\<versão>\<idioma>\win32|win64
        HKEY tl = reg_open_sub(root, u"TypeLib");
        if (tl) {
            PtrVec libs = reg_subkeys(tl);
            for (size_t i = 0; i < libs.n && !ctl_cancelled(sc->ctl); i++) {
                HKEY lk = reg_open_sub(tl, libs.items[i]);
                if (!lk) continue;
                WCHAR *hit = NULL;
                PtrVec vers = reg_subkeys(lk);
                for (size_t v = 0; v < vers.n && !hit && v < 16; v++) {
                    static const WCHAR *const PLAT[] = {u"0\\win64", u"0\\win32", u"9\\win64", u"9\\win32", u"409\\win32", u"409\\win64"};
                    for (size_t p = 0; p < COUNT(PLAT) && !hit; p++) {
                        WCHAR *sub = path_join(vers.items[v], PLAT[p]);
                        HKEY pk = reg_open_sub(lk, sub);
                        xfree(sub);
                        if (!pk) continue;
                        WCHAR *t = value_text(pk, u"");
                        RegCloseKey(pk);
                        if (owns(sc, t)) hit = t;
                        else xfree(t);
                    }
                }
                strings_free(&vers);
                RegCloseKey(lk);
                if (hit) {
                    WCHAR *base = path_join(CLASSES_ROOTS[r], u"TypeLib"), *full = path_join(base, libs.items[i]);
                    left_add(sc->out, LK_KEY, full, NULL, CONF_HIGH, LR_COM, hit);
                    xfree(base), xfree(full), xfree(hit);
                }
            }
            strings_free(&libs);
            RegCloseKey(tl);
        }
        RegCloseKey(root);
    }
}

// Verbos do menu de contexto do Explorer (botão direito em arquivos, pastas, unidades...).
static void scan_shell_verbs(Scan *sc) {
    static const WCHAR *const PARENTS[] = {u"*\\shell", u"Directory\\shell", u"Directory\\Background\\shell", u"Folder\\shell", u"Drive\\shell",
                                           u"AllFilesystemObjects\\shell", u"DesktopBackground\\Shell", u"LibraryFolder\\Background\\shell"};
    for (size_t r = 0; r < COUNT(CLASSES_ROOTS); r++) {
        for (size_t p = 0; p < COUNT(PARENTS); p++) {
            WCHAR *parent = path_join(CLASSES_ROOTS[r], PARENTS[p]);
            HKEY k = reg_open(parent, KEY_READ);
            if (k) {
                PtrVec verbs = reg_subkeys(k);
                for (size_t i = 0; i < verbs.n; i++) {
                    HKEY v = reg_open_sub(k, verbs.items[i]);
                    if (!v) continue;
                    WCHAR *icon = value_text(v, u"Icon");
                    HKEY c = reg_open_sub(v, u"command");
                    WCHAR *cmd = c ? value_text(c, u"") : NULL;
                    if (c) RegCloseKey(c);
                    RegCloseKey(v);
                    const WCHAR *hit = owns(sc, cmd) ? cmd : owns(sc, icon) ? icon : NULL;
                    if (hit) {
                        WCHAR *full = path_join(parent, verbs.items[i]);
                        left_add(sc->out, LK_KEY, full, NULL, CONF_HIGH, LR_SHELL_EXT, hit);
                        xfree(full);
                    }
                    xfree(icon), xfree(cmd);
                }
                strings_free(&verbs);
                RegCloseKey(k);
            }
            xfree(parent);
        }
        // SystemFileAssociations\<extensão ou tipo>\shell\<verbo>
        WCHAR *sfa = path_join(CLASSES_ROOTS[r], u"SystemFileAssociations");
        HKEY s = reg_open(sfa, KEY_READ);
        if (s) {
            PtrVec types = reg_subkeys(s);
            for (size_t i = 0; i < types.n; i++) {
                WCHAR *shell_sub = path_join(types.items[i], u"shell");
                HKEY sh = reg_open_sub(s, shell_sub);
                if (sh) {
                    PtrVec verbs = reg_subkeys(sh);
                    for (size_t j = 0; j < verbs.n; j++) {
                        WCHAR *csub = path_join(verbs.items[j], u"command");
                        HKEY c = reg_open_sub(sh, csub);
                        xfree(csub);
                        if (!c) continue;
                        WCHAR *cmd = value_text(c, u"");
                        RegCloseKey(c);
                        if (owns(sc, cmd)) {
                            WCHAR *a = path_join(sfa, shell_sub), *full = path_join(a, verbs.items[j]);
                            left_add(sc->out, LK_KEY, full, NULL, CONF_HIGH, LR_SHELL_EXT, cmd);
                            xfree(a), xfree(full);
                        }
                        xfree(cmd);
                    }
                    strings_free(&verbs);
                    RegCloseKey(sh);
                }
                xfree(shell_sub);
            }
            strings_free(&types);
            RegCloseKey(s);
        }
        xfree(sfa);
    }
}

// Registros que só apontam para um componente COM do programa (pelo {GUID}).
static void scan_clsid_refs(Scan *sc) {
    if (!sc->clsids.n) return;
    static const WCHAR *const HANDLERS[] = {
        u"*\\shellex\\ContextMenuHandlers",         u"*\\shellex\\PropertySheetHandlers",        u"Directory\\shellex\\ContextMenuHandlers",
        u"Directory\\shellex\\DragDropHandlers",    u"Directory\\shellex\\CopyHookHandlers",     u"Directory\\shellex\\PropertySheetHandlers",
        u"Directory\\Background\\shellex\\ContextMenuHandlers", u"Folder\\shellex\\ContextMenuHandlers", u"Folder\\ShellEx\\DragDropHandlers",
        u"Drive\\shellex\\ContextMenuHandlers",     u"Drive\\shellex\\PropertySheetHandlers",    u"AllFilesystemObjects\\shellex\\ContextMenuHandlers",
        u"AllFilesystemObjects\\shellex\\PropertySheetHandlers",
    };
    for (size_t r = 0; r < COUNT(CLASSES_ROOTS); r++)
        for (size_t h = 0; h < COUNT(HANDLERS); h++) {
            WCHAR *parent = path_join(CLASSES_ROOTS[r], HANDLERS[h]);
            HKEY k = reg_open(parent, KEY_READ);
            if (k) {
                PtrVec subs = reg_subkeys(k);
                for (size_t i = 0; i < subs.n; i++) {
                    HKEY s = reg_open_sub(k, subs.items[i]);
                    WCHAR *def = s ? reg_get_str(s, u"") : NULL;
                    if (s) RegCloseKey(s);
                    WCHAR *t = def ? wtrim_dup(def) : NULL;
                    if (vec_has_i(&sc->clsids, subs.items[i]) || (t && vec_has_i(&sc->clsids, t))) {
                        WCHAR *full = path_join(parent, subs.items[i]);
                        left_add(sc->out, LK_KEY, full, NULL, CONF_HIGH, LR_SHELL_EXT, t);
                        xfree(full);
                    }
                    xfree(def), xfree(t);
                }
                strings_free(&subs);
                RegCloseKey(k);
            }
            xfree(parent);
        }
    // ícones sobrepostos e objetos auxiliares do navegador (subchaves com o GUID no valor padrão ou no nome)
    static const WCHAR *const KEYED[] = {
        u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ShellIconOverlayIdentifiers",
        u"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ShellIconOverlayIdentifiers",
        u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Browser Helper Objects",
        u"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Browser Helper Objects",
    };
    for (size_t r = 0; r < COUNT(KEYED); r++) {
        HKEY k = reg_open(KEYED[r], KEY_READ);
        if (!k) continue;
        PtrVec subs = reg_subkeys(k);
        for (size_t i = 0; i < subs.n; i++) {
            HKEY s = reg_open_sub(k, subs.items[i]);
            WCHAR *def = s ? reg_get_str(s, u"") : NULL;
            if (s) RegCloseKey(s);
            WCHAR *t = def ? wtrim_dup(def) : NULL;
            if (vec_has_i(&sc->clsids, subs.items[i]) || (t && vec_has_i(&sc->clsids, t))) {
                WCHAR *full = path_join(KEYED[r], subs.items[i]);
                left_add(sc->out, LK_KEY, full, NULL, CONF_HIGH, LR_SHELL_EXT, t);
                xfree(full);
            }
            xfree(def), xfree(t);
        }
        strings_free(&subs);
        RegCloseKey(k);
    }
    // valores com o GUID no nome (extensões aprovadas, ganchos do Explorer, cache)
    static const WCHAR *const VALUED[] = {
        u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved",
        u"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved",
        u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved",
        u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ShellExecuteHooks",
        u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Cached",
    };
    for (size_t r = 0; r < COUNT(VALUED); r++) {
        HKEY k = reg_open(VALUED[r], KEY_READ);
        if (!k) continue;
        PtrVec names = reg_value_names(k);
        for (size_t i = 0; i < names.n; i++) {
            const WCHAR *n = names.items[i];
            WCHAR guid[39];
            if (wlen(n) < 38) continue;
            mem_copy(guid, n, 38 * sizeof(WCHAR));
            guid[38] = 0;
            if (vec_has_i(&sc->clsids, guid)) left_add(sc->out, LK_VALUE, VALUED[r], n, CONF_HIGH, LR_SHELL_EXT, NULL);
        }
        strings_free(&names);
        RegCloseKey(k);
    }
}

// Extensões de arquivo que apontam para um tipo de arquivo (ProgID) do programa.
// Confiança de uma referência a um ProgID com o nome do programa que não sai com ele: alta se
// o ProgID não existe em nenhuma raiz de classes, média se ainda existe.
static int progid_conf(const WCHAR *progid) {
    for (size_t r = 0; r < COUNT(CLASSES_ROOTS); r++) {
        WCHAR *k = path_join(CLASSES_ROOTS[r], progid);
        bool e = reg_exists(k);
        xfree(k);
        if (e) return CONF_MEDIUM;
    }
    return CONF_HIGH;
}

static void scan_progid_refs(Scan *sc) {
    for (size_t r = 0; r < COUNT(CLASSES_ROOTS); r++) {
        HKEY root = reg_open(CLASSES_ROOTS[r], KEY_READ);
        if (!root) continue;
        PtrVec subs = reg_subkeys(root);
        for (size_t i = 0; i < subs.n; i++) {
            const WCHAR *ext = subs.items[i];
            if (ext[0] != '.') continue;
            HKEY k = reg_open_sub(root, ext);
            if (!k) continue;
            WCHAR *key = path_join(CLASSES_ROOTS[r], ext);
            // ".ext" (padrão) = ProgID: a associação aponta para algo que vai sumir
            WCHAR *def = reg_get_str(k, u"");
            if (def && (vec_has_i(&sc->progids, def) || progid_named(sc, def))) left_add(sc->out, LK_VALUE, key, u"", CONF_MEDIUM, LR_REFERENCE, def);
            xfree(def);
            HKEY ow = reg_open_sub(k, u"OpenWithProgids");
            if (ow) {
                PtrVec names = reg_value_names(ow);
                WCHAR *owkey = path_join(key, u"OpenWithProgids");
                for (size_t j = 0; j < names.n; j++) {
                    // ProgID com o nome do programa: marcado só se ele sai junto ou já não existe
                    int c = vec_has_i(&sc->progids, names.items[j]) ? CONF_HIGH : progid_named(sc, names.items[j]) ? progid_conf(names.items[j]) : CONF_NONE;
                    if (c && c >= sc->thr) left_add(sc->out, LK_VALUE, owkey, names.items[j], c, LR_REFERENCE, NULL);
                }
                xfree(owkey);
                strings_free(&names);
                RegCloseKey(ow);
            }
            xfree(key);
            RegCloseKey(k);
        }
        strings_free(&subs);
        RegCloseKey(root);
    }
}

// Nome exato deste programa e de nenhum outro instalado (regra 2 aplicada na hora).
static bool name_is_ours(const Scan *sc, const WCHAR *name) {
    if (match_name(sc->id, name) != CONF_HIGH) return false;
    for (size_t j = 0; j < sc->others.n; j++)
        if (match_name(sc->others.items[j], name) == CONF_HIGH) return false;
    return true;
}

// ProgID do programa pelo nome: "VLC.mp3" é do VLC (o começo, antes do ponto, é um nome exato).
static bool progid_named(const Scan *sc, const WCHAR *progid) {
    const WCHAR *dot = wfindc(progid, '.');
    if (!dot || dot == progid) return false;
    WCHAR *head = wdupn(progid, (size_t)(dot - progid));
    bool r = name_is_ours(sc, head);
    xfree(head);
    return r;
}

// "Abrir com" do Explorer (FileExts\.ext\OpenWithProgids): os tipos de arquivo do programa,
// que ficam oferecidos mesmo depois que o ProgID sumiu.
static void scan_fileexts(Scan *sc) {
    static const WCHAR FE[] = u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts";
    HKEY root = reg_open(FE, KEY_READ);
    if (!root) return;
    PtrVec exts = reg_subkeys(root);
    for (size_t i = 0; i < exts.n && !ctl_cancelled(sc->ctl); i++) {
        WCHAR *sub = path_join(exts.items[i], u"OpenWithProgids");
        HKEY k = reg_open_sub(root, sub);
        if (k) {
            PtrVec names = reg_value_names(k);
            WCHAR *key = path_join(FE, sub);
            for (size_t j = 0; j < names.n; j++) {
                const WCHAR *n = names.items[j];
                if (vec_has_i(&sc->progids, n)) left_add(sc->out, LK_VALUE, key, n, CONF_HIGH, LR_REFERENCE, NULL);
                else if (progid_named(sc, n)) left_add(sc->out, LK_VALUE, key, n, CONF_HIGH, LR_FILE_ASSOC, NULL);
            }
            xfree(key);
            strings_free(&names);
            RegCloseKey(k);
        }
        xfree(sub);
    }
    strings_free(&exts);
    RegCloseKey(root);
}

// Software\Clients\<tipo>\<nome> (navegadores, clientes de e-mail, players registrados).
static void scan_clients(Scan *sc) {
    static const WCHAR *const ROOTS[] = {u"HKLM\\SOFTWARE\\Clients", u"HKLM\\SOFTWARE\\WOW6432Node\\Clients", u"HKCU\\Software\\Clients"};
    for (size_t r = 0; r < COUNT(ROOTS); r++) {
        HKEY root = reg_open(ROOTS[r], KEY_READ);
        if (!root) continue;
        PtrVec types = reg_subkeys(root);
        for (size_t t = 0; t < types.n; t++) {
            HKEY tk = reg_open_sub(root, types.items[t]);
            if (!tk) continue;
            PtrVec names = reg_subkeys(tk);
            for (size_t i = 0; i < names.n; i++) {
                HKEY k = reg_open_sub(tk, names.items[i]);
                if (!k) continue;
                WCHAR *hit = owned_command(sc, k);
                RegCloseKey(k);
                if (hit) {
                    WCHAR *a = path_join(ROOTS[r], types.items[t]), *full = path_join(a, names.items[i]);
                    left_add(sc->out, LK_KEY, full, NULL, CONF_HIGH, LR_FILE_ASSOC, hit);
                    xfree(a), xfree(full);
                }
                xfree(hit);
            }
            strings_free(&names);
            RegCloseKey(tk);
        }
        strings_free(&types);
        RegCloseKey(root);
    }
}

static bool key_in_list(const LeftList *l, const WCHAR *key, int min_conf) {
    for (size_t i = 0; i < l->items.n; i++) {
        const Leftover *it = l->items.items[i];
        if (it->kind == LK_KEY && it->conf >= min_conf && path_is_under_or_eq(key, it->path)) return true;
    }
    return false;
}

// RegisteredApplications: valores que apontam para a chave "Capabilities" de algo que sai.
static void scan_registered_apps(Scan *sc) {
    static const struct {
        const WCHAR *key, *hive;
    } ROOTS[] = {
        {u"HKLM\\SOFTWARE\\RegisteredApplications", u"HKLM"},
        {u"HKLM\\SOFTWARE\\WOW6432Node\\RegisteredApplications", u"HKLM"},
        {u"HKCU\\Software\\RegisteredApplications", u"HKCU"},
    };
    for (size_t r = 0; r < COUNT(ROOTS); r++) {
        HKEY k = reg_open(ROOTS[r].key, KEY_READ);
        if (!k) continue;
        PtrVec vals = reg_values(k);
        for (size_t i = 0; i < vals.n; i++) {
            RegValue *v = vals.items[i];
            if (v->type != REG_SZ) continue;
            WCHAR *data = wdupn((const WCHAR *)v->data, v->size / 2), *full = wcat3(ROOTS[r].hive, u"\\", data);
            WCHAR *n = path_norm(full);
            bool dangling = false;
            if (!key_in_list(sc->out, n, CONF_HIGH) && name_is_ours(sc, v->name)) {
                // aponta para a chave de um programa com o nome dele que já não existe (nas duas vistas)
                WCHAR *wow = wcat3(ROOTS[r].hive, u"\\SOFTWARE\\WOW6432Node\\", wistarts(data, u"Software\\") ? data + 9 : data);
                dangling = !reg_exists(n) && !reg_exists(wow);
                xfree(wow);
            }
            if (key_in_list(sc->out, n, CONF_HIGH) || dangling) left_add(sc->out, LK_VALUE, ROOTS[r].key, v->name, CONF_HIGH, LR_REFERENCE, data);
            xfree(data), xfree(full), xfree(n);
        }
        reg_values_free(&vals);
        RegCloseKey(k);
    }
}

// ---------------------------------------------------------------- rastros pelo nome do executável

// Confiança de um rastro que o Windows guarda só com o nome do executável ("gridcraft.exe"):
// alta se o nome é o do programa (e de nenhum outro instalado); média se é de um executável que
// estava na pasta dele; baixa se o nome é genérico ou outro programa tem um executável igual.
static int exe_name_conf(const Scan *sc, const WCHAR *exe_leaf) {
    if (!exe_leaf || !wiends(exe_leaf, u".exe") || wlen(exe_leaf) < 5) return CONF_NONE;
    WCHAR *base = path_strip_ext(exe_leaf);
    int c = CONF_NONE;
    if (name_is_ours(sc, base)) {
        c = CONF_HIGH;
    } else if (vec_has_i(&sc->id->exes, exe_leaf)) {
        WCHAR *nb = norm_alnum(base);
        bool weak = wlen(nb) < 4 || is_generic_word(nb) || wistarts(nb, u"unins") || wistarts(nb, u"setup") || wistarts(nb, u"install") ||
                    wistarts(nb, u"update") || wistarts(nb, u"uninst");
        for (size_t j = 0; j < sc->others.n && !weak; j++) weak = vec_has_i(&((const Identity *)sc->others.items[j])->exes, exe_leaf);
        // executável da pasta do programa com o nome dele ("Greenshot.exe" do Greenshot, cujo nome
        // também é o do editor e por isso não conta como nome exato)
        bool own_name = sc->id->name_norm && weq(nb, sc->id->name_norm);
        c = weak ? CONF_LOW : own_name ? CONF_HIGH : CONF_MEDIUM;
        xfree(nb);
    }
    xfree(base);
    return c;
}

// Nome do executável no começo de um texto, até ".exe" (inclusive), em minúsculas; ou NULL.
static WCHAR *exe_prefix(const WCHAR *s) {
    const WCHAR *e = s ? wifind(s, u".exe") : NULL;
    if (!e || e == s) return NULL;
    WCHAR *r = wdupn(s, (size_t)(e + 4 - s));
    for (WCHAR *c = r; *c; c++) *c = wfold(*c);
    if (wfindc(r, '\\')) {
        xfree(r);
        return NULL;
    }
    return r;
}

static void add_exe_trace(Scan *sc, LeftKind kind, const WCHAR *path, const WCHAR *value, const WCHAR *exe_leaf) {
    int c = exe_name_conf(sc, exe_leaf);
    if (c && c >= sc->thr) left_add(sc->out, kind, path, value, c, LR_EXE_TRACE, exe_leaf);
}

// No Registro: Tracing (gridcraft_RASAPI32 e _RASMANCS, criadas por quem usa a rede),
// RADAR\HeapLeakDetection (diagnóstico de vazamento de memória) e DirectInput (GRIDCRAFT.EXE
// seguido de data e tamanho).
static void scan_exe_registry(Scan *sc) {
    static const WCHAR *const TRACING[] = {u"HKLM\\SOFTWARE\\Microsoft\\Tracing", u"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Tracing"};
    for (size_t r = 0; r < COUNT(TRACING); r++) {
        HKEY root = reg_open(TRACING[r], KEY_READ);
        if (!root) continue;
        PtrVec subs = reg_subkeys(root);
        for (size_t i = 0; i < subs.n; i++) {
            const WCHAR *n = subs.items[i];
            size_t len = wlen(n);
            if (len <= 9 || (!wiends(n, u"_RASAPI32") && !wiends(n, u"_RASMANCS"))) continue;
            WCHAR *base = wdupn(n, len - 9), *leaf = wcat(base, u".exe"), *full = path_join(TRACING[r], n);
            for (WCHAR *c = leaf; *c; c++) *c = wfold(*c);
            add_exe_trace(sc, LK_KEY, full, NULL, leaf);
            xfree(base), xfree(leaf), xfree(full);
        }
        strings_free(&subs);
        RegCloseKey(root);
    }
    static const WCHAR *const BY_EXE[] = {u"HKLM\\SOFTWARE\\Microsoft\\RADAR\\HeapLeakDetection\\DiagnosedApplications",
                                          u"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\RADAR\\HeapLeakDetection\\DiagnosedApplications",
                                          u"HKCU\\Software\\Microsoft\\DirectInput"};
    for (size_t r = 0; r < COUNT(BY_EXE); r++) {
        HKEY root = reg_open(BY_EXE[r], KEY_READ);
        if (!root) continue;
        PtrVec subs = reg_subkeys(root);
        for (size_t i = 0; i < subs.n; i++) {
            const WCHAR *n = subs.items[i];
            WCHAR *leaf = exe_prefix(n);
            if (!leaf) continue;
            // DirectInput: depois do nome só vêm dígitos hexadecimais (data e tamanho do executável)
            bool ok = true;
            for (const WCHAR *c = n + wlen(leaf); *c && ok; c++) ok = (*c >= '0' && *c <= '9') || ((*c | 32) >= 'a' && (*c | 32) <= 'f');
            if (ok) {
                WCHAR *full = path_join(BY_EXE[r], n);
                add_exe_trace(sc, LK_KEY, full, NULL, leaf);
                xfree(full);
            }
            xfree(leaf);
        }
        strings_free(&subs);
        RegCloseKey(root);
    }
}

// Em pastas: Prefetch (C:\Windows\Prefetch\GRIDCRAFT.EXE-1A2B3C4D.pf), despejos de falha
// (%LOCALAPPDATA%\CrashDumps\gridcraft.exe.1234.dmp) e relatórios de erro do Windows
// (WER\ReportArchive e ReportQueue, pastas AppCrash_gridcraft.exe_...).
static void scan_exe_files(Scan *sc) {
    const Paths *p = &g_paths;
    if (p->windows) {
        WCHAR *pf = path_join(p->windows, u"Prefetch");
        PtrVec v = fs_list(pf);
        for (size_t i = 0; i < v.n; i++) {
            DirEntry *e = v.items[i];
            size_t n = wlen(e->name);
            if (e->is_dir || n < 17 || !wiends(e->name, u".pf") || e->name[n - 12] != '-') continue;
            WCHAR *leaf = wdupn(e->name, n - 12), *full = path_join(pf, e->name);
            for (WCHAR *c = leaf; *c; c++) *c = wfold(*c);
            add_exe_trace(sc, LK_FILE, full, NULL, leaf);
            xfree(leaf), xfree(full);
        }
        fs_list_free(&v);
        xfree(pf);
    }
    if (p->local_appdata) {
        WCHAR *cd = path_join(p->local_appdata, u"CrashDumps");
        PtrVec v = fs_list(cd);
        for (size_t i = 0; i < v.n; i++) {
            DirEntry *e = v.items[i];
            WCHAR *leaf = e->is_dir || !wiends(e->name, u".dmp") ? NULL : exe_prefix(e->name);
            if (leaf) {
                WCHAR *full = path_join(cd, e->name);
                add_exe_trace(sc, LK_FILE, full, NULL, leaf);
                xfree(full);
            }
            xfree(leaf);
        }
        fs_list_free(&v);
        xfree(cd);
    }
    const WCHAR *wer_roots[] = {p->local_appdata, p->program_data};
    for (size_t r = 0; r < COUNT(wer_roots); r++) {
        if (!wer_roots[r]) continue;
        static const WCHAR *const SUBS[] = {u"Microsoft\\Windows\\WER\\ReportArchive", u"Microsoft\\Windows\\WER\\ReportQueue"};
        for (size_t s = 0; s < COUNT(SUBS); s++) {
            WCHAR *dir = path_join(wer_roots[r], SUBS[s]);
            PtrVec v = fs_list(dir);
            for (size_t i = 0; i < v.n; i++) {
                DirEntry *e = v.items[i];
                const WCHAR *us = e->is_dir ? wfindc(e->name, '_') : NULL;
                if (!us || (!wistarts(e->name, u"AppCrash_") && !wistarts(e->name, u"AppHang_"))) continue;
                WCHAR *leaf = exe_prefix(us + 1);
                if (leaf) {
                    WCHAR *full = path_join(dir, e->name);
                    add_exe_trace(sc, LK_DIR, full, NULL, leaf);
                    xfree(full);
                }
                xfree(leaf);
            }
            fs_list_free(&v);
            xfree(dir);
        }
    }
}

// Uso pelo Explorer: FeatureUsage (barra de tarefas: programas alternados, abertos, com lista
// de atalhos ou com selo) e os avisos de "novo aplicativo para este tipo de arquivo"
// (ApplicationAssociationToasts, valores "<ProgID>_<.ext>").
static void scan_feature_usage(Scan *sc) {
    static const WCHAR *const FU[] = {u"AppSwitched", u"AppLaunch", u"ShowJumpView", u"AppBadgeUpdated"};
    for (size_t f = 0; f < COUNT(FU); f++) {
        WCHAR *key = path_join(u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FeatureUsage", FU[f]);
        HKEY k = reg_open(key, KEY_READ);
        if (k) {
            PtrVec names = reg_value_names(k);
            for (size_t i = 0; i < names.n; i++) {
                WCHAR *x = expand_known_folder(names.items[i]);
                if (owns(sc, x) || exe_gone_named(sc, x) || (!wfindc(x, '\\') && progid_named(sc, x))) left_add(sc->out, LK_VALUE, key, names.items[i], CONF_HIGH, LR_USAGE, x);
                xfree(x);
            }
            strings_free(&names);
            RegCloseKey(k);
        }
        xfree(key);
    }
    static const WCHAR TOASTS[] = u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\ApplicationAssociationToasts";
    HKEY k = reg_open(TOASTS, KEY_READ);
    if (!k) return;
    PtrVec names = reg_value_names(k);
    for (size_t i = 0; i < names.n; i++) {
        const WCHAR *n = names.items[i], *us = wrfindc(n, '_');
        if (!us || us == n) continue;
        WCHAR *head = wdupn(n, (size_t)(us - n));
        int c = CONF_NONE;
        if (wistarts(head, u"Applications\\")) c = exe_name_conf(sc, head + 13);
        else if (vec_has_i(&sc->progids, head)) c = CONF_HIGH;
        else if (progid_named(sc, head)) c = progid_conf(head);
        if (c && c >= sc->thr) left_add(sc->out, LK_VALUE, TOASTS, n, c, LR_FILE_ASSOC, head);
        xfree(head);
    }
    strings_free(&names);
    RegCloseKey(k);
}

// ---------------------------------------------------------------- Windows Installer

#define MSI_ROOT u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Installer"

// O que o Windows Installer guarda de um produto, pelo código dele (a entrada de desinstalação
// de um MSI tem o código entre chaves como nome). Depois de um msiexec /x que deu certo sobra
// pouco (o registro da pasta {código} do cache, às vezes a própria pasta); numa desinstalação
// forçada, ou que falhou, sobra o produto inteiro registrado, que o Windows continuaria
// achando instalado.
static void scan_msi(Scan *sc, const WCHAR *uninstall_key) {
    const WCHAR *code = uninstall_key ? path_leaf(uninstall_key) : NULL;
    WCHAR packed[33];
    if (!code || !guid_pack(code, packed)) return;
    static const WCHAR *const PROD[] = {u"HKLM\\SOFTWARE\\Classes\\Installer\\Products", u"HKLM\\SOFTWARE\\Classes\\Installer\\Features",
                                        u"HKCU\\Software\\Classes\\Installer\\Products", u"HKCU\\Software\\Classes\\Installer\\Features",
                                        u"HKCU\\Software\\Microsoft\\Installer\\Products", u"HKCU\\Software\\Microsoft\\Installer\\Features"};
    bool registered = false;
    for (size_t i = 0; i < COUNT(PROD); i++) {
        WCHAR *k = path_join(PROD[i], packed);
        if (reg_exists(k)) {
            left_add(sc->out, LK_KEY, k, NULL, CONF_HIGH, LR_MSI, code);
            registered = true;
        }
        xfree(k);
    }
    // códigos de atualização: um valor por produto (a chave sai junto se for o único)
    static const WCHAR *const UPG[] = {u"HKLM\\SOFTWARE\\Classes\\Installer\\UpgradeCodes", u"HKCU\\Software\\Classes\\Installer\\UpgradeCodes",
                                       u"HKCU\\Software\\Microsoft\\Installer\\UpgradeCodes", MSI_ROOT u"\\UpgradeCodes"};
    for (size_t r = 0; r < COUNT(UPG); r++) {
        HKEY root = reg_open(UPG[r], KEY_READ);
        if (!root) continue;
        PtrVec subs = reg_subkeys(root);
        for (size_t i = 0; i < subs.n; i++) {
            HKEY k = reg_open_sub(root, subs.items[i]);
            if (!k) continue;
            PtrVec names = reg_value_names(k);
            RegCloseKey(k);
            bool has = false;
            for (size_t j = 0; j < names.n && !has; j++) has = wieq(names.items[j], packed);
            if (has) {
                WCHAR *full = path_join(UPG[r], subs.items[i]);
                if (names.n == 1) left_add(sc->out, LK_KEY, full, NULL, CONF_HIGH, LR_MSI, code);
                else left_add(sc->out, LK_VALUE, full, packed, CONF_HIGH, LR_MSI, code);
                xfree(full);
            }
            strings_free(&names);
        }
        strings_free(&subs);
        RegCloseKey(root);
    }
    // UserData\<usuário>\Products\<código> (com o pacote .msi em cache) e os componentes
    HKEY ud = reg_open(MSI_ROOT u"\\UserData", KEY_READ);
    PtrVec sids = ud ? reg_subkeys(ud) : (PtrVec){0};
    WCHAR *inst = g_paths.windows ? path_join(g_paths.windows, u"Installer") : NULL;
    for (size_t s = 0; s < sids.n; s++) {
        WCHAR *rel = wfmt(u"%s\\Products\\%s", sids.items[s], packed);
        HKEY pk = reg_open_sub(ud, rel);
        if (pk) {
            HKEY ip = reg_open_sub(pk, u"InstallProperties");
            WCHAR *pkg = ip ? value_text(ip, u"LocalPackage") : NULL;
            if (ip) RegCloseKey(ip);
            WCHAR *pkgn = pkg ? path_norm(pkg) : NULL, *pkgp = pkgn ? path_parent(pkgn) : NULL;
            if (pkgn && inst && pkgp && path_eq(pkgp, inst) && file_exists(pkgn)) left_add(sc->out, LK_FILE, pkgn, NULL, CONF_HIGH, LR_MSI, code);
            xfree(pkg), xfree(pkgn), xfree(pkgp);
            RegCloseKey(pk);
            WCHAR *full = wfmt(MSI_ROOT u"\\UserData\\%s", rel);
            left_add(sc->out, LK_KEY, full, NULL, CONF_HIGH, LR_MSI, code);
            xfree(full);
            registered = true;
        }
        xfree(rel);
    }
    // componentes: só com o produto ainda registrado (são milhares de chaves) e fora da análise
    // de antes da desinstalação (o próprio msiexec tira)
    for (size_t s = 0; registered && !(sc->flags & SCANF_BEFORE) && s < sids.n && !ctl_cancelled(sc->ctl); s++) {
        WCHAR *rel = path_join(sids.items[s], u"Components");
        HKEY ck = reg_open_sub(ud, rel);
        if (ck) {
            PtrVec comps = reg_subkeys(ck);
            for (size_t c = 0; c < comps.n; c++) {
                HKEY k = reg_open_sub(ck, comps.items[c]);
                if (!k) continue;
                DWORD t, sz = 0, nvals = 0;
                bool has = RegQueryValueExW(k, packed, NULL, &t, NULL, &sz) == ERROR_SUCCESS;
                if (has) RegQueryInfoKeyW(k, NULL, NULL, NULL, NULL, NULL, NULL, &nvals, NULL, NULL, NULL, NULL);
                RegCloseKey(k);
                if (!has) continue;
                WCHAR *full = wfmt(MSI_ROOT u"\\UserData\\%s\\%s", rel, comps.items[c]);
                if (nvals == 1) left_add(sc->out, LK_KEY, full, NULL, CONF_HIGH, LR_MSI, code);
                else left_add(sc->out, LK_VALUE, full, packed, CONF_HIGH, LR_MSI, code);
                xfree(full);
            }
            strings_free(&comps);
            RegCloseKey(ck);
        }
        xfree(rel);
    }
    strings_free(&sids);
    if (ud) RegCloseKey(ud);
    // pastas registradas (Installer\Folders): as do programa e a do cache do produto
    WCHAR *cache = inst ? path_join(inst, code) : NULL;
    HKEY fk = reg_open(MSI_ROOT u"\\Folders", KEY_READ);
    if (fk) {
        PtrVec names = reg_value_names(fk);
        for (size_t i = 0; i < names.n; i++) {
            WCHAR *n = path_norm(names.items[i]);
            if ((cache && path_eq(n, cache)) || owns(sc, n)) left_add(sc->out, LK_VALUE, MSI_ROOT u"\\Folders", names.items[i], CONF_HIGH, LR_MSI, code);
            xfree(n);
        }
        strings_free(&names);
        RegCloseKey(fk);
    }
    // cache do produto: C:\Windows\Installer\{código} (ícones) e SourceHash{código}
    if (cache && file_exists(cache)) left_add(sc->out, LK_DIR, cache, NULL, CONF_HIGH, LR_MSI, code);
    if (inst) {
        WCHAR *leaf = wcat(u"SourceHash", code), *sh = path_join(inst, leaf);
        if (file_exists(sh)) left_add(sc->out, LK_FILE, sh, NULL, CONF_HIGH, LR_MSI, code);
        xfree(leaf), xfree(sh);
    }
    xfree(cache), xfree(inst);
}

// ---------------------------------------------------------------- tarefas agendadas

static WCHAR *xml_text(const WCHAR *xml, const WCHAR *tag, const WCHAR **from) {
    WCHAR *open = wcat3(u"<", tag, u">"), *close = wcat3(u"</", tag, u">");
    const WCHAR *a = wifind(*from, open), *b = a ? wifind(a, close) : NULL;
    WCHAR *r = NULL;
    if (a && b) {
        a += wlen(open);
        StrBuf sb = {0};
        for (const WCHAR *p = a; p < b; p++) {  // entidades XML mais comuns
            if (*p == '&') {
                if (wistarts(p, u"&amp;")) sb_addc(&sb, '&'), p += 4;
                else if (wistarts(p, u"&quot;")) sb_addc(&sb, '"'), p += 5;
                else if (wistarts(p, u"&apos;")) sb_addc(&sb, '\''), p += 5;
                else if (wistarts(p, u"&lt;")) sb_addc(&sb, '<'), p += 3;
                else if (wistarts(p, u"&gt;")) sb_addc(&sb, '>'), p += 3;
                else sb_addc(&sb, *p);
            } else {
                sb_addc(&sb, *p);
            }
        }
        WCHAR *t = sb_take(&sb);
        r = expand_env(t);
        xfree(t);
        *from = b + wlen(close);
    }
    (void)xml;
    xfree(open), xfree(close);
    return r;
}

static WCHAR *task_xml(const WCHAR *path) {
    uint8_t *data;
    size_t len;
    if (!file_read_all(path, &data, &len)) return NULL;
    WCHAR *w;
    if (len >= 2 && data[0] == 0xFF && data[1] == 0xFE) w = wdupn((const WCHAR *)(data + 2), (len - 2) / 2);
    else w = utf8_to_w((const char *)data, len, NULL);
    xfree(data);
    return w;
}

static void scan_tasks_in(Scan *sc, const WCHAR *dir, const WCHAR *name_prefix, int depth) {
    if (depth > 6) return;
    PtrVec v = fs_list(dir);
    for (size_t i = 0; i < v.n; i++) {
        DirEntry *e = v.items[i];
        WCHAR *path = path_join(dir, e->name), *name = wcat3(name_prefix, u"\\", e->name);
        if (e->is_dir) {
            if (!e->is_link && !(depth == 0 && wieq(e->name, u"Microsoft"))) scan_tasks_in(sc, path, name, depth + 1);
        } else if (e->size < (1u << 20)) {
            WCHAR *xml = task_xml(path);
            if (xml) {
                int conf = CONF_NONE;
                WCHAR *why = NULL;
                const WCHAR *from = xml;
                for (int n = 0; n < 8 && !conf; n++) {  // todas as ações <Exec>
                    WCHAR *cmd = xml_text(xml, u"Command", &from);
                    if (!cmd) break;
                    if (owns(sc, cmd)) conf = CONF_HIGH, why = wdup(cmd);
                    else if (match_name(sc->id, e->name) == CONF_HIGH && exe_missing(cmd)) conf = CONF_MEDIUM, why = wdup(cmd);
                    xfree(cmd);
                }
                from = xml;
                WCHAR *wd = xml_text(xml, u"WorkingDirectory", &from);
                if (!conf && owns(sc, wd)) conf = CONF_HIGH, why = wdup(wd);
                xfree(wd);
                from = xml;
                WCHAR *cls = xml_text(xml, u"ClassId", &from);  // ação de componente COM
                if (!conf && cls) {
                    WCHAR *t = wtrim_dup(cls);
                    if (vec_has_i(&sc->clsids, t)) conf = CONF_HIGH, why = wdup(t);
                    xfree(t);
                }
                xfree(cls);
                if (conf) left_add(sc->out, LK_TASK, name, NULL, conf, LR_TASK, why);
                xfree(why), xfree(xml);
            }
        }
        xfree(path), xfree(name);
    }
    fs_list_free(&v);
}

static void scan_tasks(Scan *sc) {
    if (!g_paths.system32) return;
    WCHAR *dir = path_join(g_paths.system32, u"Tasks");
    scan_tasks_in(sc, dir, u"", 0);
    xfree(dir);
}

// ---------------------------------------------------------------- pastas e atalhos

static void scan_dir_names(Scan *sc, const WCHAR *root, bool microsoft_only) {
    if (!root || !dir_exists(root)) return;
    const Identity *id = sc->id;
    PtrVec v = fs_list(root);
    for (size_t i = 0; i < v.n; i++) {
        DirEntry *e = v.items[i];
        if (!e->is_dir && !microsoft_only && !e->is_link) {
            // arquivo solto com o nome exato do programa ("winscp.rnd" direto no AppData)
            WCHAR *base = path_strip_ext(e->name);
            if (name_is_ours(sc, base)) {
                WCHAR *fp = path_join(root, e->name);
                left_add(sc->out, LK_FILE, fp, NULL, CONF_HIGH, LR_NAME, NULL);
                xfree(fp);
            }
            xfree(base);
            continue;
        }
        if (!e->is_dir || e->is_link) continue;
        WCHAR *path = path_join(root, e->name);
        if (microsoft_only) {
            if (match_name(id, e->name) == CONF_HIGH) left_add(sc->out, LK_DIR, path, NULL, CONF_HIGH, LR_NAME, NULL);
        } else {
            int c = match_name(id, e->name);
            if (c && c >= sc->thr) {
                left_add(sc->out, LK_DIR, path, NULL, c, LR_NAME, NULL);
            } else if (match_publisher(id, e->name)) {
                PtrVec v2 = fs_list(path);
                bool any = false;
                for (size_t j = 0; j < v2.n; j++) {
                    DirEntry *e2 = v2.items[j];
                    if (!e2->is_dir || e2->is_link) continue;
                    int c2 = match_name(id, e2->name);
                    if (c2 < CONF_HIGH && match_name_versioned(id, e2->name)) c2 = CONF_HIGH;
                    if (c2 && c2 >= sc->thr) {
                        WCHAR *p2 = path_join(path, e2->name);
                        any = left_add(sc->out, LK_DIR, p2, NULL, c2, LR_NAME, NULL) != NULL || any;
                        xfree(p2);
                    }
                }
                if (any) left_add(sc->out, LK_DIR_IF_EMPTY, path, NULL, CONF_MEDIUM, LR_PUBLISHER, NULL);
                fs_list_free(&v2);
            }
        }
        xfree(path);
    }
    fs_list_free(&v);
}

static void scan_shortcuts_in(Scan *sc, const WCHAR *dir, int thr, int depth) {
    if (!dir || depth > 3) return;
    PtrVec v = fs_list(dir);
    for (size_t i = 0; i < v.n; i++) {
        DirEntry *e = v.items[i];
        WCHAR *path = path_join(dir, e->name);
        if (e->is_dir) {
            if (!e->is_link) {
                int c = match_name(sc->id, e->name);
                if (c && c >= thr) left_add(sc->out, LK_DIR, path, NULL, c, LR_NAME, NULL);
                else scan_shortcuts_in(sc, path, thr, depth + 1);
            }
        } else if (wiends(e->name, u".lnk") && e->size < (1u << 20)) {
            uint8_t *data;
            size_t len;
            if (file_read_all(path, &data, &len)) {
                LnkInfo l;
                if (lnk_parse(data, len, &l)) {
                    WCHAR *target = l.target ? expand_env(l.target) : NULL, *wd = l.workdir ? expand_env(l.workdir) : NULL;
                    if (owns(sc, target) || (!target && owns(sc, wd))) {
                        left_add(sc->out, LK_FILE, path, NULL, CONF_HIGH, LR_SHORTCUT, target ? target : wd);
                    } else if (!target || !file_exists(target)) {
                        // atalho quebrado (ou "anunciado" do Windows Installer) com o nome do programa
                        WCHAR *base = path_strip_ext(e->name);
                        if (match_name(sc->id, base) == CONF_HIGH) left_add(sc->out, LK_FILE, path, NULL, CONF_MEDIUM, LR_SHORTCUT, target);
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

static void scan_shortcuts(Scan *sc) {
    const Paths *p = &g_paths;
    const WCHAR *deep[] = {p->programs, p->common_programs};
    for (size_t i = 0; i < COUNT(deep); i++) scan_shortcuts_in(sc, deep[i], sc->thr, 0);
    WCHAR *ql = p->appdata ? path_join(p->appdata, u"Microsoft\\Internet Explorer\\Quick Launch") : NULL;
    WCHAR *pinned = ql ? path_join(ql, u"User Pinned\\TaskBar") : NULL, *pinned_start = ql ? path_join(ql, u"User Pinned\\StartMenu") : NULL;
    const WCHAR *flat[] = {p->desktop, p->public_desktop, p->startup, p->common_startup, ql, pinned, pinned_start};
    for (size_t i = 0; i < COUNT(flat); i++) scan_shortcuts_in(sc, flat[i], CONF_HIGH + 1, 3);  // só os .lnk, sem pastas
    xfree(ql), xfree(pinned), xfree(pinned_start);
    // atalhos da pasta Inicializar têm também um "ativado/desativado" no Registro
    static const struct {
        const WCHAR *approved;
        int which;
    } AP[] = {{u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\StartupFolder", 0},
              {u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\StartupFolder", 1}};
    size_t n = sc->out->items.n;
    for (size_t i = 0; i < n; i++) {
        Leftover *it = sc->out->items.items[i];
        if (it->kind != LK_FILE) continue;
        for (size_t a = 0; a < COUNT(AP); a++) {
            const WCHAR *folder = AP[a].which ? p->common_startup : p->startup;
            WCHAR *parent = path_parent(it->path);
            if (folder && parent && path_eq(parent, folder) && value_exists(AP[a].approved, path_leaf(it->path)))
                left_add(sc->out, LK_VALUE, AP[a].approved, path_leaf(it->path), it->conf, LR_RUN, NULL);
            xfree(parent);
        }
    }
}

static void scan_files(Scan *sc) {
    const Identity *id = sc->id;
    const Paths *p = &g_paths;
    // pastas de instalação que ainda existem
    for (size_t i = 0; i < id->dirs.n; i++)
        if (dir_exists(id->dirs.items[i])) left_add(sc->out, LK_DIR, id->dirs.items[i], NULL, CONF_HIGH, LR_INSTALL_DIR, NULL);

    WCHAR *local_programs = p->local_appdata ? path_join(p->local_appdata, u"Programs") : NULL;
    // pasta do editor que continha a de instalação ("C:\Program Files\VideoLAN\VLC"): sai se ficar vazia
    const WCHAR *proots[] = {p->program_files, p->program_files_x86, local_programs};
    for (size_t i = 0; i < id->dirs.n; i++) {
        WCHAR *parent = path_parent(id->dirs.items[i]), *gp = parent ? path_parent(parent) : NULL;
        for (size_t r = 0; gp && r < COUNT(proots); r++)
            if (proots[r] && path_eq(gp, proots[r]) && dir_exists(parent)) left_add(sc->out, LK_DIR_IF_EMPTY, parent, NULL, CONF_HIGH, LR_PUBLISHER, NULL);
        xfree(parent), xfree(gp);
    }
    const WCHAR *roots[] = {p->program_files, p->program_files_x86, p->common_files, p->common_files_x86, p->program_data,
                            p->appdata,       p->local_appdata,     p->locallow,     local_programs};
    for (size_t i = 0; i < COUNT(roots); i++) scan_dir_names(sc, roots[i], false);
    if (sc->mode == SCAN_ADVANCED) {
        const WCHAR *more[] = {p->documents, p->public_documents, p->temp};
        for (size_t i = 0; i < COUNT(more); i++) scan_dir_names(sc, more[i], false);
    }
    if (wistarts(id->publisher, u"Microsoft")) {
        const WCHAR *ms[] = {p->appdata, p->local_appdata, p->program_data};
        for (size_t i = 0; i < COUNT(ms); i++) {
            if (!ms[i]) continue;
            WCHAR *d = path_join(ms[i], u"Microsoft");
            scan_dir_names(sc, d, true);
            xfree(d);
        }
    }
    if (sc->mode >= SCAN_MODERATE && p->profile) {
        // pastas com ponto na pasta do usuário (".gridcraft"), comuns em programas multiplataforma
        PtrVec v = fs_list(p->profile);
        for (size_t i = 0; i < v.n; i++) {
            DirEntry *e = v.items[i];
            if (e->name[0] != '.' || !e->name[1] || e->is_link) continue;
            int c = name_is_ours(sc, e->name + 1) ? CONF_HIGH : match_name(id, e->name + 1);
            if (c && c >= sc->thr) {
                WCHAR *path = path_join(p->profile, e->name);
                left_add(sc->out, e->is_dir ? LK_DIR : LK_FILE, path, NULL, c, LR_NAME, NULL);
                xfree(path);
            }
        }
        fs_list_free(&v);
    }
    if (sc->mode >= SCAN_MODERATE) scan_exe_files(sc);
    if (sc->mode >= SCAN_MODERATE && p->windows) {
        // pastas na raiz da unidade do sistema (C:\Python312, C:\Tools\...): só nome exato
        WCHAR drive[4] = {p->windows[0], ':', '\\', 0};
        PtrVec v = fs_list(drive);
        for (size_t i = 0; i < v.n; i++) {
            DirEntry *e = v.items[i];
            if (e->is_dir && !e->is_link && match_name(id, e->name) == CONF_HIGH) {
                WCHAR *path = path_join(drive, e->name);
                left_add(sc->out, LK_DIR, path, NULL, CONF_HIGH, LR_NAME, NULL);
                xfree(path);
            }
        }
        fs_list_free(&v);
    }
    xfree(local_programs);
}

// ---------------------------------------------------------------- varredura profunda (modo avançado)

typedef struct {
    Scan *sc;
    size_t budget;
} Deep;

static void deep_walk(Deep *d, const WCHAR *path, HKEY k, int depth) {
    if (!d->budget || ctl_cancelled(d->sc->ctl) || key_in_list(d->sc->out, path, CONF_NONE)) return;
    PtrVec vals = reg_values(k);
    for (size_t i = 0; i < vals.n && d->budget; i++, d->budget--) {
        RegValue *v = vals.items[i];
        WCHAR *t = regvalue_text(v);
        if (t && owns(d->sc, t)) left_add(d->sc->out, LK_VALUE, path, v->name, CONF_LOW, LR_PATH_REF, t);
        xfree(t);
    }
    reg_values_free(&vals);
    if (depth >= 8) return;
    PtrVec subs = reg_subkeys(k);
    for (size_t i = 0; i < subs.n && d->budget; i++) {
        if (depth == 0 && skip_l1(subs.items[i])) continue;
        HKEY s = reg_open_sub(k, subs.items[i]);
        if (!s) continue;
        WCHAR *child = path_join(path, subs.items[i]);
        deep_walk(d, child, s, depth + 1);
        xfree(child);
        RegCloseKey(s);
    }
    strings_free(&subs);
}

static void scan_deep_refs(Scan *sc) {
    Deep d = {sc, 400000};
    for (size_t r = 0; r < COUNT(SOFTWARE_ROOTS) && d.budget; r++) {
        HKEY k = reg_open(SOFTWARE_ROOTS[r], KEY_READ);
        if (!k) continue;
        deep_walk(&d, SOFTWARE_ROOTS[r], k, 0);
        RegCloseKey(k);
    }
}

// ---------------------------------------------------------------- proteção dos outros programas

enum { REF_OURS = 1, REF_FOREIGN = 2 };

// Referências a caminhos dentro de uma chave (com subchaves): deste programa, de outro, ou das duas.
static int key_refs(Scan *sc, HKEY k, int depth, size_t *budget) {
    int bits = 0;
    PtrVec vals = reg_values(k);
    for (size_t i = 0; i < vals.n && *budget; i++, (*budget)--) {
        WCHAR *t = regvalue_text(vals.items[i]);
        if (t) {
            if (owns(sc, t)) bits |= REF_OURS;
            else if (foreign(sc, t)) bits |= REF_FOREIGN;
        }
        xfree(t);
    }
    reg_values_free(&vals);
    if (depth >= 6) return bits;
    PtrVec subs = reg_subkeys(k);
    for (size_t i = 0; i < subs.n && *budget; i++) {
        HKEY s = reg_open_sub(k, subs.items[i]);
        if (!s) continue;
        bits |= key_refs(sc, s, depth + 1, budget);
        RegCloseKey(s);
    }
    strings_free(&subs);
    return bits;
}

// Dentro de uma chave compartilhada, as subchaves que só falam deste programa.
static void refine_key(Scan *sc, const WCHAR *path, int depth) {
    if (depth > 4) return;
    HKEY k = reg_open(path, KEY_READ);
    if (!k) return;
    PtrVec subs = reg_subkeys(k);
    for (size_t i = 0; i < subs.n; i++) {
        HKEY s = reg_open_sub(k, subs.items[i]);
        if (!s) continue;
        size_t budget = 5000;
        int bits = key_refs(sc, s, 0, &budget);
        RegCloseKey(s);
        WCHAR *child = path_join(path, subs.items[i]);
        if (bits == REF_OURS) left_add(sc->out, LK_KEY, child, NULL, CONF_HIGH, LR_PATH_REF, NULL);
        else if (bits & REF_FOREIGN) refine_key(sc, child, depth + 1);
        xfree(child);
    }
    strings_free(&subs);
    RegCloseKey(k);
}

static bool has_tag(const WCHAR *name, const WCHAR *tag) { return tag && wifind(name, tag); }

// Dentro de uma pasta compartilhada por versões, as subpastas com a versão deste programa.
static void refine_dir_by_version(Scan *sc, const WCHAR *dir, const Identity *other) {
    if (!sc->id->vtag) return;
    PtrVec v = fs_list(dir);
    for (size_t i = 0; i < v.n; i++) {
        DirEntry *e = v.items[i];
        if (!e->is_dir || e->is_link) continue;
        WCHAR *nn = norm_alnum(e->name);
        if (has_tag(nn, sc->id->vtag) && !(other && other->vtag && has_tag(nn, other->vtag)) && match_name(sc->id, e->name) >= CONF_MEDIUM) {
            WCHAR *p = path_join(dir, e->name);
            left_add(sc->out, LK_DIR, p, NULL, CONF_MEDIUM, LR_NAME, NULL);
            xfree(p);
        }
        xfree(nn);
    }
    fs_list_free(&v);
}

// Uma pasta que contém a pasta de outro programa: sai item por item, menos o outro programa.
static void split_dir(Scan *sc, Leftover *it) {
    it->kind = LK_DIR_IF_EMPTY;
    // dentro da pasta de instalação do próprio programa, os itens continuam marcados; numa
    // pasta achada só pelo nome, entram desmarcados (podem ser do outro programa)
    int child_conf = it->reason == LR_INSTALL_DIR ? it->conf : MIN(it->conf, CONF_MEDIUM);
    PtrVec v = fs_list(it->path);
    for (size_t i = 0; i < v.n; i++) {
        DirEntry *e = v.items[i];
        WCHAR *p = path_join(it->path, e->name);
        bool keep = false;
        for (size_t j = 0; j < sc->other_dirs.n && !keep; j++) {
            const WCHAR *o = sc->other_dirs.items[j];
            keep = path_is_under_or_eq(o, p);  // é o outro programa ou contém ele
        }
        if (keep && e->is_dir && !e->is_link) {
            // contém o outro mais fundo: desce um nível (o outro está em p\...\outro)
            bool exact = false;
            for (size_t j = 0; j < sc->other_dirs.n; j++) exact = exact || path_eq(sc->other_dirs.items[j], p);
            if (!exact) {
                Leftover *sub = left_add(sc->out, LK_DIR_IF_EMPTY, p, NULL, it->conf, it->reason, it->detail);
                if (sub) split_dir(sc, sub);
            }
        } else if (!keep) {
            left_add(sc->out, e->is_dir ? LK_DIR : LK_FILE, p, NULL, child_conf, it->reason, NULL);
        }
        xfree(p);
    }
    fs_list_free(&v);
}

static void protect_others(Scan *sc) {
    size_t n = sc->out->items.n;
    for (size_t i = 0; i < n; i++) {
        Leftover *it = sc->out->items.items[i];
        // regra 3: pasta que contém outro programa registrado
        if (it->kind == LK_DIR) {
            const WCHAR *inside = NULL;
            for (size_t j = 0; j < sc->other_dirs.n && !inside; j++)
                if (path_is_under_or_eq(sc->other_dirs.items[j], it->path)) inside = sc->other_dirs.items[j];
            if (inside) {
                xfree(it->detail);
                it->detail = wfmt(T(S_CONTAINS_OTHER), inside);
                log_fmt(u"  pasta com outro programa dentro, vai item por item: %s (%s)", it->path, inside);
                split_dir(sc, it);
                n = sc->out->items.n;  // split_dir acrescenta itens: eles também passam por estas regras
                continue;
            }
        }
        if (it->reason != LR_NAME || (it->kind != LK_KEY && it->kind != LK_DIR)) continue;
        // regra 2: nome que outro programa instalado também reivindica
        const WCHAR *leaf = path_leaf(it->path);
        int ours = match_name(sc->id, leaf);
        const Identity *rival = NULL;
        for (size_t j = 0; j < sc->others.n && !rival; j++) {
            const Identity *o = sc->others.items[j];
            int theirs = match_name(o, leaf);
            if (theirs && theirs >= ours) rival = o;
        }
        // chave achada por nome parecido (não exato) que guarda caminhos de outro programa
        bool foreign_refs = false;
        if (!rival && it->kind == LK_KEY && it->conf < CONF_HIGH) {
            HKEY k = reg_open(it->path, KEY_READ);
            if (k) {
                size_t budget = 20000;
                foreign_refs = (key_refs(sc, k, 0, &budget) & REF_FOREIGN) != 0;
                RegCloseKey(k);
            }
        }
        if (!rival && !foreign_refs) continue;
        it->conf = CONF_LOW;
        xfree(it->detail);
        it->detail = rival ? wfmt(T(S_ALSO_OTHER), rival->display) : wdup(T(S_KEY_HAS_OTHER));
        WCHAR *path = wdup(it->path);
        bool is_key = it->kind == LK_KEY;
        log_fmt(u"  nome compartilhado com outro programa, desmarcado: %s", path);
        if (is_key) refine_key(sc, path, 0);
        else refine_dir_by_version(sc, path, rival);
        xfree(path);
        n = sc->out->items.n;
    }
}

// ---------------------------------------------------------------- busca completa

void scan_leftovers(LeftList *out, const Identity *id, const WCHAR *uninstall_key, ScanMode mode, Ctl *ctl) {
    scan_leftovers_ex(out, id, uninstall_key, mode, 0, ctl);
}

void scan_leftovers_ex(LeftList *out, const Identity *id, const WCHAR *uninstall_key, ScanMode mode, unsigned flags, Ctl *ctl) {
    Scan sc;
    mem_set(&sc, 0, sizeof(sc));
    sc.out = out, sc.id = id, sc.mode = mode, sc.thr = threshold(mode), sc.ctl = ctl, sc.flags = flags;
    log_fmt(u"Busca de sobras: \"%s\" (modo %d, %u pastas do programa)", id->display, (int)mode, (unsigned)id->dirs.n);
    ctl_progress(ctl, T(S_SCAN_REGISTRY));
    scan_uninstall_entries(&sc, uninstall_key);
    load_others(&sc, uninstall_key);
    scan_reg_names(&sc);
    if (mode >= SCAN_MODERATE && !ctl_cancelled(ctl)) {
        ctl_progress(ctl, T(S_SCAN_STARTUP));
        scan_run(&sc);
        scan_app_paths(&sc);
        scan_shared_dlls(&sc);
        scan_services(&sc);
        scan_usage(&sc);
        scan_usage_named(&sc);
        scan_exe_registry(&sc);
        scan_msi(&sc, uninstall_key);
        scan_path_var(&sc);
        scan_firewall(&sc);
        scan_shortcuts(&sc);
        ctl_progress(ctl, T(S_SCAN_COM));
        scan_classes(&sc);
        scan_shell_verbs(&sc);
        scan_clsid_refs(&sc);
        scan_progid_refs(&sc);
        scan_fileexts(&sc);
        scan_feature_usage(&sc);  // depois dos tipos de arquivo: usa os ProgIDs que vão sair
        scan_autoplay(&sc);
        scan_clients(&sc);
        scan_appx(&sc);
        ctl_progress(ctl, T(S_SCAN_TASKS));
        scan_tasks(&sc);
    }
    if (!ctl_cancelled(ctl)) {
        ctl_progress(ctl, T(S_SCAN_FILES));
        scan_files(&sc);
    }
    if (mode == SCAN_ADVANCED && !ctl_cancelled(ctl)) {
        ctl_progress(ctl, T(S_SCAN_DEEP));
        scan_deep_refs(&sc);
    }
    protect_others(&sc);
    if (mode >= SCAN_MODERATE) scan_registered_apps(&sc);  // depois: depende das chaves que vão sair
    free_others(&sc);
    log_fmt(u"Busca de sobras: %u itens encontrados", (unsigned)out->items.n);
}

void leftovers_guard_others(LeftList *l, const WCHAR *own_key) {
    // para listas montadas fora de scan_leftovers (rastreamento): só as regras 1 e 3
    Identity *none = identity_new(u"", NULL);
    Scan sc;
    mem_set(&sc, 0, sizeof(sc));
    sc.out = l, sc.id = none, sc.mode = SCAN_MODERATE;
    load_others(&sc, own_key);
    size_t n = l->items.n;
    for (size_t i = 0; i < n; i++) {
        Leftover *it = l->items.items[i];
        if (it->kind != LK_DIR) continue;
        for (size_t j = 0; j < sc.other_dirs.n; j++)
            if (path_is_under_or_eq(sc.other_dirs.items[j], it->path)) {
                xfree(it->detail);
                it->detail = wfmt(T(S_CONTAINS_OTHER), sc.other_dirs.items[j]);
                split_dir(&sc, it);
                n = l->items.n;
                break;
            }
    }
    free_others(&sc);
    identity_free(none);
}

void leftovers_measure(LeftList *l, Ctl *ctl) {
    ctl_progress(ctl, T(S_SCAN_SIZES));
    for (size_t i = 0; i < l->items.n && !ctl_cancelled(ctl); i++) {
        Leftover *it = l->items.items[i];
        if (it->kind == LK_DIR || it->kind == LK_DIR_IF_EMPTY) it->size = fs_dir_size(it->path, 50000);
        else if (it->kind == LK_FILE) it->size = file_size(it->path);
    }
}

bool leftover_exists(const Leftover *it) {
    switch (it->kind) {
    case LK_KEY:
    case LK_KEY_IF_EMPTY: return reg_exists(it->path);
    case LK_VALUE: {
        HKEY k = reg_open(it->path, KEY_QUERY_VALUE);
        if (!k) return false;
        DWORD t, sz = 0;
        bool ok = RegQueryValueExW(k, it->value, NULL, &t, NULL, &sz) == ERROR_SUCCESS;
        RegCloseKey(k);
        return ok;
    }
    case LK_SERVICE: {
        WCHAR *k = wcat(u"HKLM\\SYSTEM\\CurrentControlSet\\Services\\", it->path);
        bool ok = reg_exists(k);
        xfree(k);
        return ok;
    }
    case LK_DIR:
    case LK_FILE:
    case LK_DIR_IF_EMPTY: return file_exists(it->path);
    case LK_TASK: {
        WCHAR *f = g_paths.system32 ? wcat3(g_paths.system32, u"\\Tasks", it->path) : wdup(u"");
        bool ok = file_exists(f);
        xfree(f);
        return ok;
    }
    case LK_FWRULE: {
        HKEY k = reg_open(FIREWALL_RULES_KEY, KEY_QUERY_VALUE);
        if (!k) return false;
        DWORD t, sz = 0;
        bool ok = RegQueryValueExW(k, it->path, NULL, &t, NULL, &sz) == ERROR_SUCCESS;
        RegCloseKey(k);
        return ok;
    }
    case LK_PATHENTRY: return true;  // entradas de PATH não vêm de rastreamentos
    case LK_APPX: {
        WCHAR *k = path_join(APPX_PACKAGES_KEY, it->path);
        bool ok = reg_exists(k);
        xfree(k);
        return ok;
    }
    }
    return false;
}

// Texto atual de um valor do Registro (como regvalue_text), ou NULL.
static WCHAR *current_value_text(const WCHAR *key, const WCHAR *name) {
    HKEY k = reg_open(key, KEY_READ);
    if (!k) return NULL;
    WCHAR *r = NULL;
    PtrVec vals = reg_values(k);
    for (size_t i = 0; i < vals.n && !r; i++) {
        RegValue *v = vals.items[i];
        if (wieq(v->name, name ? name : u"")) r = regvalue_text(v);
    }
    reg_values_free(&vals);
    RegCloseKey(k);
    return r;
}

// Um item da análise de antes do desinstalador ainda é do programa? Existir não basta para o que
// foi achado pelo conteúdo: o desinstalador pode ter devolvido a associação de um tipo de
// arquivo a outro programa (".mp3" = "WMP11.AssocFile.MP3") ou tirado só o verbo dele de uma
// chave do Windows. Por isso só voltam os itens achados pelo nome (pastas, arquivos, chaves com o
// nome do programa, valores cujo nome é o rastro) e os valores com o mesmo conteúdo de antes.
static bool still_ours(const Leftover *it) {
    if (!leftover_exists(it)) return false;
    switch (it->kind) {
    case LK_KEY:
    case LK_KEY_IF_EMPTY:
        return it->reason == LR_NAME || it->reason == LR_PUBLISHER || it->reason == LR_APP_PATHS || it->reason == LR_UNINSTALL_KEY ||
               it->reason == LR_MSI || it->reason == LR_EXE_TRACE;
    case LK_VALUE: {
        if (!it->detail || !it->detail[0]) return it->value && it->value[0];  // o nome do valor é o rastro
        WCHAR *now = current_value_text(it->path, it->value);
        bool same = now && wifind(now, it->detail);
        xfree(now);
        return same;
    }
    default: return true;  // pastas, arquivos, serviços, tarefas, regras, pacotes: pelo nome ou caminho
    }
}

size_t leftovers_add_existing(LeftList *dst, const LeftList *src) {
    size_t added = 0;
    for (size_t i = 0; i < src->items.n; i++) {
        const Leftover *it = src->items.items[i];
        if (!still_ours(it)) continue;
        size_t n0 = dst->items.n;
        left_add(dst, it->kind, it->path, it->value, it->conf, it->reason, it->detail);
        added += dst->items.n > n0;
    }
    return added;
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

static WCHAR *task_file(const WCHAR *name) { return g_paths.system32 ? wcat3(g_paths.system32, u"\\Tasks", name) : wdup(u""); }

// Copia o XML da tarefa para a pasta de backups (restaurável pela aba Backups) e remove a tarefa.
static DWORD delete_task(const WCHAR *name, const WCHAR *stamp) {
    WCHAR *file = task_file(name);
    if (!file_exists(file)) {
        xfree(file);
        return ERROR_SUCCESS;
    }
    uint8_t *data;
    size_t len;
    if (!file_read_all(file, &data, &len)) {
        xfree(file);
        return ERROR_ACCESS_DENIED;
    }
    WCHAR *safe = safe_file_label(name + 1), *bname = wfmt(u"%s - task - %s.xml", stamp, safe), *bpath = path_join(g_paths.backups_dir, bname);
    bool saved = file_write_all(bpath, data, len);
    xfree(data), xfree(safe), xfree(bname);
    if (!saved) {
        xfree(file), xfree(bpath);
        return ERROR_CANCELLED;  // sem backup, não apaga
    }
    log_fmt(u"  backup da tarefa: %s", bpath);
    xfree(bpath);
    WCHAR *exe = system_exe(u"schtasks.exe"), *cmd = wfmt(u"\"%s\" /Delete /TN \"%s\" /F", exe, name);
    int code = run_capture(cmd, NULL, NULL, 60000);
    xfree(exe), xfree(cmd);
    DWORD r = file_exists(file) ? (code > 0 ? (DWORD)code : ERROR_ACCESS_DENIED) : ERROR_SUCCESS;
    xfree(file);
    return r;
}

// Remove as regras do firewall pelo próprio Windows (Remove-NetFirewallRule), de uma vez só.
// Se o PowerShell falhar, apaga o valor do Registro: a regra some de vez na reinicialização.
static void delete_fw_rules(PtrVec *items) {
    if (!items->n) return;
    StrBuf s = {0};
    sb_add(&s, u"$ErrorActionPreference='Stop'\n$ids=@(");
    for (size_t i = 0; i < items->n; i++) {
        const WCHAR *id = ((Leftover *)items->items[i])->path;
        sb_add(&s, i ? u",'" : u"'");
        for (const WCHAR *p = id; *p; p++) {
            // aspas simples dobradas: literal do PowerShell, que também aceita as aspas tipográficas
            // (U+2018 a U+201B) como aspas simples
            if (*p == '\'' || (*p >= 0x2018 && *p <= 0x201B)) sb_addc(&s, *p);
            sb_addc(&s, *p);
        }
        sb_addc(&s, '\'');
    }
    sb_add(&s, u")\nforeach($i in $ids){ try { Remove-NetFirewallRule -Name $i } catch {} }\n");
    WCHAR *b64 = base64_utf16le(sb_str(&s));
    WCHAR *ps = g_paths.system32 ? path_join(g_paths.system32, u"WindowsPowerShell\\v1.0\\powershell.exe") : wdup(u"powershell.exe");
    WCHAR *cmd = wfmt(u"\"%s\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand %s", ps, b64);
    run_capture(cmd, NULL, NULL, 120000);
    xfree(b64), xfree(ps), xfree(cmd);
    sb_free(&s);
}

static bool fw_rule_exists(const WCHAR *id) { return value_exists(FIREWALL_RULES_KEY, id); }

static bool same_path_entry(const WCHAR *a, const WCHAR *b) {
    WCHAR *x = path_norm(a), *y = path_norm(b);
    bool eq = wieq(x, y);
    xfree(x), xfree(y);
    return eq;
}

// Tira entradas da variável PATH (de uma chave de ambiente), mantendo o tipo do valor.
static DWORD remove_path_entries(const WCHAR *key, PtrVec *items) {
    DWORD type = 0, size = 0;
    uint8_t *data = reg_get_raw(key, u"Path", &type, &size);
    if (!data) return ERROR_FILE_NOT_FOUND;
    WCHAR *path = wdupn((const WCHAR *)data, size / 2);
    xfree(data);
    StrBuf out = {0};
    for (const WCHAR *p = path; *p;) {
        const WCHAR *e = wfindc(p, ';');
        if (!e) e = p + wlen(p);
        WCHAR *raw = wdupn(p, (size_t)(e - p)), *t = wtrim_dup(raw);
        bool drop = false;
        for (size_t i = 0; i < items->n && !drop; i++) drop = same_path_entry(t, ((Leftover *)items->items[i])->value);
        if (!drop && raw[0]) {
            if (out.len) sb_addc(&out, ';');
            sb_add(&out, raw);
        }
        xfree(raw), xfree(t);
        p = *e ? e + 1 : e;
    }
    xfree(path);
    const WCHAR *s = sb_str(&out);
    DWORD r = reg_set_value(key, u"Path", type == REG_EXPAND_SZ ? REG_EXPAND_SZ : REG_SZ, (const uint8_t *)s, (DWORD)((out.len + 1) * sizeof(WCHAR)));
    sb_free(&out);
    if (r == ERROR_SUCCESS) {
        // avisa os programas abertos (o Explorer) que o ambiente mudou
        DWORD_PTR res;
        SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)u"Environment", SMTO_ABORTIFHUNG, 3000, &res);
    }
    return r;
}

static bool path_entry_present(const WCHAR *key, const WCHAR *entry) {
    HKEY k = reg_open(key, KEY_READ);
    if (!k) return false;
    WCHAR *path = reg_get_str(k, u"Path");
    RegCloseKey(k);
    bool found = false;
    for (const WCHAR *p = path; p && *p && !found;) {
        const WCHAR *e = wfindc(p, ';');
        if (!e) e = p + wlen(p);
        WCHAR *raw = wdupn(p, (size_t)(e - p)), *t = wtrim_dup(raw);
        found = t[0] && same_path_entry(t, entry);
        xfree(raw), xfree(t);
        p = *e ? e + 1 : e;
    }
    xfree(path);
    return found;
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
    case LK_SERVICE: return !it->path[0] || wfindc(it->path, '\\');
    case LK_TASK: return it->path[0] != '\\' || wistarts(it->path, u"\\Microsoft\\") || wfindc(it->path, '"');
    case LK_FWRULE: return !it->path[0];
    case LK_PATHENTRY: return !(path_eq(it->path, ENV_KEY_MACHINE) || path_eq(it->path, ENV_KEY_USER));
    case LK_APPX: return !left_appx_name_ok(it->path);
    }
    return true;
}

static bool appx_registered(const WCHAR *full_name) {
    WCHAR *k = path_join(APPX_PACKAGES_KEY, full_name);
    bool r = reg_exists(k);
    xfree(k);
    return r;
}

// Remove-AppxPackage para todos os usuários; se o Windows recusar, só para o usuário atual.
static DWORD delete_appx(const WCHAR *full_name) {
    WCHAR *err = appx_remove(full_name, true);
    if (err && appx_registered(full_name)) {
        log_fmt(u"  Remove-AppxPackage -AllUsers: %s", err);
        xfree(err);
        err = appx_remove(full_name, false);
    }
    if (err) log_fmt(u"  Remove-AppxPackage: %s", err);
    xfree(err);
    return appx_registered(full_name) ? ERROR_ACCESS_DENIED : ERROR_SUCCESS;
}

void delete_leftovers(LeftList *l, const WCHAR *label, Ctl *ctl, DelResult *res) {
    mem_set(res, 0, sizeof(*res));
    log_fmt(u"Exclusão de sobras: \"%s\"", label);
    PtrVec services = {0}, values = {0}, keys = {0}, keys_if_empty = {0}, files = {0}, dirs_if_empty = {0}, tasks = {0}, fw = {0}, pathent = {0}, appx = {0};
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
        case LK_TASK: pv_push(&tasks, it); break;
        case LK_FWRULE: pv_push(&fw, it); break;
        case LK_PATHENTRY: pv_push(&pathent, it); break;
        case LK_APPX: pv_push(&appx, it); break;
        }
    }
    WCHAR *stampf = now_text(true);
    dir_create_all(g_paths.backups_dir);

    // 1. backup de tudo o que sai do Registro, num .reg que restaura com um clique duplo
    if (services.n + values.n + keys.n + keys_if_empty.n + fw.n + pathent.n) {
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
        for (size_t i = 0; i < fw.n; i++) reg_export_value(&b, FIREWALL_RULES_KEY, ((Leftover *)fw.items[i])->path);
        bool env_m = false, env_u = false;
        for (size_t i = 0; i < pathent.n; i++) {
            Leftover *it = pathent.items[i];
            bool m = path_eq(it->path, ENV_KEY_MACHINE);
            if ((m && !env_m) || (!m && !env_u)) reg_export_value(&b, it->path, u"Path");
            env_m = env_m || m, env_u = env_u || !m;
        }
        WCHAR *safe = safe_file_label(label), *name = wfmt(u"%s - %s.reg", stampf, safe);
        res->backup_path = path_join(g_paths.backups_dir, name);
        xfree(safe), xfree(name);
        if (!file_write_utf16(res->backup_path, sb_str(&b))) {
            // sem backup, nada do Registro é apagado
            res->backup_failed = true;
            log_fmt(u"  backup NÃO gravado em %s: nada do Registro será apagado", res->backup_path);
            PtrVec *all[] = {&services, &values, &keys, &keys_if_empty, &fw, &pathent};
            for (size_t a = 0; a < COUNT(all); a++) {
                for (size_t i = 0; i < all[a]->n; i++) finish(all[a]->items[i], ERROR_CANCELLED, res, u"(sem backup)");
                pv_free(all[a]);
            }
        } else {
            log_fmt(u"  backup do Registro: %s", res->backup_path);
        }
        sb_free(&b);
    }

    // 2. pacotes do Windows, tarefas agendadas e serviços (pelo próprio Windows). Pacote não tem
    // backup: o Windows só o registra de novo pelo instalador do programa.
    if (appx.n + tasks.n + services.n) ctl_progress(ctl, T(S_DEL_SERVICES));
    for (size_t i = 0; i < appx.n; i++) finish(appx.items[i], delete_appx(((Leftover *)appx.items[i])->path), res, u"pacote do Windows");
    for (size_t i = 0; i < tasks.n; i++) finish(tasks.items[i], delete_task(((Leftover *)tasks.items[i])->path, stampf), res, u"tarefa");
    for (size_t i = 0; i < services.n; i++) finish(services.items[i], delete_service(((Leftover *)services.items[i])->path), res, u"serviço");

    // 3. firewall e PATH
    delete_fw_rules(&fw);
    for (size_t i = 0; i < fw.n; i++) {
        Leftover *it = fw.items[i];
        DWORD err = ERROR_SUCCESS;
        if (fw_rule_exists(it->path)) {
            err = reg_delete_value(FIREWALL_RULES_KEY, it->path);  // plano B: some na reinicialização
            if (err == ERROR_SUCCESS) log_fmt(u"  regra do firewall removida pelo Registro (vale após reiniciar): %s", it->path);
        }
        finish(it, err, res, u"regra do firewall");
    }
    static const WCHAR *const ENV_KEYS[] = {ENV_KEY_MACHINE, ENV_KEY_USER};
    for (size_t e = 0; e < COUNT(ENV_KEYS); e++) {
        PtrVec mine = {0};
        for (size_t i = 0; i < pathent.n; i++)
            if (path_eq(((Leftover *)pathent.items[i])->path, ENV_KEYS[e])) pv_push(&mine, pathent.items[i]);
        if (mine.n) {
            DWORD r = remove_path_entries(ENV_KEYS[e], &mine);
            for (size_t i = 0; i < mine.n; i++) {
                Leftover *it = mine.items[i];
                finish(it, r == ERROR_SUCCESS && path_entry_present(it->path, it->value) ? ERROR_ACCESS_DENIED : r, res, u"entrada do PATH");
            }
        }
        pv_free(&mine);
    }

    // 4. Registro: valores, depois chaves (as mais fundas primeiro), depois as do editor se vazias
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

    // 5. arquivos e pastas: para a Lixeira. O que não cabe nela (caminho longo demais, unidade
    // sem Lixeira) é apagado direto.
    if (files.n) ctl_progress(ctl, T(S_DEL_FILES));
    for (size_t i = 0; i < files.n && !ctl_cancelled(ctl); i++) {
        Leftover *it = files.items[i];
        DWORD err = ERROR_SUCCESS;
        if (file_exists(it->path)) {
            err = fs_recycle(it->path);
            if (err != ERROR_SUCCESS && file_exists(it->path)) {
                DWORD e2 = fs_delete_tree(it->path);
                if (!file_exists(it->path)) {
                    log_fmt(u"  apagado sem passar pela Lixeira: %s", it->path);
                    res->permanent++;
                    err = ERROR_SUCCESS;
                } else if (e2) {
                    err = e2;
                }
            }
        }
        finish(it, err, res, it->kind == LK_FILE ? u"arquivo" : u"pasta");
    }
    pv_sort(&dirs_if_empty, cmp_deepest_first, NULL);
    for (size_t i = 0; i < dirs_if_empty.n; i++) {
        Leftover *it = dirs_if_empty.items[i];
        if (!dir_exists(it->path)) finish(it, ERROR_SUCCESS, res, u"pasta");
        else if (fs_dir_empty(it->path)) finish(it, RemoveDirectoryW(it->path) ? ERROR_SUCCESS : GetLastError(), res, u"pasta vazia");
        else it->status = LS_KEPT, res->kept++;
    }
    log_fmt(u"Exclusão de sobras: %d apagados, %d falharam, %d mantidos", res->deleted, res->failed, res->kept);
    xfree(stampf);
    pv_free(&services), pv_free(&values), pv_free(&keys), pv_free(&keys_if_empty), pv_free(&files), pv_free(&dirs_if_empty);
    pv_free(&tasks), pv_free(&fw), pv_free(&pathent), pv_free(&appx);
}

void del_result_free(DelResult *r) {
    xfree(r->backup_path);
    r->backup_path = NULL;
}
