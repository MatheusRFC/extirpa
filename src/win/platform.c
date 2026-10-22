// SPDX-License-Identifier: MIT
#include "platform.h"

#include <stdarg.h>

#include "../core/pathx.h"
#include "../core/protect.h"

Paths g_paths;
FmtStyle g_fmt = {'.', false};
static SRWLOCK g_log_lock;

// ---------------------------------------------------------------- memória

static void out_of_memory(void) {
    MessageBoxW(NULL, u"Out of memory / Memória insuficiente", u"EXTIRPA", MB_OK | MB_ICONERROR);
    ExitProcess(3);
}

void *xalloc(size_t n) {
    void *p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n ? n : 1);
    if (!p) out_of_memory();
    return p;
}

void *xrealloc(void *p, size_t n) {
    if (!p) return xalloc(n);
    void *r = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, p, n ? n : 1);
    if (!r) out_of_memory();
    return r;
}

void xfree(void *p) {
    if (p) HeapFree(GetProcessHeap(), 0, p);
}

// ---------------------------------------------------------------- pastas

static WCHAR *known_folder(const GUID *id) {
    PWSTR p = NULL;
    WCHAR *r = NULL;
    if (SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, NULL, &p) == 0 && p && p[0]) r = path_norm(p);
    if (p) CoTaskMemFree(p);
    return r;
}

static WCHAR *sys_dir(UINT(WINAPI *f)(LPWSTR, UINT)) {
    WCHAR buf[MAX_PATH + 1];
    UINT n = f(buf, MAX_PATH);
    if (!n || n >= MAX_PATH) return NULL;
    buf[n] = 0;
    return path_norm(buf);
}

static void protect_dir(const WCHAR *p) {
    if (p) protect_add_dir(p);
}

static void protect_sub(const WCHAR *base, const WCHAR *sub, bool tree) {
    if (!base) return;
    WCHAR *p = path_join(base, sub);
    if (tree) protect_add_dir_tree(p);
    else protect_add_dir(p);
    xfree(p);
}

