// SPDX-License-Identifier: MIT
#include "mainwin.h"

#include "../core/fmt.h"
#include "../core/pathx.h"
#include "../core/protect.h"
#include "appx.h"
#include "backups.h"
#include "fsx.h"
#include "hunter.h"
#include "i18n.h"
#include "monitor.h"
#include "ops.h"
#include "platform.h"
#include "programs.h"
#include "progress.h"
#include "startup.h"
#include "ui.h"

typedef enum { TAB_PROGRAMS, TAB_APPS, TAB_TRACES, TAB_STARTUP, TAB_BACKUPS, TAB_COUNT } Tab;

enum {
    ID_TABS = 10,
    ID_SEARCH,
    ID_STATUS,
    ID_LV = 20,
    // Programas
    B_UNINSTALL = 100,
    B_QUICK,
    B_FORCED,
    B_HUNTER,
    B_OPEN_FOLDER,
    B_REFRESH_PROGS,
    // Apps do Windows
    B_APP_REMOVE = 120,
    B_APP_REMOVE_ALL,
    B_APP_OPEN,
    B_APP_REFRESH,
    // Rastreados
    B_TRACE_NEW = 140,
    B_TRACE_UNINSTALL,
    B_TRACE_VIEW,
    B_TRACE_DELETE,
    B_TRACE_REFRESH,
    // Inicialização
    B_ST_ENABLE = 160,
    B_ST_DISABLE,
    B_ST_DELETE,
    B_ST_OPEN,
    B_ST_REFRESH,
    // Backups
    B_BK_RESTORE = 180,
    B_BK_OPEN,
    B_BK_DELETE,
    B_BK_SYSRESTORE,
    B_BK_RECYCLE,
    B_BK_LOG,
};

typedef struct {
    int id;
    StrId text;
} ButtonDef;

static const ButtonDef BUTTONS[TAB_COUNT][7] = {
    {{B_UNINSTALL, S_BTN_UNINSTALL}, {B_QUICK, S_BTN_QUICK}, {B_FORCED, S_BTN_FORCED}, {B_HUNTER, S_BTN_HUNTER}, {B_OPEN_FOLDER, S_BTN_OPEN_FOLDER},
     {B_REFRESH_PROGS, S_BTN_REFRESH}},
    {{B_APP_REMOVE, S_BTN_APP_REMOVE}, {B_APP_REMOVE_ALL, S_BTN_APP_REMOVE_ALL}, {B_APP_OPEN, S_BTN_OPEN_FOLDER}, {B_APP_REFRESH, S_BTN_REFRESH}},
    {{B_TRACE_NEW, S_BTN_TRACE_NEW}, {B_TRACE_UNINSTALL, S_BTN_TRACE_UNINSTALL}, {B_TRACE_VIEW, S_BTN_TRACE_VIEW}, {B_TRACE_DELETE, S_BTN_TRACE_DELETE},
     {B_TRACE_REFRESH, S_BTN_REFRESH}},
    {{B_ST_ENABLE, S_BTN_ENABLE}, {B_ST_DISABLE, S_BTN_DISABLE}, {B_ST_DELETE, S_BTN_DELETE}, {B_ST_OPEN, S_BTN_OPEN_LOCATION}, {B_ST_REFRESH, S_BTN_REFRESH}},
    {{B_BK_RESTORE, S_BTN_RESTORE}, {B_BK_OPEN, S_BTN_OPEN_BACKUPS}, {B_BK_DELETE, S_BTN_DELETE}, {B_BK_SYSRESTORE, S_BTN_SYSRESTORE},
     {B_BK_RECYCLE, S_BTN_RECYCLE}, {B_BK_LOG, S_BTN_LOG}},
};

static struct {
    HWND wnd, tabs, search, status, lv[TAB_COUNT];
    HWND btn[TAB_COUNT][7];
    Tab cur;
    // programas (lista virtual: o ListView só pede o texto das linhas visíveis)
    PtrVec programs;
    int *view;
    size_t nview;
    int sort_col, sort_dir;
    HIMAGELIST icons;
    // aplicativos do Windows (carregados numa thread na primeira vez)
    PtrVec apps;
    bool apps_loaded, apps_loading;
    // rastreamentos, inicialização, backups
    PtrVec traces, startup, backups;
} M;

typedef struct {
    PtrVec list;
    WCHAR *error;
} AppsResult;

// ---------------------------------------------------------------- utilidades

static WCHAR *search_text(void) {
    WCHAR *t = ui_text(M.search), *r = wtrim_dup(t);
    xfree(t);
    return r;
}

static bool matches(const WCHAR *filter, const WCHAR *a, const WCHAR *b) {
    if (!filter[0]) return true;
    return (a && wifind(a, filter)) || (b && wifind(b, filter));
}

static void status(const WCHAR *text) { SendMessageW(M.status, SB_SETTEXTW, 0, (LPARAM)text); }

