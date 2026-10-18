// SPDX-License-Identifier: MIT
#include "dialogs.h"

#include "../core/fmt.h"
#include "../core/pathx.h"
#include "i18n.h"
#include "platform.h"
#include "ui.h"

enum {
    ID_TITLE = 100,
    ID_INFO,
    ID_MODE_LABEL,
    ID_SAFE,
    ID_MODERATE,
    ID_ADVANCED,
    ID_RESTORE,
    ID_QUIET,
    ID_NAME_LABEL,
    ID_NAME,
    ID_FOLDER_LABEL,
    ID_FOLDER,
    ID_BROWSE,
    ID_PROMPT,
    ID_EDIT,
};

// ---------------------------------------------------------------- partes comuns

typedef struct {
    HWND safe, moderate, advanced;
} ModeRadios;

static void mode_create(HWND h, ModeRadios *r, ScanMode m) {
    r->safe = ui_radio(h, T(S_MODE_SAFE_LONG), ID_SAFE, true, m == SCAN_SAFE);
    r->moderate = ui_radio(h, T(S_MODE_MODERATE_LONG), ID_MODERATE, false, m == SCAN_MODERATE);
    r->advanced = ui_radio(h, T(S_MODE_ADVANCED_LONG), ID_ADVANCED, false, m == SCAN_ADVANCED);
}

static ScanMode mode_get(const ModeRadios *r) {
    if (ui_checked(r->safe)) return SCAN_SAFE;
    if (ui_checked(r->advanced)) return SCAN_ADVANCED;
    return SCAN_MODERATE;
}

static int mode_place(const ModeRadios *r, int x, int y, int w) {
    ui_place(r->safe, x, y, w, S(22));
    ui_place(r->moderate, x, y + S(24), w, S(22));
    ui_place(r->advanced, x, y + S(48), w, S(22));
    return y + S(72);
}

static void buttons_place(HWND ok, HWND cancel, int w, int h) {
    int m = S(16), bh = S(28), bw_c = ui_button_width(T(S_CANCEL));
    WCHAR *t = ui_text(ok);
    int bw_o = MAX(ui_button_width(t), S(100));
    xfree(t);
    ui_place(cancel, w - m - bw_c, h - m - bh, bw_c, bh);
    ui_place(ok, w - m - bw_c - S(8) - bw_o, h - m - bh, bw_o, bh);
}

// ---------------------------------------------------------------- confirmar desinstalação

typedef struct {
    ModalState ms;
    Program *const *progs;
    size_t n;
    bool quick;
    UninstallOpts *opts;
    HWND title, info, mode_label, restore, quiet, ok, cancel;
    ModeRadios modes;
} ConfirmState;

static WCHAR *program_details(const Program *p) {
    StrBuf b = {0};
    if (p->publisher) sb_add(&b, p->publisher), sb_addc(&b, '\n');
    if (p->version) {
        WCHAR *t = wfmt(T(S_INFO_VERSION), p->version);
        sb_add(&b, t), sb_addc(&b, '\n');
        xfree(t);
    }
    if (p->install_location) {
        WCHAR *t = wfmt(T(S_INFO_FOLDER), p->install_location);
        sb_add(&b, t);
        xfree(t);
    }
    if (p->no_remove) sb_add(&b, u"\n"), sb_add(&b, T(S_INFO_NO_REMOVE));
    return sb_take(&b);
}

static void confirm_layout(ConfirmState *st, HWND h) {
    RECT r;
    GetClientRect(h, &r);
    int m = S(16), w = r.right - 2 * m, y = m;
    ui_place(st->title, m, y, w, S(26));
    y += S(32);
    int info_h = st->n > 1 ? S(110) : S(64);
    ui_place(st->info, m, y, w, info_h);
    y += info_h + S(10);
    ui_place(st->mode_label, m, y, w, S(20));
    y = mode_place(&st->modes, m + S(8), y + S(22), w - S(8)) + S(6);
    ui_place(st->restore, m, y, w, S(22));
    y += S(24);
    if (st->quiet) ui_place(st->quiet, m, y, w, S(22));
    buttons_place(st->ok, st->cancel, r.right, r.bottom);
}