void plat_init(void) {
    InitializeSRWLock(&g_log_lock);
    Paths *p = &g_paths;
    p->windows = sys_dir(GetWindowsDirectoryW);
    p->system32 = sys_dir(GetSystemDirectoryW);
    WCHAR *exe = xalloc(32768 * sizeof(WCHAR));
    DWORD n = GetModuleFileNameW(NULL, exe, 32767);
    p->exe = n ? path_norm(exe) : wdup(u"");
    xfree(exe);

    p->program_files = known_folder(&FOLDERID_ProgramFiles);
    p->program_files_x86 = known_folder(&FOLDERID_ProgramFilesX86);
    p->common_files = known_folder(&FOLDERID_ProgramFilesCommon);
    p->common_files_x86 = known_folder(&FOLDERID_ProgramFilesCommonX86);
    p->program_data = known_folder(&FOLDERID_ProgramData);
    p->profile = known_folder(&FOLDERID_Profile);
    p->profiles_root = known_folder(&FOLDERID_UserProfiles);
    p->public_dir = known_folder(&FOLDERID_Public);
    p->appdata = known_folder(&FOLDERID_RoamingAppData);
    p->local_appdata = known_folder(&FOLDERID_LocalAppData);
    p->locallow = known_folder(&FOLDERID_LocalAppDataLow);
    p->desktop = known_folder(&FOLDERID_Desktop);
    p->public_desktop = known_folder(&FOLDERID_PublicDesktop);
    p->documents = known_folder(&FOLDERID_Documents);
    p->public_documents = known_folder(&FOLDERID_PublicDocuments);
    p->start_menu = known_folder(&FOLDERID_StartMenu);
    p->common_start_menu = known_folder(&FOLDERID_CommonStartMenu);
    p->programs = known_folder(&FOLDERID_Programs);
    p->common_programs = known_folder(&FOLDERID_CommonPrograms);
    p->startup = known_folder(&FOLDERID_Startup);
    p->common_startup = known_folder(&FOLDERID_CommonStartup);
    p->temp = p->local_appdata ? path_join(p->local_appdata, u"Temp") : NULL;

    // pasta de dados do EXTIRPA (backups, rastreamentos, log)
    WCHAR *base = p->local_appdata ? p->local_appdata : p->windows;
    p->data_dir = path_join(base, u"EXTIRPA");
    p->backups_dir = path_join(p->data_dir, u"Backups");
    p->traces_dir = path_join(p->data_dir, u"Traces");
    p->log_path = path_join(p->data_dir, u"extirpa.log");
    p->settings_path = path_join(p->data_dir, u"settings.ini");
    dir_create_all(p->backups_dir);
    dir_create_all(p->traces_dir);

    // Lista de proteção: as pastas do sistema e do usuário nunca são apagadas, nem
    // nada que as contenha. A pasta do Windows e algumas outras, nem por dentro.
    protect_reset();
    protect_set_windows(p->windows);
    if (p->windows) protect_add_dir_tree(p->windows);
    protect_add_dir_tree(p->data_dir);
    protect_dir(p->exe);
    const WCHAR *dirs[] = {p->system32,     p->program_files,   p->program_files_x86, p->common_files,      p->common_files_x86, p->program_data,
                           p->profile,      p->profiles_root,   p->public_dir,        p->appdata,           p->local_appdata,    p->locallow,
                           p->temp,         p->desktop,         p->public_desktop,    p->documents,         p->public_documents, p->start_menu,
                           p->common_start_menu, p->programs,   p->common_programs,   p->startup,           p->common_startup};
    for (size_t i = 0; i < COUNT(dirs); i++) protect_dir(dirs[i]);
    const GUID *more[] = {&FOLDERID_Downloads, &FOLDERID_Music, &FOLDERID_Pictures, &FOLDERID_Videos, &FOLDERID_SavedGames};
    for (size_t i = 0; i < COUNT(more); i++) {
        WCHAR *d = known_folder(more[i]);
        protect_dir(d);
        xfree(d);
    }
    protect_sub(p->appdata, u"Microsoft", false);
    protect_sub(p->appdata, u"Microsoft\\Windows", false);
    protect_sub(p->local_appdata, u"Microsoft", false);
    protect_sub(p->local_appdata, u"Microsoft\\Windows", false);
    protect_sub(p->local_appdata, u"Programs", false);
    protect_sub(p->local_appdata, u"Packages", true);
    protect_sub(p->program_data, u"Microsoft", false);
    protect_sub(p->program_data, u"Microsoft\\Windows", false);
    protect_sub(p->program_data, u"Packages", true);
    protect_sub(p->common_files, u"microsoft shared", false);
    protect_sub(p->common_files_x86, u"microsoft shared", false);
    static const WCHAR *const pf_trees[] = {u"WindowsApps",        u"ModifiableWindowsApps",
                                           u"Windows Defender",   u"Windows Defender Advanced Threat Protection",
                                           u"Windows Mail",       u"Windows Media Player",
                                           u"Windows NT",         u"Windows Photo Viewer",
                                           u"Windows Portable Devices", u"Windows Security",
                                           u"Windows Sidebar",    u"WindowsPowerShell",
                                           u"Internet Explorer",  u"Reference Assemblies",
                                           u"Common Files\\System", u"Common Files\\Services"};
    for (size_t i = 0; i < COUNT(pf_trees); i++) {
        protect_sub(p->program_files, pf_trees[i], true);
        protect_sub(p->program_files_x86, pf_trees[i], true);
    }
    if (p->windows && wlen(p->windows) >= 3) {
        WCHAR drive[4] = {p->windows[0], ':', '\\', 0};
        protect_sub(drive, u"System Volume Information", true);
        protect_sub(drive, u"$Recycle.Bin", true);
        protect_sub(drive, u"Recovery", true);
        protect_sub(drive, u"Boot", true);
        protect_sub(drive, u"PerfLogs", false);
    }
}

// ---------------------------------------------------------------- arquivos

bool file_exists(const WCHAR *path) { return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES; }

bool dir_exists(const WCHAR *path) {
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

uint64_t file_size(const WCHAR *path) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &d)) return 0;
    return ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}