static void put_text(LVITEMW *it, const WCHAR *s) {
    if (!(it->mask & LVIF_TEXT) || !it->pszText || it->cchTextMax <= 0) return;
    size_t n = MIN(wlen(s), (size_t)it->cchTextMax - 1);
    if (n) mem_copy(it->pszText, s, n * sizeof(WCHAR));
    it->pszText[n] = 0;
}

static void open_with_notepad(const WCHAR *path) {
    WCHAR *np = g_paths.windows ? path_join(g_paths.windows, u"notepad.exe") : wdup(u"notepad.exe");
    WCHAR *arg = wfmt(u"\"%s\"", path);
    ShellExecuteW(M.wnd, u"open", np, arg, NULL, SW_SHOWNORMAL);
    xfree(np), xfree(arg);
}

// ---------------------------------------------------------------- programas

static int version_cmp(const WCHAR *a, const WCHAR *b) {
    a = a ? a : u"";
    b = b ? b : u"";
    while (*a || *b) {
        if (w_is_digit(*a) && w_is_digit(*b)) {
            uint64_t x = 0, y = 0;
            while (w_is_digit(*a)) x = x * 10 + (uint64_t)(*a++ - '0');
            while (w_is_digit(*b)) y = y * 10 + (uint64_t)(*b++ - '0');
            if (x != y) return x < y ? -1 : 1;
        } else {
            WCHAR x = wfold(*a), y = wfold(*b);
            if (x != y) return x < y ? -1 : 1;
            if (*a) a++;
            if (*b) b++;
        }
    }
    return 0;
}

static int cmp_programs(const void *pa, const void *pb, void *ctx) {
    (void)ctx;
    const Program *a = pa, *b = pb;
    int r = 0;
    switch (M.sort_col) {
    case 1: r = version_cmp(a->version, b->version); break;
    case 2: r = wicmp(a->publisher ? a->publisher : u"", b->publisher ? b->publisher : u""); break;
    case 3: r = a->size < b->size ? -1 : a->size > b->size ? 1 : 0; break;
    case 4: {
        int x = a->year * 10000 + a->month * 100 + a->day, y = b->year * 10000 + b->month * 100 + b->day;
        r = x < y ? -1 : x > y ? 1 : 0;
        break;
    }
    case 5: r = (int)a->scope - (int)b->scope; break;
    }
    if (!r) r = wicmp(a->name, b->name);
    return r * M.sort_dir;
}

static void programs_refilter(void) {
    PtrVec sorted = {0};
    for (size_t i = 0; i < M.programs.n; i++) pv_push(&sorted, M.programs.items[i]);
    pv_sort(&sorted, cmp_programs, NULL);
    WCHAR *f = search_text();
    xfree(M.view);
    M.view = xalloc((sorted.n + 1) * sizeof(int));
    M.nview = 0;
    for (size_t i = 0; i < sorted.n; i++) {
        Program *p = sorted.items[i];
        if (!matches(f, p->name, p->publisher)) continue;
        for (size_t k = 0; k < M.programs.n; k++)
            if (M.programs.items[k] == p) {
                M.view[M.nview++] = (int)k;
                break;
            }
    }
    pv_free(&sorted);
    SendMessageW(M.lv[TAB_PROGRAMS], LVM_SETITEMCOUNT, M.nview, 0);
    InvalidateRect(M.lv[TAB_PROGRAMS], NULL, TRUE);
    lv_sort_arrow(M.lv[TAB_PROGRAMS], M.sort_col, M.sort_dir);
    if (M.cur == TAB_PROGRAMS) {
        WCHAR *t = f[0] ? wfmt(T(S_STATUS_PROGRAMS_FILTERED), (unsigned)M.nview, (unsigned)M.programs.n) : wfmt(T(S_STATUS_PROGRAMS), (unsigned)M.programs.n);
        status(t);
        xfree(t);
    }
    xfree(f);
}

static void icons_reset(void) {
    HIMAGELIST old = M.icons;
    int sz = S(16);
    M.icons = ImageList_Create(sz, sz, ILC_COLOR32 | ILC_MASK, 64, 64);
    ImageList_ReplaceIcon(M.icons, -1, LoadIconW(NULL, IDI_APPLICATION));  // 0: ícone genérico
    SendMessageW(M.lv[TAB_PROGRAMS], LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)M.icons);
    if (old) ImageList_Destroy(old);  // a lista compartilha (LVS_SHAREIMAGELISTS): quem destrói somos nós
    for (size_t i = 0; i < M.programs.n; i++) ((Program *)M.programs.items[i])->icon = -1;
}

static void programs_reload(void) {
    SetCursor(LoadCursorW(NULL, IDC_WAIT));
    programs_free(&M.programs);
    M.programs = programs_load();
    icons_reset();
    programs_refilter();
}

