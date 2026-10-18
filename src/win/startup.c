// SPDX-License-Identifier: MIT
#include "startup.h"

#include "../core/cmdline.h"
#include "../core/lnk.h"
#include "../core/pathx.h"
#include "../core/regtext.h"
#include "fsx.h"
#include "i18n.h"
#include "platform.h"
#include "reg.h"

#define APPROVED_HKCU u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\"
#define APPROVED_HKLM u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\"

static const struct {
    StartupSource src;
    const WCHAR *key;       // chave Run/RunOnce (NULL para pastas)
    const WCHAR *approved;  // onde fica o "ativado/desativado" (NULL: não tem)
} SOURCES[] = {
    {SRC_HKCU_RUN, u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", APPROVED_HKCU u"Run"},
    {SRC_HKCU_RUNONCE, u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce", NULL},
    {SRC_HKLM_RUN, u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", APPROVED_HKLM u"Run"},
    {SRC_HKLM_RUNONCE, u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce", NULL},
    {SRC_HKLM32_RUN, u"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run", APPROVED_HKLM u"Run32"},
    {SRC_HKLM32_RUNONCE, u"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\RunOnce", NULL},
    {SRC_USER_FOLDER, NULL, APPROVED_HKCU u"StartupFolder"},
    {SRC_COMMON_FOLDER, NULL, APPROVED_HKLM u"StartupFolder"},
};

static const WCHAR *approved_key(StartupSource s) {
    for (size_t i = 0; i < COUNT(SOURCES); i++)
        if (SOURCES[i].src == s) return SOURCES[i].approved;
    return NULL;
}

const WCHAR *startup_source_text(StartupSource s) {
    switch (s) {
    case SRC_HKCU_RUN: return u"HKCU\\…\\Run";
    case SRC_HKCU_RUNONCE: return u"HKCU\\…\\RunOnce";
    case SRC_HKLM_RUN: return u"HKLM\\…\\Run";
    case SRC_HKLM_RUNONCE: return u"HKLM\\…\\RunOnce";
    case SRC_HKLM32_RUN: return u"HKLM\\WOW6432Node\\…\\Run";
    case SRC_HKLM32_RUNONCE: return u"HKLM\\WOW6432Node\\…\\RunOnce";
    case SRC_USER_FOLDER: return T(S_STARTUP_FOLDER_USER);
    case SRC_COMMON_FOLDER: return T(S_STARTUP_FOLDER_ALL);
    }
    return u"";
}

static bool read_enabled(const WCHAR *approved, const WCHAR *name) {
    if (!approved) return true;
    HKEY k = reg_open(approved, KEY_READ);
    if (!k) return true;
    uint8_t data[64];
    DWORD type = 0, size = sizeof(data);
    LSTATUS st = RegQueryValueExW(k, name, NULL, &type, data, &size);
    RegCloseKey(k);
    if (st != ERROR_SUCCESS || size < 1) return true;
    return (data[0] & 1) == 0;
}

static void add_entry(PtrVec *v, StartupSource src, const WCHAR *name, const WCHAR *command, const WCHAR *location) {
    StartupEntry *e = xalloc(sizeof(StartupEntry));
    e->name = wdup(name);
    e->command = wdup(command);
    e->location = wdup(location);
    e->source = src;
    e->can_toggle = approved_key(src) != NULL;
    e->enabled = read_enabled(approved_key(src), name);
    pv_push(v, e);
}

static void list_folder(PtrVec *out, StartupSource src, const WCHAR *dir) {
    if (!dir) return;
    PtrVec v = fs_list(dir);
    for (size_t i = 0; i < v.n; i++) {
        DirEntry *d = v.items[i];
        if (d->is_dir || wieq(d->name, u"desktop.ini")) continue;
        WCHAR *path = path_join(dir, d->name), *cmd = NULL;
        if (wiends(d->name, u".lnk")) {
            uint8_t *data;
            size_t len;
            if (file_read_all(path, &data, &len)) {
                LnkInfo l;
                if (lnk_parse(data, len, &l)) {
                    if (l.target) cmd = (l.args && l.args[0]) ? wcat3(l.target, u" ", l.args) : wdup(l.target);
                    lnk_free(&l);
                }
                xfree(data);
            }
        }
        add_entry(out, src, d->name, cmd ? cmd : path, path);
        xfree(cmd), xfree(path);
    }
    fs_list_free(&v);
}

PtrVec startup_list(void) {
    PtrVec v = {0};
    for (size_t s = 0; s < COUNT(SOURCES); s++) {
        if (!SOURCES[s].key) continue;
        HKEY k = reg_open(SOURCES[s].key, KEY_READ);
        if (!k) continue;
        PtrVec vals = reg_values(k);
        for (size_t i = 0; i < vals.n; i++) {
            RegValue *rv = vals.items[i];
            if ((rv->type != REG_SZ && rv->type != REG_EXPAND_SZ) || !rv->name[0]) continue;
            WCHAR *cmd = wdupn((const WCHAR *)rv->data, rv->size / 2);
            add_entry(&v, SOURCES[s].src, rv->name, cmd, SOURCES[s].key);
            xfree(cmd);
        }
        reg_values_free(&vals);
        RegCloseKey(k);
    }
    list_folder(&v, SRC_USER_FOLDER, g_paths.startup);
    list_folder(&v, SRC_COMMON_FOLDER, g_paths.common_startup);
    return v;
}

void startup_list_free(PtrVec *v) {
    for (size_t i = 0; i < v->n; i++) {
        StartupEntry *e = v->items[i];
        xfree(e->name), xfree(e->command), xfree(e->location), xfree(e);
    }
    pv_free(v);
}

DWORD startup_set_enabled(const StartupEntry *e, bool enabled) {
    const WCHAR *ap = approved_key(e->source);
    if (!ap) return ERROR_ACCESS_DENIED;
    // 12 bytes, como o Gerenciador de Tarefas grava: 02 = ativado, 03 = desativado
    uint8_t data[12] = {0};
    data[0] = enabled ? 0x02 : 0x03;
    DWORD r = reg_set_binary(ap, e->name, data, sizeof(data));
    log_fmt(u"Inicialização: %s \"%s\" (%s) -> %s", enabled ? u"ativar" : u"desativar", e->name, e->location, r ? u"falhou" : u"ok");
    return r;
}

DWORD startup_delete(const StartupEntry *e, WCHAR **backup_path) {
    *backup_path = NULL;
    const WCHAR *ap = approved_key(e->source);
    StrBuf b = {0};
    regtext_header(&b);
    bool any = false;
    if (e->source != SRC_USER_FOLDER && e->source != SRC_COMMON_FOLDER) any = reg_export_value(&b, e->location, e->name);
    if (ap) any = reg_export_value(&b, ap, e->name) || any;
    if (any) {
        WCHAR *stamp = now_text(true), *name = wfmt(u"%s - startup.reg", stamp);
        *backup_path = path_join(g_paths.backups_dir, name);
        xfree(stamp), xfree(name);
        dir_create_all(g_paths.backups_dir);
        if (!file_write_utf16(*backup_path, sb_str(&b))) {
            sb_free(&b);
            xfree(*backup_path);
            *backup_path = NULL;
            return ERROR_CANCELLED;  // sem backup, não apaga
        }
    }
    sb_free(&b);
    DWORD r;
    if (e->source == SRC_USER_FOLDER || e->source == SRC_COMMON_FOLDER) r = fs_recycle(e->location);
    else r = reg_delete_value(e->location, e->name);
    if (r == ERROR_SUCCESS && ap) reg_delete_value(ap, e->name);
    log_fmt(u"Inicialização: excluir \"%s\" (%s) -> %s", e->name, e->location, r ? u"falhou" : u"ok");
    return r;
}

WCHAR *startup_exe(const StartupEntry *e) {
    WCHAR *x = expand_env(e->command);
    Cmd c;
    WCHAR *r = NULL;
    if (cmd_split(x, &c)) r = path_norm(c.exe);
    cmd_free(&c);
    xfree(x);
    return r;
}