static LRESULT CALLBACK confirm_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    ConfirmState *st = ui_state(h, m, lp);
    switch (m) {
    case WM_CREATE: {
        WCHAR *title = st->n == 1 ? wfmt(T(S_CONFIRM_ONE), ((Program *)st->progs[0])->name) : wfmt(T(S_CONFIRM_MANY), (unsigned)st->n);
        st->title = ui_label(h, title, ID_TITLE);
        xfree(title);
        SendMessageW(st->title, WM_SETFONT, (WPARAM)ui_font_title(), TRUE);
        if (st->n == 1) {
            WCHAR *d = program_details(st->progs[0]);
            st->info = ui_label(h, d, ID_INFO);
            xfree(d);
        } else {
            StrBuf b = {0};
            for (size_t i = 0; i < st->n; i++) {
                sb_add(&b, st->progs[i]->name);
                if (st->progs[i]->version) sb_add(&b, u"  "), sb_add(&b, st->progs[i]->version);
                sb_add(&b, u"\r\n");
            }
            st->info = ui_edit(h, sb_str(&b), ID_INFO, ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL);
            sb_free(&b);
        }
        st->mode_label = ui_label(h, T(S_MODE_LABEL), ID_MODE_LABEL);
        mode_create(h, &st->modes, (ScanMode)setting_get_int(u"scan_mode", SCAN_MODERATE));
        st->restore = ui_check(h, T(S_RESTORE_POINT), ID_RESTORE, setting_get_int(u"restore_point", 1) != 0);
        if (st->n > 1 || st->quick) st->quiet = ui_check(h, T(S_QUIET_WHEN_POSSIBLE), ID_QUIET, st->quick);
        st->ok = ui_child(h, u"BUTTON", T(S_UNINSTALL), WS_TABSTOP | BS_DEFPUSHBUTTON, 0, IDOK);
        st->cancel = ui_button(h, T(S_CANCEL), IDCANCEL);
        return 0;
    }
    case WM_SIZE:
        confirm_layout(st, h);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            st->opts->mode = mode_get(&st->modes);
            st->opts->restore_point = ui_checked(st->restore);
            st->opts->quiet = st->quiet && ui_checked(st->quiet);
            setting_set_int(u"scan_mode", (int)st->opts->mode);
            setting_set_int(u"restore_point", st->opts->restore_point);
            ui_end(&st->ms, IDOK);
        } else if (LOWORD(wp) == IDCANCEL) {
            ui_end(&st->ms, IDCANCEL);
        }
        return 0;
    case WM_CLOSE:
        ui_end(&st->ms, IDCANCEL);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

bool dlg_confirm_uninstall(HWND owner, Program *const *progs, size_t n, bool quick, UninstallOpts *opts) {
    static bool reg;
    if (!reg) reg = ui_register(u"ExtirpaConfirm", confirm_proc) != 0;
    ConfirmState st;
    mem_set(&st, 0, sizeof(st));
    st.progs = progs, st.n = n, st.quick = quick, st.opts = opts;
    HWND w = ui_window(u"ExtirpaConfirm", T(S_UNINSTALL), owner, &st, WS_CAPTION | WS_SYSMENU);
    int h = (n > 1 ? S(470) : S(420)) + ((n > 1 || quick) ? S(24) : 0);
    ui_center(w, owner, S(580), h);
    return ui_modal(w, owner, &st.ms) == IDOK;
}

// ---------------------------------------------------------------- desinstalação forçada

typedef struct {
    ModalState ms;
    WCHAR **name, **folder;
    ScanMode *mode;
    HWND info, name_label, name_edit, folder_label, folder_edit, browse, mode_label, ok, cancel;
    ModeRadios modes;
} ForcedState;

