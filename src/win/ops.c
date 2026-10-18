// SPDX-License-Identifier: MIT
#include "ops.h"

#include "../core/cmdline.h"
#include "../core/fmt.h"
#include "../core/pathx.h"
#include "dialogs.h"
#include "i18n.h"
#include "leftwin.h"
#include "monitor.h"
#include "platform.h"
#include "progress.h"
#include "reg.h"
#include "scan.h"
#include "ui.h"
#include "uninst.h"

// ---------------------------------------------------------------- desinstalar

typedef struct {
    WCHAR *name, *key, *cmd;
    Identity *id;
    bool still_registered;
    WCHAR *problem;  // texto do que deu errado (para a nota da janela de sobras)
} Target;

typedef struct {
    Target *t;
    size_t n;
    UninstallOpts opts;
    bool run_uninstallers;
    LeftList list;
    StrBuf notes;
    bool cancelled;  // a busca de sobras foi cancelada
} UninstallJob;

static void add_note(UninstallJob *job, const WCHAR *text) {
    if (job->notes.len) sb_add(&job->notes, u"\n");
    sb_add(&job->notes, text);
}

static void uninstall_work(Progress *pg, void *arg) {
    UninstallJob *job = arg;
    INT64 seq = 0;
    bool rp = false;
    // enquanto o desinstalador roda, "Cancelar" não faria nada: só fica ativo na busca
    pg_allow_cancel(pg, !job->run_uninstallers);
    if (job->run_uninstallers && job->opts.restore_point) {
        pg_step(pg, T(S_STEP_RESTORE_POINT));
        WCHAR *desc = job->n == 1 ? wfmt(T(S_RP_DESC_ONE), job->t[0].name) : wfmt(T(S_RP_DESC_MANY), (unsigned)job->n);
        DWORD err;
        rp = restore_point_begin(desc, &seq, &err);
        xfree(desc);
        if (!rp) {
            WCHAR *e = win_error_text(err), *t = wfmt(T(S_NOTE_NO_RESTORE_POINT), e);
            add_note(job, t);
            xfree(e), xfree(t);
        }
    }
    for (size_t i = 0; job->run_uninstallers && i < job->n; i++) {
        Target *t = &job->t[i];
        WCHAR *step = job->n == 1 ? wfmt(T(S_STEP_UNINSTALLING_ONE), t->name) : wfmt(T(S_STEP_UNINSTALLING_MANY), t->name, (unsigned)(i + 1), (unsigned)job->n);
        pg_step(pg, step);
        xfree(step);
        if (!t->cmd) {
            WCHAR *note = wfmt(T(S_NOTE_NO_UNINSTALLER), t->name);
            add_note(job, note);
            xfree(note);
            continue;
        }
        pg_detail(pg, T(S_DETAIL_WAITING_UNINSTALLER));
        ResetEvent(pg_button_event(pg));
        pg_button(pg, T(S_BTN_SKIP_WAIT));
        RunResult rr;
        run_and_wait(t->cmd, pg_button_event(pg), &rr);
        pg_button(pg, NULL);
        if (!rr.started) {
            WCHAR *e = win_error_text(rr.error), *note = wfmt(T(S_NOTE_UNINSTALLER_FAILED), t->name, e);
            add_note(job, note);
            xfree(e), xfree(note);
        }
        // o Windows Installer termina de gravar o Registro logo depois do msiexec sair
        Sleep(1500);
        t->still_registered = t->key && reg_exists(t->key);
        if (t->still_registered && rr.started) {
            WCHAR *note = wfmt(T(S_NOTE_STILL_INSTALLED), t->name);
            add_note(job, note);
            xfree(note);
        }
    }
    if (rp) restore_point_end(seq, false);

    pg_allow_cancel(pg, true);
    pg_step(pg, T(S_STEP_SCANNING));
    bool any_still = false;
    for (size_t i = 0; i < job->n && !pg_cancelled(pg); i++) {
        scan_leftovers(&job->list, job->t[i].id, job->t[i].key, job->opts.mode, pg_ctl(pg));
        any_still = any_still || job->t[i].still_registered;
    }
    job->cancelled = pg_cancelled(pg);
    left_finalize(&job->list);
    leftovers_measure(&job->list, pg_ctl(pg));
    if (any_still) {
        // o programa continua instalado (desinstalação cancelada?): nada vem marcado
        for (size_t i = 0; i < job->list.items.n; i++) ((Leftover *)job->list.items.items[i])->checked = false;
    }
}