static int program_icon(Program *p) {
    if (p->icon >= 0) return p->icon;
    p->icon = 0;
    int idx;
    WCHAR *src = program_icon_source(p, &idx);
    if (src && file_exists(src)) {
        HICON small = NULL;
        if (ExtractIconExW(src, idx, NULL, &small, 1) && small) {
            int r = ImageList_ReplaceIcon(M.icons, -1, small);
            if (r >= 0) p->icon = r;
            DestroyIcon(small);
        }
    }
    xfree(src);
    return p->icon;
}

static void programs_dispinfo(NMLVDISPINFOW *di) {
    LVITEMW *it = &di->item;
    if (it->iItem < 0 || (size_t)it->iItem >= M.nview) return;
    Program *p = M.programs.items[M.view[it->iItem]];
    if (it->mask & LVIF_IMAGE) it->iImage = program_icon(p);
    if (!(it->mask & LVIF_TEXT)) return;
    WCHAR *tmp = NULL;
    switch (it->iSubItem) {
    case 0: put_text(it, p->name); break;
    case 1: put_text(it, p->version ? p->version : u""); break;
    case 2: put_text(it, p->publisher ? p->publisher : u""); break;
    case 3:
        tmp = p->size ? fmt_size(p->size, &g_fmt) : wdup(u"");
        put_text(it, tmp);
        break;
    case 4:
        tmp = p->year ? fmt_date(p->year, p->month, p->day, &g_fmt) : wdup(u"");
        put_text(it, tmp);
        break;
    case 5: put_text(it, p->scope == SCOPE_USER ? T(S_SCOPE_USER) : p->scope == SCOPE_MACHINE32 ? T(S_SCOPE_32) : T(S_SCOPE_64)); break;
    }
    xfree(tmp);
}

// Programas selecionados (vetor alocado; n = quantidade)
static Program **selected_programs(size_t *n) {
    *n = 0;
    int count = lv_selected_count(M.lv[TAB_PROGRAMS]);
    if (count <= 0) return NULL;
    Program **v = xalloc((size_t)count * sizeof(Program *));
    for (int i = lv_selected(M.lv[TAB_PROGRAMS], -1); i >= 0 && (int)*n < count; i = lv_selected(M.lv[TAB_PROGRAMS], i))
        if ((size_t)i < M.nview) v[(*n)++] = M.programs.items[M.view[i]];
    return v;
}

static void do_uninstall(bool quick) {
    size_t n;
    Program **v = selected_programs(&n);
    if (!n) {
        msg(M.wnd, T(S_SELECT_PROGRAM), MB_OK | MB_ICONINFORMATION);
        xfree(v);
        return;
    }
    if (op_uninstall(M.wnd, v, n, quick)) programs_reload();
    xfree(v);
}

static void do_forced(void) {
    size_t n;
    Program **v = selected_programs(&n);
    const WCHAR *name = n == 1 ? v[0]->name : NULL;
    WCHAR *folder = NULL;
    if (n == 1 && v[0]->install_location) {
        folder = expand_env(v[0]->install_location);
        if (protect_dir_blocked(folder)) xfree(folder), folder = NULL;
    }
    if (op_forced(M.wnd, name, folder)) programs_reload();
    xfree(folder), xfree(v);
}

static void do_open_folder(void) {
    size_t n;
    Program **v = selected_programs(&n);
    if (!n) {
        xfree(v);
        return;
    }
    Identity *id = program_identity(v[0]);
    const WCHAR *dir = NULL;
    for (size_t i = 0; i < id->dirs.n && !dir; i++)
        if (dir_exists(id->dirs.items[i])) dir = id->dirs.items[i];
    if (dir) shell_open(dir);
    else msg(M.wnd, T(S_NO_FOLDER), MB_OK | MB_ICONINFORMATION);
    identity_free(id);
    xfree(v);
}

static void programs_menu(void) {
    size_t n;
    Program **v = selected_programs(&n);
    xfree(v);
    if (!n) return;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | MF_DEFAULT, B_UNINSTALL, T(S_BTN_UNINSTALL));
    AppendMenuW(m, MF_STRING, B_QUICK, T(S_BTN_QUICK));
    AppendMenuW(m, MF_STRING | (n == 1 ? 0 : MF_GRAYED), B_FORCED, T(S_BTN_FORCED));
    AppendMenuW(m, MF_STRING | (n == 1 ? 0 : MF_GRAYED), B_OPEN_FOLDER, T(S_BTN_OPEN_FOLDER));
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, B_REFRESH_PROGS, T(S_BTN_REFRESH));
    POINT pt;
    GetCursorPos(&pt);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, M.wnd, NULL);
    DestroyMenu(m);
    if (cmd) PostMessageW(M.wnd, WM_COMMAND, (WPARAM)cmd, 0);
}

// ---------------------------------------------------------------- apps do Windows

