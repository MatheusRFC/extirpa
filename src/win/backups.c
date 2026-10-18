// SPDX-License-Identifier: MIT
#include "backups.h"

#include "../core/fmt.h"
#include "../core/pathx.h"
#include "fsx.h"
#include "platform.h"

static int cmp_newest(const void *a, const void *b, void *ctx) {
    (void)ctx;
    const BackupFile *x = a, *y = b;
    uint64_t tx = ((uint64_t)x->mtime.dwHighDateTime << 32) | x->mtime.dwLowDateTime;
    uint64_t ty = ((uint64_t)y->mtime.dwHighDateTime << 32) | y->mtime.dwLowDateTime;
    return tx < ty ? 1 : tx > ty ? -1 : 0;
}

PtrVec backups_list(void) {
    PtrVec out = {0};
    PtrVec v = fs_list(g_paths.backups_dir);
    for (size_t i = 0; i < v.n; i++) {
        DirEntry *e = v.items[i];
        if (e->is_dir || !wiends(e->name, u".reg")) continue;
        BackupFile *b = xalloc(sizeof(BackupFile));
        b->path = path_join(g_paths.backups_dir, e->name);
        b->file = wdup(e->name);
        b->size = e->size;
        b->mtime = e->mtime;
        pv_push(&out, b);
    }
    fs_list_free(&v);
    pv_sort(&out, cmp_newest, NULL);
    return out;
}

void backups_list_free(PtrVec *v) {
    for (size_t i = 0; i < v->n; i++) {
        BackupFile *b = v->items[i];
        xfree(b->path), xfree(b->file), xfree(b);
    }
    pv_free(v);
}

WCHAR *backup_restore(const WCHAR *path) {
    WCHAR *reg = system_exe(u"reg.exe");
    WCHAR *cmd = wfmt(u"\"%s\" import \"%s\"", reg, path);
    xfree(reg);
    char *out = NULL;
    size_t len = 0;
    int code = run_capture(cmd, &out, &len, 120000);
    xfree(cmd);
    log_fmt(u"Restaurar backup: %s -> código %d", path, code);
    WCHAR *r = NULL;
    if (code != 0) {
        // o reg.exe escreve na página de código do console; caracteres fora do ASCII podem
        // sair trocados aqui, mas a mensagem continua legível
        WCHAR *w = out && len ? utf8_to_w(out, len, NULL) : wfmt(u"reg.exe: %d", code);
        r = wtrim_dup(w);
        xfree(w);
    }
    xfree(out);
    return r;
}
