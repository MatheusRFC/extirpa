// SPDX-License-Identifier: MIT
#include "hunter.h"

#include "../core/pathx.h"
#include "../core/protect.h"
#include "i18n.h"
#include "ops.h"
#include "platform.h"
#include "programs.h"
#include "startup.h"
#include "ui.h"

#define BRAND RGB(0xD6, 0x45, 0x45)

enum { M_UNINSTALL = 1, M_FORCED, M_OPEN, M_KILL, M_STARTUP_OFF, M_EXIT };

typedef struct {
    ModalState ms;
    HWND wnd, main;
    bool dragging;
    bool changed;
    PtrVec programs;
} Hunter;

static void paint(HWND h) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT r;
    GetClientRect(h, &r);
    HBRUSH bg = CreateSolidBrush(BRAND);
    FillRect(dc, &r, bg);
    DeleteObject(bg);
    HPEN pen = CreatePen(PS_SOLID, MAX(2, S(3)), RGB(255, 255, 255));
    HGDIOBJ old_pen = SelectObject(dc, pen), old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    int cx = r.right / 2, cy = r.bottom / 2, rad = r.right * 30 / 100, gap = r.right * 8 / 100;
    Ellipse(dc, cx - rad, cy - rad, cx + rad, cy + rad);
    int ext = r.right * 44 / 100;
    MoveToEx(dc, cx - ext, cy, NULL), LineTo(dc, cx - gap, cy);
    MoveToEx(dc, cx + gap, cy, NULL), LineTo(dc, cx + ext, cy);
    MoveToEx(dc, cx, cy - ext, NULL), LineTo(dc, cx, cy - gap);
    MoveToEx(dc, cx, cy + gap, NULL), LineTo(dc, cx, cy + ext);
    SelectObject(dc, old_pen), SelectObject(dc, old_brush);
    DeleteObject(pen);
    EndPaint(h, &ps);
}

static void reload(Hunter *hu) {
    programs_free(&hu->programs);
    hu->programs = programs_load();
}

static StartupEntry *startup_for(PtrVec *entries, const WCHAR *exe) {
    for (size_t i = 0; i < entries->n; i++) {
        StartupEntry *e = entries->items[i];
        WCHAR *x = startup_exe(e);
        bool same = x && path_eq(x, exe);
        xfree(x);
        if (same && e->can_toggle && e->enabled) return e;
    }
    return NULL;
}