static void apps_fill(void) {
    HWND lv = M.lv[TAB_APPS];
    lv_clear(lv);
    WCHAR *f = search_text();
    size_t shown = 0;
    for (size_t i = 0; i < M.apps.n; i++) {
        AppxPackage *a = M.apps.items[i];
        if (!matches(f, a->name, a->publisher)) continue;
        int row = lv_insert(lv, (int)shown++, a->name, (LPARAM)i);
        lv_text(lv, row, 1, a->version);
        lv_text(lv, row, 2, a->publisher);
        lv_text(lv, row, 3, a->removable ? T(S_APP_REMOVABLE) : T(S_APP_SYSTEM));
        lv_text(lv, row, 4, a->full_name);
    }
    if (M.cur == TAB_APPS) {
        WCHAR *t = wfmt(T(S_STATUS_APPS), (unsigned)shown);
        status(t);
        xfree(t);
    }
    xfree(f);
}

static DWORD WINAPI apps_thread(LPVOID arg) {
    (void)arg;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    AppsResult *r = xalloc(sizeof(AppsResult));
    r->list = appx_list(&r->error);
    CoUninitialize();
    if (!PostMessageW(M.wnd, WM_APP_LOADED, 0, (LPARAM)r)) {
        appx_list_free(&r->list);
        xfree(r->error), xfree(r);
    }
    return 0;
}

static void apps_load(void) {
    if (M.apps_loading) return;
    M.apps_loading = true;
    lv_clear(M.lv[TAB_APPS]);
    status(T(S_STATUS_APPS_LOADING));
    HANDLE th = CreateThread(NULL, 0, apps_thread, NULL, 0, NULL);
    if (th) CloseHandle(th);
    else M.apps_loading = false;
}

static void apps_loaded(AppsResult *r) {
    M.apps_loading = false;
    M.apps_loaded = true;
    appx_list_free(&M.apps);
    M.apps = r->list;
    apps_fill();
    if (r->error) {
        WCHAR *t = wfmt(T(S_ERR_APPS), r->error);
        msg(M.wnd, t, MB_OK | MB_ICONWARNING);
        xfree(t);
    }
    xfree(r->error), xfree(r);
}

static AppxPackage *selected_app(void) {
    int i = lv_selected(M.lv[TAB_APPS], -1);
    if (i < 0) return NULL;
    size_t idx = (size_t)lv_param(M.lv[TAB_APPS], i);
    return idx < M.apps.n ? M.apps.items[idx] : NULL;
}

typedef struct {
    const WCHAR *full_name;
    bool all_users;
    WCHAR *error;
} AppJob;

static void app_work(Progress *pg, void *arg) {
    AppJob *job = arg;
    pg_allow_cancel(pg, false);
    pg_step(pg, T(S_STEP_REMOVING_APP));
    pg_detail(pg, job->full_name);
    job->error = appx_remove(job->full_name, job->all_users);
}

static void do_app_remove(bool all_users) {
    AppxPackage *a = selected_app();
    if (!a) {
        msg(M.wnd, T(S_SELECT_APP), MB_OK | MB_ICONINFORMATION);
        return;
    }
    WCHAR *q = wfmt(a->removable ? T(S_CONFIRM_APP) : T(S_CONFIRM_SYSTEM_APP), a->name, all_users ? T(S_ALL_USERS_SUFFIX) : u"");
    int ans = msg(M.wnd, q, MB_YESNO | (a->removable ? MB_ICONQUESTION : MB_ICONWARNING) | MB_DEFBUTTON2);
    xfree(q);
    if (ans != IDYES) return;
    AppJob job = {a->full_name, all_users, NULL};
    progress_run(M.wnd, T(S_BTN_APP_REMOVE), app_work, &job);
    if (job.error) {
        WCHAR *t = wfmt(T(S_ERR_APP_REMOVE), job.error);
        msg(M.wnd, t, MB_OK | MB_ICONERROR);
        xfree(t), xfree(job.error);
    } else {
        msg(M.wnd, T(S_APP_REMOVED), MB_OK | MB_ICONINFORMATION);
    }
    apps_load();
}

// ---------------------------------------------------------------- rastreados

static void traces_fill(void) {
    traces_list_free(&M.traces);
    M.traces = traces_list();
    HWND lv = M.lv[TAB_TRACES];
    lv_clear(lv);
    WCHAR *f = search_text();
    size_t shown = 0;
    for (size_t i = 0; i < M.traces.n; i++) {
        TraceFile *t = M.traces.items[i];
        if (!matches(f, t->name, NULL)) continue;
        int row = lv_insert(lv, (int)shown++, t->name, (LPARAM)i);
        lv_text(lv, row, 1, t->date);
        WCHAR *n = wfmt(u"%u", (unsigned)t->items);
        lv_text(lv, row, 2, n);
        xfree(n);
        lv_text(lv, row, 3, path_leaf(t->path));
    }
    if (M.cur == TAB_TRACES) {
        WCHAR *t = wfmt(T(S_STATUS_TRACES), (unsigned)shown);
        status(t);
        xfree(t);
    }
    xfree(f);
}

