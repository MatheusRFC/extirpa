// SPDX-License-Identifier: MIT
#include "ui.h"

HINSTANCE g_inst;
HICON g_icon_big, g_icon_small;
UINT g_dpi = 96;
static HFONT g_font, g_bold, g_title;

int S(int v) { return (int)(((long long)v * g_dpi + 48) / 96); }

static HFONT make_font(int points, bool bold) {
    LOGFONTW lf;
    mem_set(&lf, 0, sizeof(lf));
    lf.lfHeight = -(LONG)(((long long)points * g_dpi + 36) / 72);
    lf.lfWeight = bold ? FW_BOLD : 400;
    lf.lfCharSet = 1;  // DEFAULT_CHARSET
    lf.lfQuality = 5;  // CLEARTYPE_QUALITY
    static const WCHAR face[] = u"Segoe UI";
    mem_copy(lf.lfFaceName, face, sizeof(face));
    return CreateFontIndirectW(&lf);
}

void ui_set_dpi(UINT dpi) {
    // As fontes antigas não são apagadas: controles de outras janelas ainda podem usá-las.
    // Mudança de DPI é rara (trocar a janela de monitor), então o custo é desprezível.
    g_dpi = dpi ? dpi : 96;
    g_font = make_font(9, false);
    g_bold = make_font(9, true);
    g_title = make_font(12, true);
}

void ui_init(void) {
    g_inst = GetModuleHandleW(NULL);
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    ui_set_dpi(96);
    g_icon_big = LoadImageW(g_inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    g_icon_small = LoadImageW(g_inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    if (!g_icon_big) g_icon_big = LoadIconW(NULL, IDI_APPLICATION);
    if (!g_icon_small) g_icon_small = g_icon_big;
}

HFONT ui_font(void) { return g_font; }
HFONT ui_font_bold(void) { return g_bold; }
HFONT ui_font_title(void) { return g_title; }

void *ui_state(HWND h, UINT m, LPARAM lp) {
    if (m == WM_NCCREATE) {
        void *s = ((CREATESTRUCTW *)lp)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)s);
        return s;
    }
    return (void *)GetWindowLongPtrW(h, GWLP_USERDATA);
}