static void forced_layout(ForcedState *st, HWND h) {
    RECT r;
    GetClientRect(h, &r);
    int m = S(16), w = r.right - 2 * m, y = m;
    ui_place(st->info, m, y, w, S(40));
    y += S(46);
    ui_place(st->name_label, m, y, w, S(20));
    ui_place(st->name_edit, m, y + S(22), w, S(24));
    y += S(56);
    ui_place(st->folder_label, m, y, w, S(20));
    int bw = ui_button_width(T(S_BROWSE));
    ui_place(st->folder_edit, m, y + S(22), w - bw - S(8), S(24));
    ui_place(st->browse, m + w - bw, y + S(21), bw, S(26));
    y += S(58);
    ui_place(st->mode_label, m, y, w, S(20));
    mode_place(&st->modes, m + S(8), y + S(22), w - S(8));
    buttons_place(st->ok, st->cancel, r.right, r.bottom);
}

static LRESULT CALLBACK forced_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    ForcedState *st = ui_state(h, m, lp);
    switch (m) {
    case WM_CREATE:
        st->info = ui_label(h, T(S_FORCED_INFO), ID_INFO);
        st->name_label = ui_label(h, T(S_FORCED_NAME), ID_NAME_LABEL);
        st->name_edit = ui_edit(h, *st->name ? *st->name : u"", ID_NAME, 0);
        st->folder_label = ui_label(h, T(S_FORCED_FOLDER), ID_FOLDER_LABEL);
        st->folder_edit = ui_edit(h, *st->folder ? *st->folder : u"", ID_FOLDER, 0);
        st->browse = ui_button(h, T(S_BROWSE), ID_BROWSE);
        st->mode_label = ui_label(h, T(S_MODE_LABEL_FORCED), ID_MODE_LABEL);
        mode_create(h, &st->modes, (ScanMode)setting_get_int(u"scan_mode", SCAN_MODERATE));
        st->ok = ui_child(h, u"BUTTON", T(S_FIND_LEFTOVERS), WS_TABSTOP | BS_DEFPUSHBUTTON, 0, IDOK);
        st->cancel = ui_button(h, T(S_CANCEL), IDCANCEL);
        SetFocus(st->name_edit);
        return 0;
    case WM_SIZE:
        forced_layout(st, h);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_BROWSE) {
            WCHAR *f = dlg_pick_folder(h, T(S_FORCED_FOLDER));
            if (f) SetWindowTextW(st->folder_edit, f), xfree(f);
        } else if (LOWORD(wp) == IDOK) {
            WCHAR *n = ui_text(st->name_edit), *f = ui_text(st->folder_edit);
            WCHAR *tn = wtrim_dup(n), *tf = path_norm(f);
            xfree(n), xfree(f);
            if (tf[0] && !dir_exists(tf)) {
                msg(h, T(S_FORCED_BAD_FOLDER), MB_OK | MB_ICONWARNING);
                xfree(tn), xfree(tf);
                return 0;
            }
            if (wlen(tn) < 3 && !tf[0]) {
                msg(h, T(S_FORCED_NEED_NAME), MB_OK | MB_ICONWARNING);
                xfree(tn), xfree(tf);
                return 0;
            }
            xfree(*st->name), xfree(*st->folder);
            *st->name = tn;
            *st->folder = tf;
            *st->mode = mode_get(&st->modes);
            setting_set_int(u"scan_mode", (int)*st->mode);
            ui_end(&st->ms, IDOK);
        } else if (LOWORD(wp) == IDCANCEL) {
            ui_end(&st->ms, IDCANCEL);
        }
        return 0;
    case WM_CLOSE:
        ui_end(&st->ms, IDCANCEL);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

bool dlg_forced(HWND owner, WCHAR **name, WCHAR **folder, ScanMode *mode) {
    static bool reg;
    if (!reg) reg = ui_register(u"ExtirpaForced", forced_proc) != 0;
    ForcedState st;
    mem_set(&st, 0, sizeof(st));
    st.name = name, st.folder = folder, st.mode = mode;
    HWND w = ui_window(u"ExtirpaForced", T(S_FORCED_TITLE), owner, &st, WS_CAPTION | WS_SYSMENU);
    ui_center(w, owner, S(580), S(420));
    return ui_modal(w, owner, &st.ms) == IDOK;
}