static void on_target(Hunter *hu, POINT pt) {
    HWND target = WindowFromPoint(pt);
    HWND root = target ? GetAncestor(target, GA_ROOT) : NULL;
    if (!root || root == hu->wnd) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(root, &pid);
    if (!pid || pid == GetCurrentProcessId()) return;
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pid);
    if (!proc) proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) {
        msg(hu->wnd, T(S_HUNTER_NO_ACCESS), MB_OK | MB_ICONWARNING);
        return;
    }
    WCHAR buf[1024];
    DWORD n = COUNT(buf);
    bool got = QueryFullProcessImageNameW(proc, 0, buf, &n) != 0;
    if (!got) {
        CloseHandle(proc);
        msg(hu->wnd, T(S_HUNTER_NO_ACCESS), MB_OK | MB_ICONWARNING);
        return;
    }
    buf[n] = 0;
    WCHAR *exe = path_norm(buf), *dir = path_parent(exe);
    if (root == GetShellWindow() || (g_paths.windows && path_is_under(exe, g_paths.windows))) {
        msg(hu->wnd, T(S_HUNTER_SYSTEM), MB_OK | MB_ICONINFORMATION);
        CloseHandle(proc);
        xfree(exe), xfree(dir);
        return;
    }
    Program *p = programs_find_by_exe(&hu->programs, exe);
    PtrVec entries = startup_list();
    StartupEntry *se = startup_for(&entries, exe);

    HMENU m = CreatePopupMenu();
    WCHAR *head = p ? wdup(p->name) : wdup(path_leaf(exe));
    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, head);
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    if (p) {
        WCHAR *t = wfmt(T(S_HUNTER_UNINSTALL), p->name);
        AppendMenuW(m, MF_STRING | MF_DEFAULT, M_UNINSTALL, t);
        xfree(t);
    } else {
        AppendMenuW(m, MF_STRING | MF_GRAYED, 0, T(S_HUNTER_NOT_FOUND));
    }
    AppendMenuW(m, MF_STRING, M_FORCED, T(S_HUNTER_FORCED));
    AppendMenuW(m, MF_STRING, M_OPEN, T(S_HUNTER_OPEN_FOLDER));
    AppendMenuW(m, MF_STRING, M_KILL, T(S_HUNTER_KILL));
    if (se) AppendMenuW(m, MF_STRING, M_STARTUP_OFF, T(S_HUNTER_STARTUP_OFF));
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, M_EXIT, T(S_HUNTER_EXIT));
    SetForegroundWindow(hu->wnd);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hu->wnd, NULL);
    DestroyMenu(m);
    xfree(head);

    switch (cmd) {
    case M_UNINSTALL:
        if (p) {
            Program *list[1] = {p};
            hu->changed = op_uninstall(hu->wnd, list, 1, false) || hu->changed;
            reload(hu);
        }
        break;
    case M_FORCED: {
        const WCHAR *folder = (p && p->install_location) ? p->install_location : (dir && !protect_dir_blocked(dir) ? dir : NULL);
        WCHAR *name = p ? wdup(p->name) : path_strip_ext(path_leaf(exe));
        hu->changed = op_forced(hu->wnd, name, folder) || hu->changed;
        xfree(name);
        reload(hu);
        break;
    }
    case M_OPEN: shell_select(exe); break;
    case M_KILL: {
        WCHAR *q = wfmt(T(S_HUNTER_KILL_CONFIRM), path_leaf(exe));
        if (msg(hu->wnd, q, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES && !TerminateProcess(proc, 1)) {
            WCHAR *e = win_error_text(GetLastError());
            msg(hu->wnd, e, MB_OK | MB_ICONERROR);
            xfree(e);
        }
        xfree(q);
        break;
    }
    case M_STARTUP_OFF:
        if (se && startup_set_enabled(se, false) == ERROR_SUCCESS) msg(hu->wnd, T(S_HUNTER_STARTUP_DONE), MB_OK | MB_ICONINFORMATION);
        break;
    case M_EXIT: ui_end(&hu->ms, IDOK); break;
    }
    startup_list_free(&entries);
    CloseHandle(proc);
    xfree(exe), xfree(dir);
}

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    Hunter *hu = ui_state(h, m, lp);
    switch (m) {
    case WM_CREATE:
        hu->wnd = h;
        SetLayeredWindowAttributes(h, 0, 235, LWA_ALPHA);
        return 0;
    case WM_PAINT:
        paint(h);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(NULL, IDC_CROSS));
        return TRUE;
    case WM_LBUTTONDOWN:
        hu->dragging = true;
        SetCapture(h);
        return 0;
    case WM_LBUTTONUP:
        if (hu->dragging) {
            hu->dragging = false;
            ReleaseCapture();
            POINT pt;
            GetCursorPos(&pt);
            on_target(hu, pt);
        }
        return 0;
    case WM_CAPTURECHANGED:
        hu->dragging = false;
        return 0;
    case WM_RBUTTONUP: {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, T(S_HUNTER_HINT));
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(menu, MF_STRING, M_EXIT, T(S_HUNTER_EXIT));
        POINT pt;
        GetCursorPos(&pt);
        int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, NULL);
        DestroyMenu(menu);
        if (cmd == M_EXIT) ui_end(&hu->ms, IDOK);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) ui_end(&hu->ms, IDOK);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == IDCANCEL) ui_end(&hu->ms, IDOK);
        return 0;
    case WM_CLOSE:
        ui_end(&hu->ms, IDOK);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

bool hunter_run(HWND main) {
    static bool reg;
    if (!reg) reg = ui_register(u"ExtirpaHunter", proc) != 0;
    Hunter hu;
    mem_set(&hu, 0, sizeof(hu));
    hu.main = main;
    hu.programs = programs_load();
    HWND w = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, u"ExtirpaHunter", u"EXTIRPA", WS_POPUP, 0, 0, S(64), S(64), NULL, NULL, g_inst, &hu);
    if (!w) {
        programs_free(&hu.programs);
        return false;
    }
    MONITORINFO mi;
    mem_set(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromWindow(main, MONITOR_DEFAULTTONEAREST), &mi);
    SetWindowPos(w, HWND_TOPMOST, mi.rcWork.right - S(64) - S(24), mi.rcWork.top + S(24), S(64), S(64), 0);
    ShowWindow(main, SW_HIDE);
    ui_modal(w, NULL, &hu.ms);
    ShowWindow(main, SW_SHOW);
    SetForegroundWindow(main);
    programs_free(&hu.programs);
    return hu.changed;
}
