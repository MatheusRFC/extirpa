// SPDX-License-Identifier: MIT
#include "leftwin.h"

#include "../core/fmt.h"
#include "../core/pathx.h"
#include "fsx.h"
#include "i18n.h"
#include "platform.h"
#include "progress.h"
#include "scan.h"
#include "ui.h"

enum { ID_HEADER = 100, ID_NOTE, ID_HINT, ID_LIST, ID_ALL, ID_NONE, ID_DELETE, ID_MENU_OPEN = 300, ID_MENU_CHECK, ID_MENU_UNCHECK };

typedef struct {
    ModalState ms;
    LeftList *list;
    const WCHAR *label, *note;
    HWND header, note_lbl, hint, lv, all, none, del, close;
} LeftState;

static const WCHAR *kind_text(LeftKind k) {
    switch (k) {
    case LK_KEY: return T(S_KIND_KEY);
    case LK_VALUE: return T(S_KIND_VALUE);
    case LK_KEY_IF_EMPTY: return T(S_KIND_KEY_IF_EMPTY);
    case LK_DIR: return T(S_KIND_DIR);
    case LK_FILE: return T(S_KIND_FILE);
    case LK_DIR_IF_EMPTY: return T(S_KIND_DIR_IF_EMPTY);
    case LK_SERVICE: return T(S_KIND_SERVICE);
    case LK_TASK: return T(S_KIND_TASK);
    case LK_FWRULE: return T(S_KIND_FWRULE);
    case LK_PATHENTRY: return T(S_KIND_PATHENTRY);
    }
    return u"";
}

static const WCHAR *conf_text(int c) { return c >= CONF_HIGH ? T(S_CONF_HIGH) : c == CONF_MEDIUM ? T(S_CONF_MEDIUM) : T(S_CONF_LOW); }

static const WCHAR *reason_text(LeftReason r) {
    static const StrId ids[] = {S_REASON_UNINSTALL_KEY, S_REASON_INSTALL_DIR, S_REASON_NAME,   S_REASON_PUBLISHER, S_REASON_PATH_REF,
                                S_REASON_RUN,           S_REASON_APP_PATHS,   S_REASON_SHARED, S_REASON_SERVICE,   S_REASON_SHORTCUT,
                                S_REASON_COM,           S_REASON_FILE_ASSOC,  S_REASON_TRACE,  S_REASON_USER,      S_REASON_TASK,
                                S_REASON_FIREWALL,      S_REASON_PATH_VAR,    S_REASON_USAGE,  S_REASON_SHELL_EXT, S_REASON_REFERENCE};
    _Static_assert(COUNT(ids) == LR_REFERENCE + 1, "um texto para cada LeftReason");
    return (unsigned)r < COUNT(ids) ? T(ids[r]) : u"";
}

static WCHAR *item_text(const Leftover *it) {
    if (it->kind == LK_VALUE) return wfmt(u"%s  →  %s", it->path, it->value && it->value[0] ? it->value : T(S_DEFAULT_VALUE));
    if (it->kind == LK_PATHENTRY) return wfmt(u"PATH  →  %s  (%s)", it->value, it->path);
    return wdup(it->path);
}

static WCHAR *status_text(const Leftover *it) {
    switch (it->status) {
    case LS_DELETED: return wdup(T(S_STATUS_DELETED));
    case LS_KEPT: return wdup(T(S_STATUS_KEPT));
    case LS_FAILED: {
        WCHAR *e = win_error_text(it->error), *r = wfmt(T(S_STATUS_FAILED), e);
        xfree(e);
        return r;
    }
    }
    return wdup(u"");
}

static void update_header(LeftState *st) {
    size_t pending = 0;
    for (size_t i = 0; i < st->list->items.n; i++)
        if (((Leftover *)st->list->items.items[i])->status != LS_DELETED) pending++;
    WCHAR *t = st->list->items.n ? wfmt(T(S_LEFT_HEADER), (unsigned)pending, st->label) : wfmt(T(S_LEFT_NONE), st->label);
    SetWindowTextW(st->header, t);
    xfree(t);
    EnableWindow(st->del, pending > 0);
}