// ---------------------------------------------------------------- caixa de texto

typedef struct {
    ModalState ms;
    const WCHAR *prompt, *initial;
    WCHAR *result;
    HWND label, edit, ok, cancel;
} InputState;

static LRESULT CALLBACK input_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    InputState *st = ui_state(h, m, lp);
    switch (m) {
    case WM_CREATE:
        st->label = ui_label(h, st->prompt, ID_PROMPT);
        st->edit = ui_edit(h, st->initial ? st->initial : u"", ID_EDIT, 0);
        st->ok = ui_child(h, u"BUTTON", T(S_OK), WS_TABSTOP | BS_DEFPUSHBUTTON, 0, IDOK);
        st->cancel = ui_button(h, T(S_CANCEL), IDCANCEL);
        SetFocus(st->edit);
        SendMessageW(st->edit, EM_SETSEL, 0, -1);
        return 0;
    case WM_SIZE: {
        RECT r;
        GetClientRect(h, &r);
        int m2 = S(16);
        ui_place(st->label, m2, m2, r.right - 2 * m2, S(36));
        ui_place(st->edit, m2, m2 + S(40), r.right - 2 * m2, S(24));
        buttons_place(st->ok, st->cancel, r.right, r.bottom);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            WCHAR *t = ui_text(st->edit);
            st->result = wtrim_dup(t);
            xfree(t);
            if (!st->result[0]) {
                xfree(st->result);
                st->result = NULL;
                return 0;
            }
            ui_end(&st->ms, IDOK);
        } else if (LOWORD(wp) == IDCANCEL) {
            ui_end(&st->ms, IDCANCEL);
        }
        return 0;
    case WM_CLOSE:
        ui_end(&st->ms, IDCANCEL);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

WCHAR *dlg_input(HWND owner, const WCHAR *title, const WCHAR *prompt, const WCHAR *initial) {
    static bool reg;
    if (!reg) reg = ui_register(u"ExtirpaInput", input_proc) != 0;
    InputState st;
    mem_set(&st, 0, sizeof(st));
    st.prompt = prompt, st.initial = initial;
    HWND w = ui_window(u"ExtirpaInput", title, owner, &st, WS_CAPTION | WS_SYSMENU);
    ui_center(w, owner, S(480), S(190));
    if (ui_modal(w, owner, &st.ms) != IDOK) {
        xfree(st.result);
        return NULL;
    }
    return st.result;
}

// ---------------------------------------------------------------- seletores do Windows

WCHAR *dlg_pick_folder(HWND owner, const WCHAR *title) {
    WCHAR name[MAX_PATH + 1];
    name[0] = 0;
    BROWSEINFOW bi;
    mem_set(&bi, 0, sizeof(bi));
    bi.hwndOwner = owner;
    bi.pszDisplayName = name;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return NULL;
    WCHAR path[MAX_PATH + 1];
    path[0] = 0;
    BOOL ok = SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    return ok && path[0] ? path_norm(path) : NULL;
}

WCHAR *dlg_pick_file(HWND owner, const WCHAR *title, const WCHAR *filter) {
    WCHAR *buf = xalloc(32768 * sizeof(WCHAR));
    OPENFILENAMEW of;
    mem_set(&of, 0, sizeof(of));
    of.lStructSize = sizeof(of);
    of.hwndOwner = owner;
    of.lpstrFilter = filter;
    of.lpstrFile = buf;
    of.nMaxFile = 32767;
    of.lpstrTitle = title;
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_DONTADDTORECENT;
    if (!GetOpenFileNameW(&of)) {
        xfree(buf);
        return NULL;
    }
    WCHAR *r = wdup(buf);
    xfree(buf);
    return r;
}