bool file_read_all(const WCHAR *path, uint8_t **data, size_t *len) {
    *data = NULL;
    *len = 0;
    uint64_t size = file_size(path);
    if (size > 256u * 1024 * 1024) return false;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    uint8_t *buf = xalloc((size_t)size + 2);
    size_t got = 0;
    while (got < size) {
        DWORD r = 0;
        if (!ReadFile(h, buf + got, (DWORD)MIN(size - got, (uint64_t)(1u << 30)), &r, NULL) || !r) break;
        got += r;
    }
    CloseHandle(h);
    *data = buf;
    *len = got;
    return true;
}

bool file_write_all(const WCHAR *path, const void *data, size_t len) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    const uint8_t *p = data;
    bool ok = true;
    while (len && ok) {
        DWORD w = 0;
        DWORD chunk = (DWORD)MIN(len, (size_t)(1u << 30));
        ok = WriteFile(h, p, chunk, &w, NULL) && w == chunk;
        p += chunk;
        len -= chunk;
    }
    CloseHandle(h);
    return ok;
}

bool file_write_utf16(const WCHAR *path, const WCHAR *text) {
    size_t n = wlen(text);
    WCHAR *buf = xalloc((n + 1) * sizeof(WCHAR));
    buf[0] = 0xFEFF;
    mem_copy(buf + 1, text, n * sizeof(WCHAR));
    bool ok = file_write_all(path, buf, (n + 1) * sizeof(WCHAR));
    xfree(buf);
    return ok;
}

bool dir_create_all(const WCHAR *path) {
    WCHAR *p = path_norm(path);
    size_t n = wlen(p);
    for (size_t i = 3; i <= n; i++) {
        if (i == n || p[i] == '\\') {
            WCHAR c = p[i];
            p[i] = 0;
            if (!dir_exists(p)) CreateDirectoryW(p, NULL);
            p[i] = c;
        }
    }
    bool ok = dir_exists(p);
    xfree(p);
    return ok;
}

// ---------------------------------------------------------------- texto

WCHAR *expand_env(const WCHAR *s) {
    if (!s) return wdup(u"");
    if (!wfindc(s, '%')) return wdup(s);
    DWORD n = ExpandEnvironmentStringsW(s, NULL, 0);
    if (!n) return wdup(s);
    WCHAR *r = xalloc((n + 1) * sizeof(WCHAR));
    if (!ExpandEnvironmentStringsW(s, r, n + 1)) {
        xfree(r);
        return wdup(s);
    }
    return r;
}

WCHAR *win_error_text(DWORD err) {
    WCHAR buf[512];
    DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0, buf, COUNT(buf), NULL);
    while (n && (buf[n - 1] == '\r' || buf[n - 1] == '\n' || buf[n - 1] == ' ' || buf[n - 1] == '.')) n--;
    if (!n) return wfmt(u"0x%u", (unsigned)err);
    buf[n] = 0;
    return wfmt(u"%s (%u)", buf, (unsigned)err);
}

WCHAR *now_text(bool for_file_name) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    StrBuf b = {0};
    sb_add_u64_pad(&b, t.wYear, 4);
    if (!for_file_name) sb_addc(&b, '-');
    sb_add_u64_pad(&b, t.wMonth, 2);
    if (!for_file_name) sb_addc(&b, '-');
    sb_add_u64_pad(&b, t.wDay, 2);
    sb_addc(&b, for_file_name ? '-' : ' ');
    sb_add_u64_pad(&b, t.wHour, 2);
    if (!for_file_name) sb_addc(&b, ':');
    sb_add_u64_pad(&b, t.wMinute, 2);
    if (!for_file_name) sb_addc(&b, ':');
    sb_add_u64_pad(&b, t.wSecond, 2);
    return sb_take(&b);
}

WCHAR *filetime_text(const FILETIME *ft) {
    FILETIME local;
    SYSTEMTIME t;
    if (!FileTimeToLocalFileTime(ft, &local) || !FileTimeToSystemTime(&local, &t)) return wdup(u"");
    return fmt_datetime(t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, &g_fmt);
}

// ---------------------------------------------------------------- log