static void show_result(HWND owner, const WCHAR *label, UninstallJob *job, bool cancelled) {
    const WCHAR *note = job->notes.len ? sb_str(&job->notes) : NULL;
    if (cancelled && !job->list.items.n) {
        msg(owner, T(S_SCAN_CANCELLED), MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!job->list.items.n) {
        StrBuf b = {0};
        sb_add(&b, T(S_NO_LEFTOVERS));
        if (note) sb_add(&b, u"\n\n"), sb_add(&b, note);
        msg(owner, sb_str(&b), MB_OK | MB_ICONINFORMATION);
        sb_free(&b);
        return;
    }
    leftovers_show(owner, label, note, &job->list);
}

bool op_uninstall(HWND owner, Program *const *progs, size_t n, bool quick) {
    if (!n) return false;
    UninstallJob job;
    mem_set(&job, 0, sizeof(job));
    if (!dlg_confirm_uninstall(owner, progs, n, quick, &job.opts)) return false;
    job.run_uninstallers = true;
    job.n = n;
    job.t = xalloc(n * sizeof(Target));
    // tudo o que identifica o programa é lido ANTES: depois de desinstalado, a entrada some
    for (size_t i = 0; i < n; i++) {
        job.t[i].name = wdup(progs[i]->name);
        job.t[i].key = wdup(progs[i]->key_path);
        job.t[i].cmd = uninstall_command(progs[i], job.opts.quiet, NULL);
        job.t[i].id = program_identity(progs[i]);
    }
    WCHAR *label = n == 1 ? wdup(progs[0]->name) : wfmt(T(S_LABEL_MANY), (unsigned)n);
    log_fmt(u"Desinstalar: %s", label);
    progress_run(owner, T(S_UNINSTALL), uninstall_work, &job);
    show_result(owner, label, &job, job.cancelled);
    for (size_t i = 0; i < n; i++) {
        xfree(job.t[i].name), xfree(job.t[i].key), xfree(job.t[i].cmd), xfree(job.t[i].problem);
        identity_free(job.t[i].id);
    }
    xfree(job.t), xfree(label);
    left_free(&job.list);
    sb_free(&job.notes);
    return true;
}

// ---------------------------------------------------------------- desinstalação forçada

bool op_forced(HWND owner, const WCHAR *name, const WCHAR *folder) {
    WCHAR *n = name ? wdup(name) : NULL, *f = folder ? wdup(folder) : NULL;
    ScanMode mode = SCAN_MODERATE;
    if (!dlg_forced(owner, &n, &f, &mode)) {
        xfree(n), xfree(f);
        return false;
    }
    UninstallJob job;
    mem_set(&job, 0, sizeof(job));
    job.n = 1;
    job.opts.mode = mode;
    job.t = xalloc(sizeof(Target));
    const WCHAR *label = n[0] ? n : path_leaf(f);
    job.t[0].name = wdup(label);
    job.t[0].id = identity_new(label, NULL);
    if (f[0]) {
        identity_add_dir(job.t[0].id, f);
        identity_add_exact(job.t[0].id, path_leaf(f));
    }
    log_fmt(u"Desinstalação forçada: nome \"%s\", pasta \"%s\"", n, f);
    progress_run(owner, T(S_FORCED_TITLE), uninstall_work, &job);
    show_result(owner, label, &job, job.cancelled);
    xfree(job.t[0].name);
    identity_free(job.t[0].id);
    xfree(job.t);
    left_free(&job.list);
    sb_free(&job.notes);
    xfree(n), xfree(f);
    return true;
}

// ---------------------------------------------------------------- rastrear instalação

typedef struct {
    WCHAR *installer;
    Trace *trace;
    bool ok;
} TraceJob;

static void trace_work(Progress *pg, void *arg) {
    TraceJob *job = arg;
    Snapshot s;
    mem_set(&s, 0, sizeof(s));
    pg_step(pg, T(S_STEP_SNAPSHOT1));
    monitor_snapshot(&s, false, pg_ctl(pg));
    if (pg_cancelled(pg)) goto out;

    pg_step(pg, T(S_STEP_INSTALLING));
    pg_detail(pg, job->installer);
    WCHAR *cmd;
    if (wiends(job->installer, u".msi")) {
        WCHAR *m = system_exe(u"msiexec.exe"), *a = wfmt(u"/i \"%s\"", job->installer);
        cmd = cmd_join(m, a);
        xfree(m), xfree(a);
    } else {
        cmd = wfmt(u"\"%s\"", job->installer);
    }
    ResetEvent(pg_button_event(pg));
    pg_button(pg, T(S_BTN_FINISH_TRACE));
    pg_allow_cancel(pg, false);  // durante a instalação, só "Concluir rastreamento"
    RunResult rr;
    run_and_wait(cmd, pg_button_event(pg), &rr);
    xfree(cmd);
    if (!rr.started) {
        WCHAR *e = win_error_text(rr.error), *t = wfmt(T(S_ERR_INSTALLER), e);
        pg_ask(pg, t, MB_OK | MB_ICONERROR);
        xfree(e), xfree(t);
        pg_button(pg, NULL);
        goto out;
    }
    pg_allow_cancel(pg, true);
    if (!rr.skipped) {
        // o instalador terminou: dá a chance de abrir o programa uma vez antes da segunda foto
        pg_step(pg, T(S_STEP_INSTALL_DONE));
        pg_detail(pg, T(S_DETAIL_INSTALL_DONE));
        while (WaitForSingleObject(pg_button_event(pg), 200) == WAIT_TIMEOUT && !pg_cancelled(pg)) {
        }
    }
    pg_button(pg, NULL);
    if (pg_cancelled(pg)) goto out;
    pg_allow_cancel(pg, false);
    pg_step(pg, T(S_STEP_SNAPSHOT2));
    s.out = trace_new();
    monitor_snapshot(&s, true, pg_ctl(pg));
    job->trace = s.out;
    job->ok = true;
out:
    hs_free(&s.before);
}

bool op_trace_install(HWND owner) {
    WCHAR *file = dlg_pick_file(owner, T(S_PICK_INSTALLER), T(S_FILTER_INSTALLERS));
    if (!file) return false;
    if (msg(owner, T(S_TRACE_TIPS), MB_OKCANCEL | MB_ICONINFORMATION) != IDOK) {
        xfree(file);
        return false;
    }
    TraceJob job = {file, NULL, false};
    log_fmt(u"Rastrear instalação: %s", file);
    progress_run(owner, T(S_TRACE_TITLE), trace_work, &job);
    bool saved = false;
    if (job.ok && job.trace) {
        WCHAR *guess = trace_guess_name(job.trace);
        if (!guess) guess = path_strip_ext(path_leaf(file));
        WCHAR *prompt = wfmt(T(S_TRACE_NAME_PROMPT), (unsigned)job.trace->items.n);
        WCHAR *name = dlg_input(owner, T(S_TRACE_TITLE), prompt, guess);
        xfree(prompt), xfree(guess);
        if (name) {
            job.trace->name = name;
            job.trace->installer = wdup(file);
            job.trace->date = now_text(false);
            WCHAR *path = NULL;
            if (trace_save(job.trace, &path)) {
                WCHAR *t = wfmt(T(S_TRACE_SAVED), name, (unsigned)job.trace->items.n);
                msg(owner, t, MB_OK | MB_ICONINFORMATION);
                xfree(t);
                log_fmt(u"Rastreamento salvo: %s (%u itens)", path, (unsigned)job.trace->items.n);
                saved = true;
            } else {
                msg(owner, T(S_ERR_SAVE_TRACE), MB_OK | MB_ICONERROR);
            }
            xfree(path);
        }
    } else {
        msg(owner, T(S_TRACE_CANCELLED), MB_OK | MB_ICONINFORMATION);
    }
    trace_free(job.trace);
    xfree(file);
    return saved;
}

// ---------------------------------------------------------------- desinstalar pelo rastreamento

static bool item_exists(const Leftover *it) {
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
    }
    return false;
}