static TraceFile *selected_trace(void) {
    int i = lv_selected(M.lv[TAB_TRACES], -1);
    if (i < 0) return NULL;
    size_t idx = (size_t)lv_param(M.lv[TAB_TRACES], i);
    return idx < M.traces.n ? M.traces.items[idx] : NULL;
}

// ---------------------------------------------------------------- inicialização

static void startup_fill(void) {
    startup_list_free(&M.startup);
    M.startup = startup_list();
    HWND lv = M.lv[TAB_STARTUP];
    lv_clear(lv);
    WCHAR *f = search_text();
    size_t shown = 0;
    for (size_t i = 0; i < M.startup.n; i++) {
        StartupEntry *e = M.startup.items[i];
        if (!matches(f, e->name, e->command)) continue;
        int row = lv_insert(lv, (int)shown++, e->name, (LPARAM)i);
        lv_text(lv, row, 1, e->enabled ? T(S_ENABLED) : T(S_DISABLED));
        lv_text(lv, row, 2, e->command);
        lv_text(lv, row, 3, startup_source_text(e->source));
    }
    if (M.cur == TAB_STARTUP) {
        WCHAR *t = wfmt(T(S_STATUS_STARTUP), (unsigned)shown);
        status(t);
        xfree(t);
    }
    xfree(f);
}

static StartupEntry *selected_startup(void) {
    int i = lv_selected(M.lv[TAB_STARTUP], -1);
    if (i < 0) return NULL;
    size_t idx = (size_t)lv_param(M.lv[TAB_STARTUP], i);
    return idx < M.startup.n ? M.startup.items[idx] : NULL;
}

// ---------------------------------------------------------------- backups

static void backups_fill(void) {
    backups_list_free(&M.backups);
    M.backups = backups_list();
    HWND lv = M.lv[TAB_BACKUPS];
    lv_clear(lv);
    WCHAR *f = search_text();
    size_t shown = 0;
    for (size_t i = 0; i < M.backups.n; i++) {
        BackupFile *b = M.backups.items[i];
        if (!matches(f, b->file, NULL)) continue;
        int row = lv_insert(lv, (int)shown++, b->file, (LPARAM)i);
        WCHAR *d = filetime_text(&b->mtime), *s = fmt_size(b->size, &g_fmt);
        lv_text(lv, row, 1, d);
        lv_text(lv, row, 2, s);
        xfree(d), xfree(s);
    }
    if (M.cur == TAB_BACKUPS) {
        WCHAR *t = wfmt(T(S_STATUS_BACKUPS), (unsigned)shown, g_paths.backups_dir);
        status(t);
        xfree(t);
    }
    xfree(f);
}

static BackupFile *selected_backup(void) {
    int i = lv_selected(M.lv[TAB_BACKUPS], -1);
    if (i < 0) return NULL;
    size_t idx = (size_t)lv_param(M.lv[TAB_BACKUPS], i);
    return idx < M.backups.n ? M.backups.items[idx] : NULL;
}

// ---------------------------------------------------------------- abas e layout

static void refresh_tab(Tab t) {
    switch (t) {
    case TAB_PROGRAMS: programs_refilter(); break;
    case TAB_APPS:
        if (!M.apps_loaded) apps_load();
        else apps_fill();
        break;
    case TAB_TRACES: traces_fill(); break;
    case TAB_STARTUP: startup_fill(); break;
    case TAB_BACKUPS: backups_fill(); break;
    case TAB_COUNT: break;
    }
}

static void layout(void) {
    RECT r;
    GetClientRect(M.wnd, &r);
    SendMessageW(M.status, WM_SIZE, 0, 0);
    RECT sr;
    GetWindowRect(M.status, &sr);
    int sb = sr.bottom - sr.top;
    int m = S(10), w = r.right - 2 * m, y = m;
    ui_place(M.tabs, m, y, w, S(30));
    y += S(38);
    int bh = S(28), x = m;
    for (int i = 0; i < 7 && M.btn[M.cur][i]; i++) {
        int bw = ui_button_width(T(BUTTONS[M.cur][i].text));
        ui_place(M.btn[M.cur][i], x, y, bw, bh);
        x += bw + S(6);
    }
    int sw = S(260);
    ui_place(M.search, r.right - m - sw, y + S(2), sw, S(24));
    y += bh + S(8);
    for (int t = 0; t < TAB_COUNT; t++) ui_place(M.lv[t], m, y, w, r.bottom - sb - S(8) - y);
}

static void show_tab(Tab t) {
    M.cur = t;
    for (int k = 0; k < TAB_COUNT; k++) {
        ShowWindow(M.lv[k], k == (int)t ? SW_SHOW : SW_HIDE);
        for (int i = 0; i < 7 && M.btn[k][i]; i++) ShowWindow(M.btn[k][i], k == (int)t ? SW_SHOW : SW_HIDE);
    }
    layout();
    refresh_tab(t);
    SetFocus(M.lv[t]);
}