static void fill(LeftState *st) {
    lv_clear(st->lv);
    for (size_t i = 0; i < st->list->items.n; i++) {
        Leftover *it = st->list->items.items[i];
        WCHAR *txt = item_text(it);
        int row = lv_insert(st->lv, (int)i, txt, (LPARAM)i);
        xfree(txt);
        lv_text(st->lv, row, 1, kind_text(it->kind));
        lv_text(st->lv, row, 2, conf_text(it->conf));
        WCHAR *size = it->size ? fmt_size(it->size, &g_fmt) : wdup(u"");
        lv_text(st->lv, row, 3, size);
        xfree(size);
        WCHAR *why = it->detail ? wfmt(u"%s: %s", reason_text(it->reason), it->detail) : wdup(reason_text(it->reason));
        lv_text(st->lv, row, 4, why);
        xfree(why);
        WCHAR *stt = status_text(it);
        lv_text(st->lv, row, 5, stt);
        xfree(stt);
        lv_set_checked(st->lv, row, it->checked && it->status != LS_DELETED);
    }
    update_header(st);
}

static void layout(LeftState *st, HWND h) {
    RECT r;
    GetClientRect(h, &r);
    int m = S(14), w = r.right - 2 * m, y = m;
    ui_place(st->header, m, y, w, S(26));
    y += S(30);
    if (st->note) {
        ui_place(st->note_lbl, m, y, w, S(36));
        y += S(40);
    }
    ui_place(st->hint, m, y, w, S(36));
    y += S(40);
    int bh = S(28), by = r.bottom - m - bh;
    ui_place(st->lv, m, y, w, by - S(10) - y);
    int x = m;
    int w1 = ui_button_width(T(S_CHECK_ALL)), w2 = ui_button_width(T(S_UNCHECK_ALL));
    ui_place(st->all, x, by, w1, bh);
    ui_place(st->none, x + w1 + S(8), by, w2, bh);
    int wc = ui_button_width(T(S_CLOSE)), wd = ui_button_width(T(S_DELETE_CHECKED));
    ui_place(st->close, r.right - m - wc, by, wc, bh);
    ui_place(st->del, r.right - m - wc - S(8) - wd, by, wd, bh);
}

typedef struct {
    LeftList *list;
    const WCHAR *label;
    DelResult res;
} DeleteJob;

static void delete_work(Progress *pg, void *arg) {
    DeleteJob *job = arg;
    pg_allow_cancel(pg, false);
    pg_step(pg, T(S_DELETING));
    delete_leftovers(job->list, job->label, pg_ctl(pg), &job->res);
}