ATOM ui_register(const WCHAR *cls, WNDPROC proc) {
    WNDCLASSEXW wc;
    mem_set(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = proc;
    wc.hInstance = g_inst;
    wc.hIcon = g_icon_big;
    wc.hIconSm = g_icon_small;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpszClassName = cls;
    return RegisterClassExW(&wc);
}

HWND ui_child(HWND parent, const WCHAR *cls, const WCHAR *text, DWORD style, DWORD exstyle, int id) {
    HWND h = CreateWindowExW(exstyle, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, g_inst, NULL);
    if (h) SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
    return h;
}

HWND ui_button(HWND parent, const WCHAR *text, int id) { return ui_child(parent, u"BUTTON", text, WS_TABSTOP | BS_PUSHBUTTON, 0, id); }

HWND ui_check(HWND parent, const WCHAR *text, int id, bool checked) {
    HWND h = ui_child(parent, u"BUTTON", text, WS_TABSTOP | BS_AUTOCHECKBOX, 0, id);
    ui_set_checked(h, checked);
    return h;
}

HWND ui_radio(HWND parent, const WCHAR *text, int id, bool first, bool checked) {
    HWND h = ui_child(parent, u"BUTTON", text, (first ? WS_GROUP | WS_TABSTOP : 0) | BS_AUTORADIOBUTTON, 0, id);
    ui_set_checked(h, checked);
    return h;
}

HWND ui_label(HWND parent, const WCHAR *text, int id) { return ui_child(parent, u"STATIC", text, SS_LEFT | SS_NOPREFIX, 0, id); }

HWND ui_edit(HWND parent, const WCHAR *text, int id, DWORD extra_style) {
    return ui_child(parent, u"EDIT", text, WS_TABSTOP | ES_AUTOHSCROLL | extra_style, WS_EX_CLIENTEDGE, id);
}

bool ui_checked(HWND h) { return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED; }
void ui_set_checked(HWND h, bool on) { SendMessageW(h, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0); }

WCHAR *ui_text(HWND h) {
    int n = GetWindowTextLengthW(h);
    WCHAR *s = xalloc(((size_t)(n > 0 ? n : 0) + 2) * sizeof(WCHAR));
    if (n > 0) GetWindowTextW(h, s, n + 1);
    return s;
}

void ui_place(HWND h, int x, int y, int w, int hh) {
    if (h) MoveWindow(h, x, y, w > 0 ? w : 0, hh > 0 ? hh : 0, TRUE);
}

int ui_button_width(const WCHAR *text) { return S(28) + (int)wlen(text) * S(7); }

// ---------------------------------------------------------------- ListView

HWND lv_create(HWND parent, int id, DWORD style, DWORD exstyle) {
    HWND lv = ui_child(parent, u"SysListView32", u"", WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | style, WS_EX_CLIENTEDGE, id);
    DWORD ex = LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP | LVS_EX_INFOTIP | exstyle;
    SendMessageW(lv, LVM_SETEXTENDEDLISTVIEWSTYLE, ex, ex);
    SetWindowTheme(lv, u"Explorer", NULL);
    return lv;
}

void lv_column(HWND lv, int i, const WCHAR *text, int width, bool right) {
    LVCOLUMNW c;
    mem_set(&c, 0, sizeof(c));
    c.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
    c.fmt = right ? LVCFMT_RIGHT : LVCFMT_LEFT;
    c.cx = width;
    c.pszText = (LPWSTR)text;
    c.iSubItem = i;
    SendMessageW(lv, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&c);
}

int lv_insert(HWND lv, int i, const WCHAR *text, LPARAM param) {
    LVITEMW it;
    mem_set(&it, 0, sizeof(it));
    it.mask = LVIF_TEXT | LVIF_PARAM;
    it.iItem = i;
    it.pszText = (LPWSTR)(text ? text : u"");
    it.lParam = param;
    return (int)SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&it);
}

void lv_text(HWND lv, int i, int sub, const WCHAR *text) {
    LVITEMW it;
    mem_set(&it, 0, sizeof(it));
    it.iSubItem = sub;
    it.pszText = (LPWSTR)(text ? text : u"");
    SendMessageW(lv, LVM_SETITEMTEXTW, (WPARAM)i, (LPARAM)&it);
}

LPARAM lv_param(HWND lv, int i) {
    LVITEMW it;
    mem_set(&it, 0, sizeof(it));
    it.mask = LVIF_PARAM;
    it.iItem = i;
    if (!SendMessageW(lv, LVM_GETITEMW, 0, (LPARAM)&it)) return 0;
    return it.lParam;
}

int lv_count(HWND lv) { return (int)SendMessageW(lv, LVM_GETITEMCOUNT, 0, 0); }

int lv_selected(HWND lv, int after) { return (int)SendMessageW(lv, LVM_GETNEXTITEM, (WPARAM)after, LVNI_SELECTED); }

int lv_selected_count(HWND lv) { return (int)SendMessageW(lv, LVM_GETSELECTEDCOUNT, 0, 0); }

void lv_select(HWND lv, int i, bool only) {
    LVITEMW it;
    mem_set(&it, 0, sizeof(it));
    if (only) {
        it.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
        it.state = 0;
        SendMessageW(lv, LVM_SETITEMSTATE, (WPARAM)-1, (LPARAM)&it);
    }
    if (i < 0) return;
    it.stateMask = LVIS_SELECTED | LVIS_FOCUSED;
    it.state = LVIS_SELECTED | LVIS_FOCUSED;
    SendMessageW(lv, LVM_SETITEMSTATE, (WPARAM)i, (LPARAM)&it);
    SendMessageW(lv, LVM_ENSUREVISIBLE, (WPARAM)i, FALSE);
}

bool lv_checked(HWND lv, int i) {
    UINT st = (UINT)SendMessageW(lv, LVM_GETITEMSTATE, (WPARAM)i, LVIS_STATEIMAGEMASK);
    return ((st & LVIS_STATEIMAGEMASK) >> 12) == 2;
}