static void create_lists(HWND h) {
    // Programas: lista virtual com ícones e seleção múltipla
    M.lv[TAB_PROGRAMS] = lv_create(h, ID_LV + TAB_PROGRAMS, LVS_OWNERDATA | LVS_SHAREIMAGELISTS, 0);
    HWND lv = M.lv[TAB_PROGRAMS];
    lv_column(lv, 0, T(S_COL_NAME), S(330), false);
    lv_column(lv, 1, T(S_COL_VERSION), S(120), false);
    lv_column(lv, 2, T(S_COL_PUBLISHER), S(220), false);
    lv_column(lv, 3, T(S_COL_SIZE), S(80), true);
    lv_column(lv, 4, T(S_COL_INSTALLED), S(100), false);
    lv_column(lv, 5, T(S_COL_SCOPE), S(90), false);

    lv = M.lv[TAB_APPS] = lv_create(h, ID_LV + TAB_APPS, LVS_SINGLESEL, 0);
    lv_column(lv, 0, T(S_COL_NAME), S(300), false);
    lv_column(lv, 1, T(S_COL_VERSION), S(120), false);
    lv_column(lv, 2, T(S_COL_PUBLISHER), S(220), false);
    lv_column(lv, 3, T(S_COL_STATUS), S(110), false);
    lv_column(lv, 4, T(S_COL_PACKAGE), S(360), false);

    lv = M.lv[TAB_TRACES] = lv_create(h, ID_LV + TAB_TRACES, LVS_SINGLESEL, 0);
    lv_column(lv, 0, T(S_COL_NAME), S(320), false);
    lv_column(lv, 1, T(S_COL_DATE), S(150), false);
    lv_column(lv, 2, T(S_COL_ITEMS), S(80), true);
    lv_column(lv, 3, T(S_COL_FILE), S(380), false);

    lv = M.lv[TAB_STARTUP] = lv_create(h, ID_LV + TAB_STARTUP, LVS_SINGLESEL, 0);
    lv_column(lv, 0, T(S_COL_NAME), S(220), false);
    lv_column(lv, 1, T(S_COL_STATE), S(90), false);
    lv_column(lv, 2, T(S_COL_COMMAND), S(480), false);
    lv_column(lv, 3, T(S_COL_SOURCE), S(200), false);

    lv = M.lv[TAB_BACKUPS] = lv_create(h, ID_LV + TAB_BACKUPS, LVS_SINGLESEL, 0);
    lv_column(lv, 0, T(S_COL_FILE), S(520), false);
    lv_column(lv, 1, T(S_COL_DATE), S(150), false);
    lv_column(lv, 2, T(S_COL_SIZE), S(90), true);
}

static void create_controls(HWND h) {
    M.tabs = ui_child(h, u"SysTabControl32", u"", WS_TABSTOP | WS_CLIPSIBLINGS, 0, ID_TABS);
    static const StrId tabs[TAB_COUNT] = {S_TAB_PROGRAMS, S_TAB_APPS, S_TAB_TRACES, S_TAB_STARTUP, S_TAB_BACKUPS};
    for (int i = 0; i < TAB_COUNT; i++) {
        TCITEMW ti;
        mem_set(&ti, 0, sizeof(ti));
        ti.mask = TCIF_TEXT;
        ti.pszText = (LPWSTR)T(tabs[i]);
        SendMessageW(M.tabs, TCM_INSERTITEMW, (WPARAM)i, (LPARAM)&ti);
    }
    for (int t = 0; t < TAB_COUNT; t++)
        for (int i = 0; i < 7 && BUTTONS[t][i].id; i++) M.btn[t][i] = ui_button(h, T(BUTTONS[t][i].text), BUTTONS[t][i].id);
    M.search = ui_edit(h, u"", ID_SEARCH, 0);
    SendMessageW(M.search, EM_SETCUEBANNER, TRUE, (LPARAM)T(S_SEARCH_HINT));
    create_lists(h);
    M.status = ui_child(h, u"msctls_statusbar32", u"", SBARS_SIZEGRIP, 0, ID_STATUS);
    M.sort_col = 0;
    M.sort_dir = 1;
}

static void apply_fonts(void) {
    HWND all[64];
    int n = 0;
    all[n++] = M.tabs, all[n++] = M.search, all[n++] = M.status;
    for (int t = 0; t < TAB_COUNT; t++) {
        all[n++] = M.lv[t];
        for (int i = 0; i < 7 && M.btn[t][i]; i++) all[n++] = M.btn[t][i];
    }
    for (int i = 0; i < n; i++) SendMessageW(all[i], WM_SETFONT, (WPARAM)ui_font(), TRUE);
}