static void do_delete(LeftState *st, HWND h) {
    size_t n = 0;
    for (int i = 0; i < lv_count(st->lv); i++) {
        Leftover *it = st->list->items.items[lv_param(st->lv, i)];
        it->checked = lv_checked(st->lv, i) && it->status != LS_DELETED;
        if (it->checked) n++;
    }
    if (!n) {
        msg(h, T(S_NOTHING_CHECKED), MB_OK | MB_ICONINFORMATION);
        return;
    }
    WCHAR *q = wfmt(T(S_CONFIRM_DELETE), (unsigned)n);
    int ans = msg(h, q, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
    xfree(q);
    if (ans != IDYES) return;

    // programas abertos a partir das pastas que vão sair seguram os arquivos: oferece fechar
    PtrVec paths = {0};
    for (size_t i = 0; i < st->list->items.n; i++) {
        Leftover *it = st->list->items.items[i];
        if (it->checked && it->status != LS_DELETED && (it->kind == LK_DIR || it->kind == LK_FILE || it->kind == LK_DIR_IF_EMPTY)) pv_push(&paths, it->path);
    }
    PtrVec procs = processes_under(&paths);
    pv_free(&paths);
    if (procs.n) {
        StrBuf list = {0};
        for (size_t i = 0; i < procs.n && i < 15; i++) {
            ProcInfo *p = procs.items[i];
            WCHAR *line = wfmt(u"%s (PID %u)\n", p->exe, (unsigned)p->pid);
            sb_add(&list, line);
            xfree(line);
        }
        WCHAR *t = wfmt(T(S_ASK_KILL), sb_str(&list));
        int a = msg(h, t, MB_YESNOCANCEL | MB_ICONWARNING);
        xfree(t);
        sb_free(&list);
        if (a == IDCANCEL) {
            processes_free(&procs);
            return;
        }
        if (a == IDYES)
            for (size_t i = 0; i < procs.n; i++) {
                ProcInfo *p = procs.items[i];
                log_fmt(u"Encerrando %s (PID %u): %s", p->exe, (unsigned)p->pid, process_kill(p->pid) ? u"ok" : u"falhou");
            }
    }
    processes_free(&procs);

    DeleteJob job;
    mem_set(&job, 0, sizeof(job));
    job.list = st->list;
    job.label = st->label;
    progress_run(h, T(S_DELETING_TITLE), delete_work, &job);
    fill(st);

    StrBuf b = {0};
    WCHAR *sum = wfmt(T(S_DELETE_SUMMARY), job.res.deleted, job.res.failed);
    sb_add(&b, sum);
    xfree(sum);
    if (job.res.kept) {
        WCHAR *k = wfmt(T(S_DELETE_KEPT), job.res.kept);
        sb_add(&b, u"\n"), sb_add(&b, k);
        xfree(k);
    }
    if (job.res.permanent) {
        WCHAR *pm = wfmt(T(S_DELETE_PERMANENT), job.res.permanent);
        sb_add(&b, u"\n"), sb_add(&b, pm);
        xfree(pm);
    }
    if (job.res.backup_failed) sb_add(&b, u"\n\n"), sb_add(&b, T(S_BACKUP_FAILED));
    else if (job.res.backup_path) {
        WCHAR *bk = wfmt(T(S_BACKUP_SAVED), job.res.backup_path);
        sb_add(&b, u"\n\n"), sb_add(&b, bk);
        xfree(bk);
    }
    msg(h, sb_str(&b), MB_OK | (job.res.failed ? MB_ICONWARNING : MB_ICONINFORMATION));
    sb_free(&b);

    // arquivos presos (em uso): oferece apagar na próxima reinicialização
    size_t locked = 0;
    for (size_t i = 0; i < st->list->items.n; i++) {
        Leftover *it = st->list->items.items[i];
        if (it->status == LS_FAILED && (it->kind == LK_FILE || it->kind == LK_DIR) && file_exists(it->path)) locked++;
    }
    if (locked) {
        WCHAR *t = wfmt(T(S_ASK_REBOOT_DELETE), (unsigned)locked);
        if (msg(h, t, MB_YESNO | MB_ICONQUESTION) == IDYES) {
            size_t ok = 0;
            for (size_t i = 0; i < st->list->items.n; i++) {
                Leftover *it = st->list->items.items[i];
                if (it->status == LS_FAILED && (it->kind == LK_FILE || it->kind == LK_DIR) && file_exists(it->path) && fs_delete_on_reboot(it->path)) {
                    ok++;
                    log_fmt(u"  agendado para a reinicialização: %s", it->path);
                }
            }
            WCHAR *done = wfmt(T(S_REBOOT_SCHEDULED), (unsigned)ok);
            msg(h, done, MB_OK | MB_ICONINFORMATION);
            xfree(done);
        }
        xfree(t);
    }
    del_result_free(&job.res);
}

static void context_menu(LeftState *st, HWND h) {
    int sel = lv_selected(st->lv, -1);
    if (sel < 0) return;
    Leftover *it = st->list->items.items[lv_param(st->lv, sel)];
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_MENU_CHECK, T(S_MENU_CHECK));
    AppendMenuW(m, MF_STRING, ID_MENU_UNCHECK, T(S_MENU_UNCHECK));
    if (left_is_file(it->kind)) {
        AppendMenuW(m, MF_SEPARATOR, 0, NULL);
        AppendMenuW(m, MF_STRING, ID_MENU_OPEN, T(S_MENU_OPEN_LOCATION));
    }
    POINT pt;
    GetCursorPos(&pt);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, NULL);
    DestroyMenu(m);
    if (cmd == ID_MENU_OPEN) {
        if (file_exists(it->path)) shell_select(it->path);
        else {
            WCHAR *p = path_parent(it->path);
            if (p && dir_exists(p)) shell_open(p);
            xfree(p);
        }
    } else if (cmd == ID_MENU_CHECK || cmd == ID_MENU_UNCHECK) {
        for (int i = lv_selected(st->lv, -1); i >= 0; i = lv_selected(st->lv, i)) lv_set_checked(st->lv, i, cmd == ID_MENU_CHECK);
    }
}

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    LeftState *st = ui_state(h, m, lp);
    switch (m) {
    case WM_CREATE:
        st->header = ui_label(h, u"", ID_HEADER);
        SendMessageW(st->header, WM_SETFONT, (WPARAM)ui_font_title(), TRUE);
        if (st->note) {
            st->note_lbl = ui_label(h, st->note, ID_NOTE);
            SendMessageW(st->note_lbl, WM_SETFONT, (WPARAM)ui_font_bold(), TRUE);
        }
        st->hint = ui_label(h, T(S_LEFT_HINT), ID_HINT);
        st->lv = lv_create(h, ID_LIST, 0, LVS_EX_CHECKBOXES);
        lv_column(st->lv, 0, T(S_COL_ITEM), S(430), false);
        lv_column(st->lv, 1, T(S_COL_TYPE), S(130), false);
        lv_column(st->lv, 2, T(S_COL_CONFIDENCE), S(80), false);
        lv_column(st->lv, 3, T(S_COL_SIZE), S(80), true);
        lv_column(st->lv, 4, T(S_COL_REASON), S(330), false);
        lv_column(st->lv, 5, T(S_COL_STATUS), S(200), false);
        st->all = ui_button(h, T(S_CHECK_ALL), ID_ALL);
        st->none = ui_button(h, T(S_UNCHECK_ALL), ID_NONE);
        st->del = ui_button(h, T(S_DELETE_CHECKED), ID_DELETE);
        st->close = ui_button(h, T(S_CLOSE), IDCANCEL);
        fill(st);
        return 0;
    case WM_SIZE:
        layout(st, h);
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = S(640);
        mm->ptMinTrackSize.y = S(380);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_ALL:
        case ID_NONE:
            for (int i = 0; i < lv_count(st->lv); i++) {
                Leftover *it = st->list->items.items[lv_param(st->lv, i)];
                lv_set_checked(st->lv, i, LOWORD(wp) == ID_ALL && it->status != LS_DELETED);
            }
            break;
        case ID_DELETE: do_delete(st, h); break;
        case IDCANCEL: ui_end(&st->ms, IDOK); break;  // Enter (IDOK) não fecha: só Esc e o botão Fechar
        }
        return 0;
    case WM_NOTIFY: {
        NMHDR *nm = (NMHDR *)lp;
        if (nm->idFrom == ID_LIST && nm->code == NM_RCLICK) context_menu(st, h);
        return 0;
    }
    case WM_CLOSE:
        ui_end(&st->ms, IDOK);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

void leftovers_show(HWND owner, const WCHAR *label, const WCHAR *note, LeftList *list) {
    static bool reg;
    if (!reg) reg = ui_register(u"ExtirpaLeftovers", proc) != 0;
    LeftState st;
    mem_set(&st, 0, sizeof(st));
    st.list = list, st.label = label, st.note = note;
    HWND w = ui_window(u"ExtirpaLeftovers", T(S_LEFT_TITLE), owner, &st, WS_OVERLAPPEDWINDOW);
    ui_center(w, owner, S(1080), S(640));
    ui_modal(w, owner, &st.ms);
}