typedef struct {
    UninstallJob base;
    Trace *trace;
} TraceUninstallJob;

static void trace_uninstall_work(Progress *pg, void *arg) {
    TraceUninstallJob *job = arg;
    uninstall_work(pg, &job->base);  // desinstalador (se houver) e a busca normal de sobras
    // mais tudo o que a instalação criou e ainda existe
    LeftList extra;
    mem_set(&extra, 0, sizeof(extra));
    left_add_trace(&extra, job->trace);
    for (size_t i = 0; i < extra.items.n; i++) {
        Leftover *it = extra.items.items[i];
        if (!item_exists(it)) continue;
        left_add(&job->base.list, it->kind, it->path, it->value, it->conf, it->reason, it->detail);
    }
    left_free(&extra);
    leftovers_guard_others(&job->base.list, job->base.n ? job->base.t[0].key : NULL);
    left_finalize(&job->base.list);
    leftovers_measure(&job->base.list, pg_ctl(pg));
    if (job->base.n && job->base.t[0].still_registered)
        for (size_t i = 0; i < job->base.list.items.n; i++) ((Leftover *)job->base.list.items.items[i])->checked = false;
}

bool op_trace_uninstall(HWND owner, const WCHAR *trace_path, const PtrVec *programs) {
    Trace *t = trace_load(trace_path);
    if (!t) {
        msg(owner, T(S_ERR_LOAD_TRACE), MB_OK | MB_ICONERROR);
        return false;
    }
    // o programa rastreado ainda está instalado?
    Program *prog = NULL;
    for (size_t i = 0; programs && i < programs->n && !prog; i++) {
        Program *p = programs->items[i];
        if (wieq(p->name, t->name)) prog = p;
    }
    TraceUninstallJob job;
    mem_set(&job, 0, sizeof(job));
    job.trace = t;
    job.base.opts.mode = (ScanMode)setting_get_int(u"scan_mode", SCAN_MODERATE);
    job.base.n = 1;
    job.base.t = xalloc(sizeof(Target));
    if (prog) {
        WCHAR *q = wfmt(T(S_TRACE_RUN_UNINSTALLER), prog->name);
        int a = msg(owner, q, MB_YESNOCANCEL | MB_ICONQUESTION);
        xfree(q);
        if (a == IDCANCEL) {
            trace_free(t);
            xfree(job.base.t);
            return false;
        }
        if (a == IDYES) {
            job.base.run_uninstallers = true;
            job.base.opts.restore_point = setting_get_int(u"restore_point", 1) != 0;
            job.base.t[0].cmd = uninstall_command(prog, false, NULL);
        }
        job.base.t[0].name = wdup(prog->name);
        job.base.t[0].key = wdup(prog->key_path);
        job.base.t[0].id = program_identity(prog);
    } else {
        job.base.t[0].name = wdup(t->name);
        job.base.t[0].id = identity_new(t->name, NULL);
    }
    log_fmt(u"Desinstalar pelo rastreamento: %s", trace_path);
    progress_run(owner, T(S_UNINSTALL), trace_uninstall_work, &job);
    show_result(owner, job.base.t[0].name, &job.base, job.base.cancelled);
    xfree(job.base.t[0].name), xfree(job.base.t[0].key), xfree(job.base.t[0].cmd);
    identity_free(job.base.t[0].id);
    xfree(job.base.t);
    left_free(&job.base.list);
    sb_free(&job.base.notes);
    trace_free(t);
    return true;
}