// ---------------------------------------------------------------- comandos

static void on_command(int id) {
    switch (id) {
    case B_UNINSTALL: do_uninstall(false); break;
    case B_QUICK: do_uninstall(true); break;
    case B_FORCED: do_forced(); break;
    case B_HUNTER:
        if (hunter_run(M.wnd)) programs_reload();
        break;
    case B_OPEN_FOLDER: do_open_folder(); break;
    case B_REFRESH_PROGS: programs_reload(); break;

    case B_APP_REMOVE: do_app_remove(false); break;
    case B_APP_REMOVE_ALL: do_app_remove(true); break;
    case B_APP_OPEN: {
        AppxPackage *a = selected_app();
        if (a && a->location[0] && dir_exists(a->location)) shell_open(a->location);
        else if (a) msg(M.wnd, T(S_NO_FOLDER), MB_OK | MB_ICONINFORMATION);
        break;
    }
    case B_APP_REFRESH: apps_load(); break;

    case B_TRACE_NEW:
        if (op_trace_install(M.wnd)) traces_fill();
        programs_reload();
        break;
    case B_TRACE_UNINSTALL: {
        TraceFile *t = selected_trace();
        if (!t) {
            msg(M.wnd, T(S_SELECT_TRACE), MB_OK | MB_ICONINFORMATION);
            break;
        }
        WCHAR *path = wdup(t->path);
        if (op_trace_uninstall(M.wnd, path, &M.programs)) programs_reload();
        xfree(path);
        traces_fill();
        break;
    }
    case B_TRACE_VIEW: {
        TraceFile *t = selected_trace();
        if (t) open_with_notepad(t->path);
        break;
    }
    case B_TRACE_DELETE: {
        TraceFile *t = selected_trace();
        if (!t) break;
        WCHAR *q = wfmt(T(S_CONFIRM_DELETE_TRACE), t->name);
        if (msg(M.wnd, q, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) fs_recycle(t->path);
        xfree(q);
        traces_fill();
        break;
    }
    case B_TRACE_REFRESH: traces_fill(); break;

    case B_ST_ENABLE:
    case B_ST_DISABLE: {
        StartupEntry *e = selected_startup();
        if (!e) break;
        if (!e->can_toggle) {
            msg(M.wnd, T(S_STARTUP_NO_TOGGLE), MB_OK | MB_ICONINFORMATION);
            break;
        }
        DWORD r = startup_set_enabled(e, id == B_ST_ENABLE);
        if (r) {
            WCHAR *err = win_error_text(r);
            msg(M.wnd, err, MB_OK | MB_ICONERROR);
            xfree(err);
        }
        startup_fill();
        break;
    }
    case B_ST_DELETE: {
        StartupEntry *e = selected_startup();
        if (!e) break;
        WCHAR *q = wfmt(T(S_CONFIRM_DELETE_STARTUP), e->name);
        int ans = msg(M.wnd, q, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        xfree(q);
        if (ans != IDYES) break;
        WCHAR *bk = NULL;
        DWORD r = startup_delete(e, &bk);
        if (r) {
            WCHAR *err = win_error_text(r);
            msg(M.wnd, err, MB_OK | MB_ICONERROR);
            xfree(err);
        } else if (bk) {
            WCHAR *t = wfmt(T(S_BACKUP_SAVED), bk);
            msg(M.wnd, t, MB_OK | MB_ICONINFORMATION);
            xfree(t);
        }
        xfree(bk);
        startup_fill();
        break;
    }
    case B_ST_OPEN: {
        StartupEntry *e = selected_startup();
        if (!e) break;
        WCHAR *exe = startup_exe(e);
        if (exe && file_exists(exe)) shell_select(exe);
        else if (e->source == SRC_USER_FOLDER || e->source == SRC_COMMON_FOLDER) shell_select(e->location);
        else msg(M.wnd, T(S_NO_FOLDER), MB_OK | MB_ICONINFORMATION);
        xfree(exe);
        break;
    }
    case B_ST_REFRESH: startup_fill(); break;

    case B_BK_RESTORE: {
        BackupFile *b = selected_backup();
        if (!b) {
            msg(M.wnd, T(S_SELECT_BACKUP), MB_OK | MB_ICONINFORMATION);
            break;
        }
        WCHAR *q = wfmt(T(S_CONFIRM_RESTORE), b->file);
        int ans = msg(M.wnd, q, MB_YESNO | MB_ICONQUESTION);
        xfree(q);
        if (ans != IDYES) break;
        SetCursor(LoadCursorW(NULL, IDC_WAIT));
        WCHAR *err = backup_restore(b->path);
        if (err) {
            WCHAR *t = wfmt(T(S_ERR_RESTORE), err);
            msg(M.wnd, t, MB_OK | MB_ICONERROR);
            xfree(t), xfree(err);
        } else {
            msg(M.wnd, T(S_RESTORED), MB_OK | MB_ICONINFORMATION);
        }
        break;
    }
    case B_BK_OPEN: shell_open(g_paths.backups_dir); break;
    case B_BK_DELETE: {
        BackupFile *b = selected_backup();
        if (!b) break;
        WCHAR *q = wfmt(T(S_CONFIRM_DELETE_BACKUP), b->file);
        if (msg(M.wnd, q, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) fs_recycle(b->path);
        xfree(q);
        backups_fill();
        break;
    }
    case B_BK_SYSRESTORE: {
        WCHAR *rs = system_exe(u"rstrui.exe");
        ShellExecuteW(M.wnd, u"open", rs, NULL, NULL, SW_SHOWNORMAL);
        xfree(rs);
        break;
    }
    case B_BK_RECYCLE: shell_open(u"shell:RecycleBinFolder"); break;
    case B_BK_LOG:
        if (file_exists(g_paths.log_path)) open_with_notepad(g_paths.log_path);
        else msg(M.wnd, T(S_NO_LOG), MB_OK | MB_ICONINFORMATION);
        break;
    }
}

static void on_notify(NMHDR *nm) {
    if (nm->idFrom == ID_TABS && nm->code == TCN_SELCHANGE) {
        show_tab((Tab)SendMessageW(M.tabs, TCM_GETCURSEL, 0, 0));
        return;
    }
    if (nm->idFrom == ID_LV + TAB_PROGRAMS) {
        switch (nm->code) {
        case LVN_GETDISPINFOW: programs_dispinfo((NMLVDISPINFOW *)nm); break;
        case LVN_COLUMNCLICK: {
            int col = ((NMLISTVIEW *)nm)->iSubItem;
            M.sort_dir = (col == M.sort_col) ? -M.sort_dir : 1;
            M.sort_col = col;
            programs_refilter();
            break;
        }
        case NM_DBLCLK:
            if (((NMITEMACTIVATE *)nm)->iItem >= 0) do_uninstall(false);
            break;
        case NM_RCLICK: programs_menu(); break;
        case LVN_KEYDOWN: {
            WORD vk = ((NMLVKEYDOWN *)nm)->wVKey;
            if (vk == VK_DELETE) do_uninstall(false);
            else if (vk == VK_APPS) programs_menu();
            break;
        }
        }
    }
}

// ---------------------------------------------------------------- janela

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE:
        M.wnd = h;
        create_controls(h);
        icons_reset();
        return 0;
    case WM_SIZE:
        if (M.tabs) layout();
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = S(820);
        mm->ptMinTrackSize.y = S(480);
        return 0;
    }
    case WM_DPICHANGED: {
        ui_set_dpi(HIWORD(wp));
        apply_fonts();
        RECT *r = (RECT *)lp;
        SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        icons_reset();
        layout();
        InvalidateRect(M.lv[TAB_PROGRAMS], NULL, TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == ID_SEARCH && HIWORD(wp) == EN_CHANGE) refresh_tab(M.cur == TAB_APPS && !M.apps_loaded ? TAB_COUNT : M.cur);
        else on_command(LOWORD(wp));
        return 0;
    case WM_NOTIFY:
        on_notify((NMHDR *)lp);
        return 0;
    case WM_APP_LOADED:
        apps_loaded((AppsResult *)lp);
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

HWND mainwin_create(void) {
    if (!ui_register(u"ExtirpaMain", proc)) return NULL;
    HWND h = CreateWindowExW(WS_EX_CONTROLPARENT, u"ExtirpaMain", T(S_APP_TITLE), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1000, 640,
                             NULL, NULL, g_inst, NULL);
    if (!h) return NULL;
    UINT dpi = GetDpiForWindow(h);
    if (dpi && dpi != g_dpi) {
        ui_set_dpi(dpi);
        apply_fonts();
        icons_reset();
    }
    ui_center(h, NULL, S(1120), S(700));
    show_tab(TAB_PROGRAMS);
    programs_reload();
    ShowWindow(h, SW_SHOW);
    UpdateWindow(h);
    return h;
}

bool mainwin_pretranslate(MSG *m) {
    if (m->message != WM_KEYDOWN || !M.wnd || !IsWindowEnabled(M.wnd)) return false;
    if (m->wParam == 'F' && GetKeyState(VK_CONTROL) < 0) {
        SetFocus(M.search);
        SendMessageW(M.search, EM_SETSEL, 0, -1);
        return true;
    }
    if (m->wParam == VK_F5) {
        if (M.cur == TAB_PROGRAMS) programs_reload();
        else if (M.cur == TAB_APPS) apps_load();
        else refresh_tab(M.cur);
        return true;
    }
    if (m->wParam == VK_ESCAPE && m->hwnd == M.search) {
        SetWindowTextW(M.search, u"");
        return true;
    }
    return false;
}

HWND mainwin_handle(void) { return M.wnd; }