void log_text(const WCHAR *text) {
    if (!g_paths.log_path) return;
    WCHAR *stamp = now_text(false);
    WCHAR *line = wcat3(stamp, u"  ", text);
    size_t n;
    char *u8 = w_to_utf8(line, wlen(line), &n);
    AcquireSRWLockExclusive(&g_log_lock);
    HANDLE h = CreateFileW(g_paths.log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w;
        WriteFile(h, u8, (DWORD)n, &w, NULL);
        WriteFile(h, "\r\n", 2, &w, NULL);
        CloseHandle(h);
    }
    ReleaseSRWLockExclusive(&g_log_lock);
    xfree(u8), xfree(line), xfree(stamp);
}

void log_fmt(const WCHAR *pattern, ...) {
    // repassa os argumentos para wfmt montando o texto aqui mesmo (wfmt não tem versão va_list)
    va_list ap;
    va_start(ap, pattern);
    StrBuf b = {0};
    for (const WCHAR *p = pattern; *p; p++) {
        if (*p != '%' || !p[1]) {
            sb_addc(&b, *p);
            continue;
        }
        p++;
        if (*p == 's') {
            const WCHAR *s = va_arg(ap, const WCHAR *);
            sb_add(&b, s ? s : u"");
        } else if (*p == 'd') {
            int v = va_arg(ap, int);
            if (v < 0) sb_addc(&b, '-'), sb_add_u64(&b, (uint64_t)(-(int64_t)v));
            else sb_add_u64(&b, (uint64_t)v);
        } else if (*p == 'u') {
            sb_add_u64(&b, va_arg(ap, unsigned));
        } else if (*p == 'U') {
            sb_add_u64(&b, va_arg(ap, uint64_t));
        } else if (*p == 'c') {
            sb_addc(&b, (WCHAR)va_arg(ap, int));
        } else {
            sb_addc(&b, '%');
            if (*p != '%') sb_addc(&b, *p);
        }
    }
    va_end(ap);
    log_text(sb_str(&b));
    sb_free(&b);
}

// ---------------------------------------------------------------- processos

typedef struct {
    HANDLE process;
    DWORD timeout;
} Watchdog;

static DWORD WINAPI watchdog_thread(LPVOID arg) {
    Watchdog *w = arg;
    if (WaitForSingleObject(w->process, w->timeout) == WAIT_TIMEOUT) TerminateProcess(w->process, 1);
    return 0;
}

#ifdef EXTIRPA_FAKE_WIN
int fake_run_capture(const WCHAR *cmdline, char **out, size_t *out_len);  // tests/fakewin.c
#endif

int run_capture(const WCHAR *cmdline, char **out, size_t *out_len, DWORD timeout_ms) {
#ifdef EXTIRPA_FAKE_WIN
    (void)timeout_ms;
    return fake_run_capture(cmdline, out, out_len);
#endif
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE rd = NULL, wr = NULL;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(u"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    STARTUPINFOW si;
    mem_set(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = nul;
    si.hStdOutput = wr;
    si.hStdError = wr;
    PROCESS_INFORMATION pi;
    mem_set(&pi, 0, sizeof(pi));
    WCHAR *cmd = wdup(cmdline);  // CreateProcessW pode alterar o buffer
    BOOL ok = CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, NULL, NULL, &si, &pi);
    xfree(cmd);
    CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {
        CloseHandle(rd);
        return -1;
    }
    Watchdog wd = {pi.hProcess, timeout_ms};
    HANDLE th = CreateThread(NULL, 0, watchdog_thread, &wd, 0, NULL);

    size_t cap = 4096, len = 0;
    char *buf = xalloc(cap);
    for (;;) {
        if (cap - len < 2048) buf = xrealloc(buf, cap *= 2);
        DWORD r = 0;
        if (!ReadFile(rd, buf + len, (DWORD)(cap - len - 1), &r, NULL) || !r) break;
        len += r;
    }
    buf[len] = 0;
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    if (th) {
        WaitForSingleObject(th, INFINITE);
        CloseHandle(th);
    }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (out) *out = buf;
    else xfree(buf);
    if (out_len) *out_len = len;
    return (int)code;
}

PtrVec processes_under(const PtrVec *paths) {
    PtrVec r = {0};
    DWORD cap = 1024, got = 0;
    DWORD *pids = NULL;
    for (;;) {
        pids = xrealloc(pids, cap * sizeof(DWORD));
        if (!K32EnumProcesses(pids, cap * sizeof(DWORD), &got)) {
            xfree(pids);
            return r;
        }
        if (got < cap * sizeof(DWORD)) break;
        cap *= 2;
    }
    DWORD self = GetCurrentProcessId();
    for (DWORD i = 0; i < got / sizeof(DWORD); i++) {
        if (!pids[i] || pids[i] == self) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pids[i]);
        if (!h) continue;
        WCHAR buf[1024];
        DWORD n = COUNT(buf);
        if (QueryFullProcessImageNameW(h, 0, buf, &n)) {
            buf[n] = 0;
            for (size_t k = 0; k < paths->n; k++)
                if (path_is_under_or_eq(buf, paths->items[k])) {
                    ProcInfo *p = xalloc(sizeof(ProcInfo));
                    p->pid = pids[i];
                    p->exe = wdup(buf);
                    pv_push(&r, p);
                    break;
                }
        }
        CloseHandle(h);
    }
    xfree(pids);
    return r;
}