void lv_set_checked(HWND lv, int i, bool on) {
    LVITEMW it;
    mem_set(&it, 0, sizeof(it));
    it.stateMask = LVIS_STATEIMAGEMASK;
    it.state = (UINT)(on ? 2 : 1) << 12;
    SendMessageW(lv, LVM_SETITEMSTATE, (WPARAM)i, (LPARAM)&it);
}

void lv_sort_arrow(HWND lv, int col, int dir) {
    HWND hdr = (HWND)SendMessageW(lv, LVM_GETHEADER, 0, 0);
    if (!hdr) return;
    for (int i = 0; i < 32; i++) {
        HDITEMW h;
        mem_set(&h, 0, sizeof(h));
        h.mask = HDI_FORMAT;
        if (!SendMessageW(hdr, HDM_GETITEMW, (WPARAM)i, (LPARAM)&h)) break;
        h.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (i == col && dir > 0) h.fmt |= HDF_SORTUP;
        if (i == col && dir < 0) h.fmt |= HDF_SORTDOWN;
        SendMessageW(hdr, HDM_SETITEMW, (WPARAM)i, (LPARAM)&h);
    }
}

void lv_clear(HWND lv) { SendMessageW(lv, LVM_DELETEALLITEMS, 0, 0); }

// ---------------------------------------------------------------- janelas

int msg(HWND owner, const WCHAR *text, UINT flags) { return MessageBoxW(owner, text, u"EXTIRPA", flags); }

void ui_end(ModalState *ms, int result) {
    ms->result = result;
    ms->done = true;
}

#ifdef EXTIRPA_SELFTEST
void (*g_modal_hook)(HWND w, ModalState *ms);
#define SELFTEST_TIMER 0x7E57
#endif

int ui_modal(HWND w, HWND owner, ModalState *ms) {
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(w, SW_SHOW);
    UpdateWindow(w);
    SetForegroundWindow(w);
#ifdef EXTIRPA_SELFTEST
    if (g_modal_hook) SetTimer(w, SELFTEST_TIMER, 1200, NULL);
#endif
    MSG m;
    bool quit = false;
    while (!ms->done) {
        int r = GetMessageW(&m, NULL, 0, 0);
        if (r <= 0) {
            quit = true;
            break;
        }
#ifdef EXTIRPA_SELFTEST
        if (g_modal_hook && m.message == WM_TIMER && m.hwnd == w && m.wParam == SELFTEST_TIMER) {
            KillTimer(w, SELFTEST_TIMER);
            g_modal_hook(w, ms);
            continue;
        }
#endif
        if (!IsDialogMessageW(w, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    if (owner) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    DestroyWindow(w);
    if (quit) PostQuitMessage(0);
    return quit ? IDCANCEL : ms->result;
}

void ui_center(HWND w, HWND owner, int cx, int cy) {
    RECT r;
    MONITORINFO mi;
    mem_set(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromWindow(owner ? owner : w, MONITOR_DEFAULTTONEAREST), &mi);
    if (owner && IsWindowVisible(owner) && !IsIconic(owner)) GetWindowRect(owner, &r);
    else r = mi.rcWork;
    int x = r.left + (r.right - r.left - cx) / 2, y = r.top + (r.bottom - r.top - cy) / 2;
    // dentro da área de trabalho do monitor
    if (x + cx > mi.rcWork.right) x = mi.rcWork.right - cx;
    if (y + cy > mi.rcWork.bottom) y = mi.rcWork.bottom - cy;
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y < mi.rcWork.top) y = mi.rcWork.top;
    SetWindowPos(w, NULL, x, y, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE);
}

HWND ui_window(const WCHAR *cls, const WCHAR *title, HWND owner, void *state, DWORD style) {
    // a janela nova nasce no monitor da dona: fontes no DPI dela antes de criar os controles
    if (owner) {
        UINT dpi = GetDpiForWindow(owner);
        if (dpi && dpi != g_dpi) ui_set_dpi(dpi);
    }
    return CreateWindowExW(WS_EX_CONTROLPARENT, cls, title, style | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 400, 300, owner, NULL, g_inst, state);
}
