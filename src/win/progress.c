// SPDX-License-Identifier: MIT
#include "progress.h"

#include "i18n.h"
#include "platform.h"
#include "ui.h"

enum { ID_STEP = 100, ID_DETAIL, ID_BAR, ID_EXTRA };

struct Progress {
    ModalState ms;
    HWND wnd, step, detail, bar, extra, cancel;
    WorkFn fn;
    void *arg;
    HANDLE button_event, cancel_event;
    Ctl ctl;
    bool allow_cancel;
};

static const WCHAR CLS[] = u"ExtirpaProgress";

static void layout(Progress *pg) {
    RECT r;
    GetClientRect(pg->wnd, &r);
    int w = r.right, h = r.bottom, m = S(16);
    ui_place(pg->step, m, m, w - 2 * m, S(22));
    ui_place(pg->detail, m, m + S(28), w - 2 * m, S(36));
    ui_place(pg->bar, m, m + S(70), w - 2 * m, S(16));
    int bw = ui_button_width(T(S_CANCEL)), bh = S(28), y = h - m - bh;
    ui_place(pg->cancel, w - m - bw, y, bw, bh);
    WCHAR *t = ui_text(pg->extra);
    int ew = ui_button_width(t);
    xfree(t);
    ui_place(pg->extra, w - m - bw - S(8) - ew, y, ew, bh);
}

static void do_cancel(Progress *pg) {
    if (!pg->allow_cancel || pg->ctl.cancel) return;
    pg->ctl.cancel = 1;
    SetEvent(pg->cancel_event);
    EnableWindow(pg->cancel, FALSE);
    SetWindowTextW(pg->cancel, T(S_CANCELLING));
}

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    Progress *pg = ui_state(h, m, lp);
    switch (m) {
    case WM_CREATE:
        pg->wnd = h;
        pg->step = ui_label(h, u"", ID_STEP);
        SendMessageW(pg->step, WM_SETFONT, (WPARAM)ui_font_bold(), TRUE);
        pg->detail = ui_child(h, u"STATIC", u"", SS_LEFT | SS_NOPREFIX | SS_PATHELLIPSIS, 0, ID_DETAIL);
        pg->bar = ui_child(h, u"msctls_progress32", u"", PBS_MARQUEE, 0, ID_BAR);
        SendMessageW(pg->bar, PBM_SETMARQUEE, TRUE, 30);
        pg->extra = ui_button(h, u"", ID_EXTRA);
        ShowWindow(pg->extra, SW_HIDE);
        pg->cancel = ui_button(h, T(S_CANCEL), IDCANCEL);
        EnableWindow(pg->cancel, pg->allow_cancel);
        return 0;
    case WM_SIZE:
        layout(pg);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == IDCANCEL) do_cancel(pg);
        if (LOWORD(wp) == ID_EXTRA) {
            SetEvent(pg->button_event);
            EnableWindow(pg->extra, FALSE);
        }
        return 0;
    case WM_CLOSE:
        do_cancel(pg);
        return 0;
    case WM_APP_STEP:
        SetWindowTextW(pg->step, (WCHAR *)lp);
        SetWindowTextW(pg->detail, u"");
        xfree((void *)lp);
        return 0;
    case WM_APP_DETAIL:
        SetWindowTextW(pg->detail, (WCHAR *)lp);
        xfree((void *)lp);
        return 0;
    case WM_APP_BUTTON:
        if (wp) {
            SetWindowTextW(pg->extra, (WCHAR *)lp);
            EnableWindow(pg->extra, TRUE);
            ShowWindow(pg->extra, SW_SHOW);
            layout(pg);
        } else {
            ShowWindow(pg->extra, SW_HIDE);
        }
        xfree((void *)lp);
        return 0;
    case WM_APP_ASK:
        return MessageBoxW(h, (const WCHAR *)lp, u"EXTIRPA", (UINT)wp);
    case WM_APP_DONE:
        ui_end(&pg->ms, IDOK);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static void ctl_cb(void *ctx, const WCHAR *text) { pg_detail(ctx, text); }

static DWORD WINAPI worker(LPVOID arg) {
    Progress *pg = arg;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    pg->fn(pg, pg->arg);
    CoUninitialize();
    PostMessageW(pg->wnd, WM_APP_DONE, 0, 0);
    return 0;
}

void progress_run(HWND owner, const WCHAR *title, WorkFn fn, void *arg) {
    static bool registered;
    if (!registered) registered = ui_register(CLS, proc) != 0;
    Progress *pg = xalloc(sizeof(Progress));
    pg->fn = fn;
    pg->arg = arg;
    pg->allow_cancel = true;
    pg->button_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    pg->cancel_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    pg->ctl.progress = ctl_cb;
    pg->ctl.ctx = pg;
    HWND w = ui_window(CLS, title, owner, pg, WS_CAPTION | WS_SYSMENU);
    ui_center(w, owner, S(560), S(230));
    HANDLE th = CreateThread(NULL, 0, worker, pg, 0, NULL);
    if (!th) {
        DestroyWindow(w);
        msg(owner, T(S_ERR_THREAD), MB_OK | MB_ICONERROR);
    } else {
        ui_modal(w, owner, &pg->ms);
        WaitForSingleObject(th, INFINITE);
        CloseHandle(th);
    }
    CloseHandle(pg->button_event);
    CloseHandle(pg->cancel_event);
    xfree(pg);
}

void pg_step(Progress *pg, const WCHAR *text) { PostMessageW(pg->wnd, WM_APP_STEP, 0, (LPARAM)wdup(text)); }
void pg_detail(Progress *pg, const WCHAR *text) { PostMessageW(pg->wnd, WM_APP_DETAIL, 0, (LPARAM)wdup(text)); }
void pg_button(Progress *pg, const WCHAR *label) { PostMessageW(pg->wnd, WM_APP_BUTTON, label != NULL, (LPARAM)wdup(label)); }
HANDLE pg_button_event(Progress *pg) { return pg->button_event; }
HANDLE pg_cancel_event(Progress *pg) { return pg->cancel_event; }
void pg_allow_cancel(Progress *pg, bool allow) {
    pg->allow_cancel = allow;
    if (pg->wnd && !pg->ctl.cancel) EnableWindow(pg->cancel, allow);
}
bool pg_cancelled(Progress *pg) { return pg->ctl.cancel != 0; }
Ctl *pg_ctl(Progress *pg) { return &pg->ctl; }
int pg_ask(Progress *pg, const WCHAR *text, UINT flags) { return (int)SendMessageW(pg->wnd, WM_APP_ASK, flags, (LPARAM)text); }
HWND pg_window(Progress *pg) { return pg->wnd; }