void processes_free(PtrVec *v) {
    for (size_t i = 0; i < v->n; i++) {
        ProcInfo *p = v->items[i];
        xfree(p->exe), xfree(p);
    }
    pv_free(v);
}

bool process_kill(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (!h) return false;
    bool ok = TerminateProcess(h, 1) != 0;
    if (ok) WaitForSingleObject(h, 5000);
    CloseHandle(h);
    return ok;
}

void shell_open(const WCHAR *target) { ShellExecuteW(NULL, u"open", target, NULL, NULL, SW_SHOWNORMAL); }

void shell_select(const WCHAR *path) {
    WCHAR *args = wfmt(u"/select,\"%s\"", path);
    WCHAR *explorer = g_paths.windows ? path_join(g_paths.windows, u"explorer.exe") : wdup(u"explorer.exe");
    ShellExecuteW(NULL, u"open", explorer, args, NULL, SW_SHOWNORMAL);
    xfree(args), xfree(explorer);
}

WCHAR *system_exe(const WCHAR *name) { return g_paths.system32 ? path_join(g_paths.system32, name) : wdup(name); }

// ---------------------------------------------------------------- configurações

static WCHAR *settings_text(void) {
    uint8_t *data;
    size_t len;
    if (!file_read_all(g_paths.settings_path, &data, &len)) return wdup(u"");
    WCHAR *w = utf8_to_w((const char *)data, len, NULL);
    xfree(data);
    return w;
}

int setting_get_int(const WCHAR *key, int def) {
    WCHAR *t = settings_text();
    int r = def;
    size_t lk = wlen(key);
    for (const WCHAR *p = t; *p;) {
        const WCHAR *eol = p;
        while (*eol && *eol != '\n') eol++;
        if (wieqn(p, key, lk) && p[lk] == '=') {
            WCHAR *v = wdupn(p + lk + 1, (size_t)(eol - p - lk - 1));
            WCHAR *tv = wtrim_dup(v);
            uint64_t n;
            bool neg = tv[0] == '-';
            if (w_to_u64(tv + (neg ? 1 : 0), &n) && n < 0x7FFFFFFF) r = neg ? -(int)n : (int)n;
            xfree(v), xfree(tv);
        }
        p = *eol ? eol + 1 : eol;
    }
    xfree(t);
    return r;
}

void setting_set_int(const WCHAR *key, int value) {
    WCHAR *t = settings_text();
    StrBuf b = {0};
    size_t lk = wlen(key);
    for (const WCHAR *p = t; *p;) {
        const WCHAR *eol = p;
        while (*eol && *eol != '\n') eol++;
        bool same = wieqn(p, key, lk) && p[lk] == '=';
        if (!same && eol > p) {
            sb_addn(&b, p, (size_t)(eol - p));
            sb_addc(&b, '\n');
        }
        p = *eol ? eol + 1 : eol;
    }
    WCHAR *line = wfmt(u"%s=%d\n", key, value);
    sb_add(&b, line);
    size_t n;
    char *u8 = w_to_utf8(sb_str(&b), b.len, &n);
    file_write_all(g_paths.settings_path, u8, n);
    xfree(u8), xfree(line), xfree(t);
    sb_free(&b);
}
