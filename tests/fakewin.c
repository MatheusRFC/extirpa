// SPDX-License-Identifier: MIT
// Um "Windows de mentira" para testar no Linux a parte do EXTIRPA que mexe no Registro e
// no disco (src/win: reg, fsx, programs, scan, startup, monitor...). O Registro e o disco
// são árvores em memória; as funções têm o comportamento documentado da API real nos
// pontos que o EXTIRPA usa (códigos de erro, ERROR_MORE_DATA, enumeração por índice...).
#define _GNU_SOURCE
#include "fakewin.h"

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/pathx.h"

static __thread DWORD g_last_error;
static void fake_misc_reset(void);

// ---------------------------------------------------------------- memória e básicos

HANDLE WINAPI GetProcessHeap(void) { return (HANDLE)1; }
LPVOID WINAPI HeapAlloc(HANDLE h, DWORD f, SIZE_T n) {
    (void)h, (void)f;
    return calloc(1, n ? n : 1);
}
LPVOID WINAPI HeapReAlloc(HANDLE h, DWORD f, LPVOID p, SIZE_T n) {
    (void)h, (void)f;
    size_t old = malloc_usable_size(p);
    unsigned char *r = realloc(p, n ? n : 1);
    if (r && n > old) memset(r + old, 0, n - old);
    return r;
}
BOOL WINAPI HeapFree(HANDLE h, DWORD f, LPVOID p) {
    (void)h, (void)f;
    free(p);
    return TRUE;
}
DWORD WINAPI GetLastError(void) { return g_last_error; }
VOID WINAPI SetLastError(DWORD e) { g_last_error = e; }
VOID WINAPI ExitProcess(UINT c) { exit((int)c); }
int WINAPI MessageBoxW(HWND h, LPCWSTR text, LPCWSTR cap, UINT t) {
    (void)h, (void)cap, (void)t;
    char *u = w_to_utf8(text, wlen(text), NULL);
    fprintf(stderr, "[MessageBox] %s\n", u);
    free(u);
    return IDOK;
}
VOID WINAPI InitializeSRWLock(PSRWLOCK l) { (void)l; }
VOID WINAPI AcquireSRWLockExclusive(PSRWLOCK l) { (void)l; }
VOID WINAPI ReleaseSRWLockExclusive(PSRWLOCK l) { (void)l; }
DWORD WINAPI FormatMessageW(DWORD f, LPCVOID s, DWORD id, DWORD lang, LPWSTR buf, DWORD n, va_list *a) {
    (void)f, (void)s, (void)id, (void)lang, (void)buf, (void)n, (void)a;
    return 0;
}
LANGID WINAPI GetUserDefaultUILanguage(void) { return 0x0416; }
void WINAPI CoTaskMemFree(LPVOID p) { free(p); }
DWORD WINAPI GetCurrentProcessId(void) { return 1234; }

VOID WINAPI GetLocalTime(LPSYSTEMTIME t) {
    memset(t, 0, sizeof(*t));
    t->wYear = 2026, t->wMonth = 10, t->wDay = 18, t->wHour = 12, t->wMinute = 34, t->wSecond = 56;
}
BOOL WINAPI FileTimeToLocalFileTime(const FILETIME *a, LPFILETIME b) {
    *b = *a;
    return TRUE;
}
BOOL WINAPI FileTimeToSystemTime(const FILETIME *ft, LPSYSTEMTIME t) {
    // dias desde 1601-01-01 -> data civil (algoritmo de Howard Hinnant, a partir de 1970)
    uint64_t v = ((uint64_t)ft->dwHighDateTime << 32) | ft->dwLowDateTime;
    int64_t secs = (int64_t)(v / 10000000ULL) - 11644473600LL;
    int64_t days = secs / 86400, rem = secs % 86400;
    days += 719468;
    int64_t era = days / 146097, doe = days - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400, doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    int64_t d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
    memset(t, 0, sizeof(*t));
    t->wYear = (WORD)(y + (m <= 2)), t->wMonth = (WORD)m, t->wDay = (WORD)d;
    t->wHour = (WORD)(rem / 3600), t->wMinute = (WORD)(rem % 3600 / 60), t->wSecond = (WORD)(rem % 60);
    return TRUE;
}

// ---------------------------------------------------------------- variáveis e pastas

typedef struct {
    const char *name, *value;
} EnvVar;
static const EnvVar ENV[] = {
    {"ProgramFiles", "C:\\Program Files"}, {"ProgramFiles(x86)", "C:\\Program Files (x86)"}, {"SystemRoot", "C:\\Windows"},
    {"windir", "C:\\Windows"},             {"LOCALAPPDATA", "C:\\Users\\ana\\AppData\\Local"}, {"APPDATA", "C:\\Users\\ana\\AppData\\Roaming"},
    {"ProgramData", "C:\\ProgramData"},     {"USERPROFILE", "C:\\Users\\ana"},
};

DWORD WINAPI ExpandEnvironmentStringsW(LPCWSTR src, LPWSTR dst, DWORD n) {
    StrBuf b = {0};
    for (const WCHAR *p = src; *p; p++) {
        if (*p == '%') {
            const WCHAR *e = wfindc(p + 1, '%');
            if (e) {
                WCHAR *name = wdupn(p + 1, (size_t)(e - p - 1));
                const char *val = NULL;
                for (size_t i = 0; i < COUNT(ENV); i++) {
                    WCHAR *en = w_from_ascii(ENV[i].name);
                    if (wieq(en, name)) val = ENV[i].value;
                    xfree(en);
                }
                xfree(name);
                if (val) {
                    sb_add_ascii(&b, val);
                    p = e;
                    continue;
                }
            }
        }
        sb_addc(&b, *p);
    }
    DWORD need = (DWORD)b.len + 1;
    if (dst && n >= need) mem_copy(dst, sb_str(&b), need * sizeof(WCHAR));
    sb_free(&b);
    return need;
}

DWORD WINAPI GetEnvironmentVariableW(LPCWSTR n, LPWSTR b, DWORD s) {
    (void)n, (void)b, (void)s;
    return 0;
}

static UINT copy_out(const char *s, LPWSTR buf, UINT size) {
    size_t n = strlen(s);
    if (n + 1 > size) return (UINT)(n + 1);
    for (size_t i = 0; i <= n; i++) buf[i] = (unsigned char)s[i];
    return (UINT)n;
}
UINT WINAPI GetWindowsDirectoryW(LPWSTR b, UINT s) { return copy_out("C:\\Windows", b, s); }
UINT WINAPI GetSystemDirectoryW(LPWSTR b, UINT s) { return copy_out("C:\\Windows\\System32", b, s); }
DWORD WINAPI GetModuleFileNameW(HMODULE m, LPWSTR b, DWORD s) {
    (void)m;
    return copy_out("C:\\Tools\\extirpa.exe", b, (UINT)s);
}

static const struct {
    const GUID *id;
    const char *path;
} FOLDERS[] = {
    {&FOLDERID_ProgramFiles, "C:\\Program Files"},
    {&FOLDERID_ProgramFilesX86, "C:\\Program Files (x86)"},
    {&FOLDERID_ProgramFilesCommon, "C:\\Program Files\\Common Files"},
    {&FOLDERID_ProgramFilesCommonX86, "C:\\Program Files (x86)\\Common Files"},
    {&FOLDERID_ProgramData, "C:\\ProgramData"},
    {&FOLDERID_Profile, "C:\\Users\\ana"},
    {&FOLDERID_UserProfiles, "C:\\Users"},
    {&FOLDERID_Public, "C:\\Users\\Public"},
    {&FOLDERID_RoamingAppData, "C:\\Users\\ana\\AppData\\Roaming"},
    {&FOLDERID_LocalAppData, "C:\\Users\\ana\\AppData\\Local"},
    {&FOLDERID_LocalAppDataLow, "C:\\Users\\ana\\AppData\\LocalLow"},
    {&FOLDERID_Desktop, "C:\\Users\\ana\\Desktop"},
    {&FOLDERID_PublicDesktop, "C:\\Users\\Public\\Desktop"},
    {&FOLDERID_Documents, "C:\\Users\\ana\\Documents"},
    {&FOLDERID_PublicDocuments, "C:\\Users\\Public\\Documents"},
    {&FOLDERID_StartMenu, "C:\\Users\\ana\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu"},
    {&FOLDERID_CommonStartMenu, "C:\\ProgramData\\Microsoft\\Windows\\Start Menu"},
    {&FOLDERID_Programs, "C:\\Users\\ana\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs"},
    {&FOLDERID_CommonPrograms, "C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs"},
    {&FOLDERID_Startup, "C:\\Users\\ana\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\Startup"},
    {&FOLDERID_CommonStartup, "C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\Startup"},
    {&FOLDERID_Downloads, "C:\\Users\\ana\\Downloads"},
    {&FOLDERID_Music, "C:\\Users\\ana\\Music"},
    {&FOLDERID_Pictures, "C:\\Users\\ana\\Pictures"},
    {&FOLDERID_Videos, "C:\\Users\\ana\\Videos"},
    {&FOLDERID_SavedGames, "C:\\Users\\ana\\Saved Games"},
    {&FOLDERID_Windows, "C:\\Windows"},
    {&FOLDERID_System, "C:\\Windows\\System32"},
};

HRESULT WINAPI SHGetKnownFolderPath(REFKNOWNFOLDERID id, DWORD f, HANDLE t, PWSTR *out) {
    (void)f, (void)t;
    for (size_t i = 0; i < COUNT(FOLDERS); i++)
        if (memcmp(id, FOLDERS[i].id, sizeof(GUID)) == 0) {
            size_t n = strlen(FOLDERS[i].path);
            WCHAR *w = malloc((n + 1) * sizeof(WCHAR));
            for (size_t k = 0; k <= n; k++) w[k] = (unsigned char)FOLDERS[i].path[k];
            *out = w;
            return 0;
        }
    *out = NULL;
    return (HRESULT)0x80070002;
}

// ---------------------------------------------------------------- Registro

typedef struct FKey FKey;
typedef struct {
    WCHAR *name;
    DWORD type;
    uint8_t *data;
    DWORD size;
} FVal;
struct FKey {
    WCHAR *name;
    FKey *parent;
    PtrVec kids, vals;
};

static FKey *g_roots[4];  // HKLM, HKCU, HKCR, HKU

static FKey *new_key(const WCHAR *name, FKey *parent) {
    FKey *k = xalloc(sizeof(FKey));
    k->name = wdup(name);
    k->parent = parent;
    if (parent) pv_push(&parent->kids, k);
    return k;
}

static FKey *root_of(HKEY h) {
    if (h == HKEY_LOCAL_MACHINE) return g_roots[0];
    if (h == HKEY_CURRENT_USER) return g_roots[1];
    if (h == HKEY_CLASSES_ROOT) return g_roots[2];
    if (h == HKEY_USERS) return g_roots[3];
    return (FKey *)h;
}

static FKey *child(FKey *k, const WCHAR *name, size_t n) {
    for (size_t i = 0; i < k->kids.n; i++) {
        FKey *c = k->kids.items[i];
        if (wlen(c->name) == n && wieqn(c->name, name, n)) return c;
    }
    return NULL;
}

static FKey *walk(FKey *k, const WCHAR *sub, bool create) {
    const WCHAR *p = sub ? sub : u"";
    while (k && *p) {
        const WCHAR *e = p;
        while (*e && *e != '\\') e++;
        FKey *c = child(k, p, (size_t)(e - p));
        if (!c && create) {
            WCHAR *nm = wdupn(p, (size_t)(e - p));
            c = new_key(nm, k);
            xfree(nm);
        }
        k = c;
        p = *e ? e + 1 : e;
    }
    return k;
}

static FKey *key_from_path(const WCHAR *path, bool create) {
    static const WCHAR *const names[4] = {u"HKLM", u"HKCU", u"HKCR", u"HKU"};
    for (int i = 0; i < 4; i++) {
        size_t n = wlen(names[i]);
        if (wieqn(path, names[i], n) && (path[n] == 0 || path[n] == '\\')) return walk(g_roots[i], path[n] ? path + n + 1 : u"", create);
    }
    return NULL;
}

static FVal *find_val(FKey *k, const WCHAR *name) {
    for (size_t i = 0; i < k->vals.n; i++) {
        FVal *v = k->vals.items[i];
        if (wieq(v->name, name ? name : u"")) return v;
    }
    return NULL;
}

void fake_reset(void) {
    // vaza a memória da rodada anterior de propósito: handles antigos não podem apontar para lixo
    g_roots[0] = new_key(u"HKLM", NULL);
    g_roots[1] = new_key(u"HKCU", NULL);
    g_roots[2] = new_key(u"HKCR", NULL);
    g_roots[3] = new_key(u"HKU", NULL);
    fake_fs_reset();
}

void fake_key(const WCHAR *path) { key_from_path(path, true); }

void fake_val(const WCHAR *path, const WCHAR *name, DWORD type, const void *data, DWORD size) {
    FKey *k = key_from_path(path, true);
    FVal *v = find_val(k, name);
    if (!v) {
        v = xalloc(sizeof(FVal));
        v->name = wdup(name ? name : u"");
        pv_push(&k->vals, v);
    }
    xfree(v->data);
    v->type = type;
    v->size = size;
    v->data = xalloc(size + 2);
    if (size) memcpy(v->data, data, size);
}

void fake_str(const WCHAR *path, const WCHAR *name, const WCHAR *value) { fake_val(path, name, REG_SZ, value, (DWORD)((wlen(value) + 1) * sizeof(WCHAR))); }

void fake_dword(const WCHAR *path, const WCHAR *name, DWORD value) {
    uint8_t b[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
    fake_val(path, name, REG_DWORD, b, 4);
}

bool fake_key_exists(const WCHAR *path) { return key_from_path(path, false) != NULL; }

bool fake_val_exists(const WCHAR *path, const WCHAR *name) {
    FKey *k = key_from_path(path, false);
    return k && find_val(k, name);
}

LSTATUS WINAPI RegOpenKeyExW(HKEY h, LPCWSTR sub, DWORD o, REGSAM sam, PHKEY out) {
    (void)o, (void)sam;
    FKey *k = walk(root_of(h), sub, false);
    if (!k) return ERROR_FILE_NOT_FOUND;
    *out = (HKEY)k;
    return ERROR_SUCCESS;
}

LSTATUS WINAPI RegCloseKey(HKEY h) {
    (void)h;
    return ERROR_SUCCESS;
}

LSTATUS WINAPI RegCreateKeyExW(HKEY h, LPCWSTR sub, DWORD r, LPWSTR cls, DWORD opt, REGSAM sam, LPSECURITY_ATTRIBUTES sa, PHKEY out, LPDWORD disp) {
    (void)r, (void)cls, (void)opt, (void)sam, (void)sa;
    *out = (HKEY)walk(root_of(h), sub, true);
    if (disp) *disp = 1;
    return ERROR_SUCCESS;
}

LSTATUS WINAPI RegEnumKeyExW(HKEY h, DWORD i, LPWSTR name, LPDWORD n, LPDWORD r, LPWSTR c, LPDWORD cn, PFILETIME ft) {
    (void)r, (void)c, (void)cn;
    FKey *k = root_of(h);
    if (i >= k->kids.n) return ERROR_NO_MORE_ITEMS;
    FKey *kid = k->kids.items[i];
    size_t len = wlen(kid->name);
    if (len + 1 > *n) return ERROR_MORE_DATA;
    memcpy(name, kid->name, (len + 1) * sizeof(WCHAR));
    *n = (DWORD)len;
    if (ft) ft->dwLowDateTime = ft->dwHighDateTime = 0;
    return ERROR_SUCCESS;
}

LSTATUS WINAPI RegEnumValueW(HKEY h, DWORD i, LPWSTR name, LPDWORD nn, LPDWORD r, LPDWORD type, LPBYTE data, LPDWORD dn) {
    (void)r;
    FKey *k = root_of(h);
    if (i >= k->vals.n) return ERROR_NO_MORE_ITEMS;
    FVal *v = k->vals.items[i];
    size_t len = wlen(v->name);
    if (len + 1 > *nn) return ERROR_MORE_DATA;
    memcpy(name, v->name, (len + 1) * sizeof(WCHAR));
    *nn = (DWORD)len;
    if (type) *type = v->type;
    if (data) {
        if (!dn || *dn < v->size) {
            if (dn) *dn = v->size;
            return ERROR_MORE_DATA;
        }
        memcpy(data, v->data, v->size);
    }
    if (dn) *dn = v->size;
    return ERROR_SUCCESS;
}

LSTATUS WINAPI RegQueryValueExW(HKEY h, LPCWSTR name, LPDWORD r, LPDWORD type, LPBYTE data, LPDWORD sz) {
    (void)r;
    FVal *v = find_val(root_of(h), name);
    if (!v) return ERROR_FILE_NOT_FOUND;
    if (type) *type = v->type;
    if (data) {
        if (!sz || *sz < v->size) {
            if (sz) *sz = v->size;
            return ERROR_MORE_DATA;
        }
        memcpy(data, v->data, v->size);
    }
    if (sz) *sz = v->size;
    return ERROR_SUCCESS;
}

LSTATUS WINAPI RegQueryInfoKeyW(HKEY h, LPWSTR c, LPDWORD cn, LPDWORD r, LPDWORD subkeys, LPDWORD max_sub, LPDWORD max_cls, LPDWORD values, LPDWORD max_vname,
                                LPDWORD max_vdata, LPDWORD sd, PFILETIME ft) {
    (void)c, (void)cn, (void)r, (void)max_cls, (void)sd;
    FKey *k = root_of(h);
    DWORD ms = 0, mvn = 0, mvd = 0;
    for (size_t i = 0; i < k->kids.n; i++) ms = MAX(ms, (DWORD)wlen(((FKey *)k->kids.items[i])->name));
    for (size_t i = 0; i < k->vals.n; i++) {
        FVal *v = k->vals.items[i];
        mvn = MAX(mvn, (DWORD)wlen(v->name));
        mvd = MAX(mvd, v->size);
    }
    if (subkeys) *subkeys = (DWORD)k->kids.n;
    if (max_sub) *max_sub = ms;
    if (values) *values = (DWORD)k->vals.n;
    if (max_vname) *max_vname = mvn;
    if (max_vdata) *max_vdata = mvd;
    if (ft) ft->dwLowDateTime = 0x12345678, ft->dwHighDateTime = 0x01DD0000;  // uma data em 2026
    return ERROR_SUCCESS;
}

LSTATUS WINAPI RegSetValueExW(HKEY h, LPCWSTR name, DWORD r, DWORD type, const BYTE *data, DWORD size) {
    (void)r;
    FKey *k = root_of(h);
    FVal *v = find_val(k, name);
    if (!v) {
        v = xalloc(sizeof(FVal));
        v->name = wdup(name ? name : u"");
        pv_push(&k->vals, v);
    }
    v->type = type;
    v->size = size;
    v->data = xalloc(size + 2);
    memcpy(v->data, data, size);
    return ERROR_SUCCESS;
}

LSTATUS WINAPI RegDeleteValueW(HKEY h, LPCWSTR name) {
    FKey *k = root_of(h);
    for (size_t i = 0; i < k->vals.n; i++) {
        FVal *v = k->vals.items[i];
        if (wieq(v->name, name ? name : u"")) {
            k->vals.items[i] = k->vals.items[--k->vals.n];
            return ERROR_SUCCESS;
        }
    }
    return ERROR_FILE_NOT_FOUND;
}

static void detach(FKey *k) {
    FKey *p = k->parent;
    for (size_t i = 0; i < p->kids.n; i++)
        if (p->kids.items[i] == k) {
            for (size_t j = i + 1; j < p->kids.n; j++) p->kids.items[j - 1] = p->kids.items[j];
            p->kids.n--;
            return;
        }
}

LSTATUS WINAPI RegDeleteTreeW(HKEY h, LPCWSTR sub) {
    FKey *k = walk(root_of(h), sub, false);
    if (!k) return ERROR_FILE_NOT_FOUND;
    if (!sub || !*sub) {  // só o conteúdo
        k->kids.n = 0;
        k->vals.n = 0;
        return ERROR_SUCCESS;
    }
    detach(k);
    return ERROR_SUCCESS;
}

LSTATUS WINAPI RegDeleteKeyExW(HKEY h, LPCWSTR sub, REGSAM sam, DWORD r) {
    (void)sam, (void)r;
    FKey *k = walk(root_of(h), sub, false);
    if (!k) return ERROR_FILE_NOT_FOUND;
    if (k->kids.n) return ERROR_ACCESS_DENIED;
    detach(k);
    return ERROR_SUCCESS;
}

// serviços: um serviço é a chave em HKLM\SYSTEM\CurrentControlSet\Services
SC_HANDLE WINAPI OpenSCManagerW(LPCWSTR m, LPCWSTR d, DWORD a) {
    (void)m, (void)d, (void)a;
    return (SC_HANDLE)1;
}
SC_HANDLE WINAPI OpenServiceW(SC_HANDLE scm, LPCWSTR name, DWORD a) {
    (void)scm, (void)a;
    WCHAR *p = wcat(u"HKLM\\SYSTEM\\CurrentControlSet\\Services\\", name);
    FKey *k = key_from_path(p, false);
    xfree(p);
    if (!k) {
        g_last_error = ERROR_SERVICE_DOES_NOT_EXIST;
        return NULL;
    }
    return (SC_HANDLE)k;
}
BOOL WINAPI ControlService(SC_HANDLE s, DWORD c, LPSERVICE_STATUS st) {
    (void)s, (void)c;
    memset(st, 0, sizeof(*st));
    st->dwCurrentState = SERVICE_STOP_PENDING;
    return TRUE;
}
BOOL WINAPI QueryServiceStatus(SC_HANDLE s, LPSERVICE_STATUS st) {
    (void)s;
    memset(st, 0, sizeof(*st));
    st->dwCurrentState = SERVICE_STOPPED;
    return TRUE;
}
BOOL WINAPI DeleteService(SC_HANDLE s) {
    detach((FKey *)s);
    return TRUE;
}
BOOL WINAPI CloseServiceHandle(SC_HANDLE s) {
    (void)s;
    return TRUE;
}

// ---------------------------------------------------------------- disco

typedef struct FNode FNode;
struct FNode {
    WCHAR *name;
    bool dir;
    FNode *parent;
    PtrVec kids;
    uint8_t *data;
    size_t size;
    DWORD attrs;
};

static FNode *g_drive;
static PtrVec g_recycled, g_reboot;

void fake_fs_reset(void) {
    g_drive = xalloc(sizeof(FNode));
    g_drive->name = wdup(u"C:");
    g_drive->dir = true;
    mem_set(&g_recycled, 0, sizeof(g_recycled));
    mem_set(&g_reboot, 0, sizeof(g_reboot));
    fake_misc_reset();
}

static FNode *fchild(FNode *d, const WCHAR *name, size_t n) {
    for (size_t i = 0; i < d->kids.n; i++) {
        FNode *c = d->kids.items[i];
        if (wlen(c->name) == n && wieqn(c->name, name, n)) return c;
    }
    return NULL;
}

static FNode *fwalk(const WCHAR *path, bool create_dirs, bool last_is_file) {
    const WCHAR *p = path;
    if (wistarts(p, u"\\\\?\\")) p += 4;
    if (!(wfold(p[0]) == 'c' && p[1] == ':')) return NULL;
    p += 2;
    FNode *n = g_drive;
    while (*p == '\\') p++;
    while (n && *p) {
        const WCHAR *e = p;
        while (*e && *e != '\\') e++;
        FNode *c = fchild(n, p, (size_t)(e - p));
        if (!c && create_dirs) {
            c = xalloc(sizeof(FNode));
            c->name = wdupn(p, (size_t)(e - p));
            c->dir = !(last_is_file && !*e);
            c->parent = n;
            pv_push(&n->kids, c);
        }
        n = c;
        p = *e ? e + 1 : e;
        while (*p == '\\') p++;
    }
    return n;
}

void fake_dir(const WCHAR *path) { fwalk(path, true, false); }

void fake_file(const WCHAR *path, const void *data, size_t size) {
    FNode *n = fwalk(path, true, true);
    xfree(n->data);
    n->data = xalloc(size + 1);
    if (size) memcpy(n->data, data, size);
    n->size = size;
}

bool fake_exists(const WCHAR *path) { return fwalk(path, false, false) != NULL; }
size_t fake_recycled_count(void) { return g_recycled.n; }
const WCHAR *fake_recycled(size_t i) { return g_recycled.items[i]; }
bool fake_read(const WCHAR *path, uint8_t **data, size_t *len) {
    FNode *n = fwalk(path, false, false);
    if (!n || n->dir) return false;
    *data = n->data;
    *len = n->size;
    return true;
}
FNode *fake_find_child_named(const WCHAR *dir, const WCHAR *suffix, WCHAR **full) {
    FNode *d = fwalk(dir, false, false);
    for (size_t i = 0; d && i < d->kids.n; i++) {
        FNode *c = d->kids.items[i];
        if (wiends(c->name, suffix)) {
            *full = path_join(dir, c->name);
            return c;
        }
    }
    return NULL;
}

static void fdetach(FNode *n) {
    FNode *p = n->parent;
    for (size_t i = 0; i < p->kids.n; i++)
        if (p->kids.items[i] == n) {
            for (size_t j = i + 1; j < p->kids.n; j++) p->kids.items[j - 1] = p->kids.items[j];
            p->kids.n--;
            return;
        }
}

DWORD WINAPI GetFileAttributesW(LPCWSTR path) {
    FNode *n = fwalk(path, false, false);
    if (!n) {
        g_last_error = ERROR_FILE_NOT_FOUND;
        return INVALID_FILE_ATTRIBUTES;
    }
    return (n->dir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL) | n->attrs;
}

BOOL WINAPI GetFileAttributesExW(LPCWSTR path, GET_FILEEX_INFO_LEVELS l, LPVOID out) {
    (void)l;
    FNode *n = fwalk(path, false, false);
    if (!n) return FALSE;
    WIN32_FILE_ATTRIBUTE_DATA *d = out;
    memset(d, 0, sizeof(*d));
    d->dwFileAttributes = n->dir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    d->nFileSizeLow = (DWORD)(n->size & 0xFFFFFFFF);
    d->nFileSizeHigh = (DWORD)((uint64_t)n->size >> 32);
    return TRUE;
}

BOOL WINAPI SetFileAttributesW(LPCWSTR path, DWORD a) {
    FNode *n = fwalk(path, false, false);
    if (!n) return FALSE;
    n->attrs = a & FILE_ATTRIBUTE_READONLY;
    return TRUE;
}

BOOL WINAPI CreateDirectoryW(LPCWSTR path, LPSECURITY_ATTRIBUTES sa) {
    (void)sa;
    if (fwalk(path, false, false)) {
        g_last_error = ERROR_ALREADY_EXISTS;
        return FALSE;
    }
    WCHAR *parent = path_parent(path);
    bool ok = parent && fwalk(parent, false, false);
    xfree(parent);
    if (!ok) {
        g_last_error = ERROR_PATH_NOT_FOUND;
        return FALSE;
    }
    fwalk(path, true, false);
    return TRUE;
}

BOOL WINAPI RemoveDirectoryW(LPCWSTR path) {
    FNode *n = fwalk(path, false, false);
    if (!n || !n->dir) {
        g_last_error = ERROR_PATH_NOT_FOUND;
        return FALSE;
    }
    if (n->kids.n) {
        g_last_error = ERROR_DIR_NOT_EMPTY;
        return FALSE;
    }
    fdetach(n);
    return TRUE;
}

BOOL WINAPI DeleteFileW(LPCWSTR path) {
    FNode *n = fwalk(path, false, false);
    if (!n || n->dir) return FALSE;
    fdetach(n);
    return TRUE;
}

BOOL WINAPI MoveFileExW(LPCWSTR from, LPCWSTR to, DWORD flags) {
    if (!to && (flags & MOVEFILE_DELAY_UNTIL_REBOOT)) {
        pv_push(&g_reboot, wdup(from));
        return TRUE;
    }
    return FALSE;
}

int WINAPI SHFileOperationW(LPSHFILEOPSTRUCTW op) {
    if (op->wFunc != FO_DELETE) return 1;
    FNode *n = fwalk(op->pFrom, false, false);
    if (!n) return 2;
    fdetach(n);
    pv_push(&g_recycled, wdup(op->pFrom));
    return 0;
}

typedef struct {
    FNode *dir;
    size_t idx;
} FFind;

static void fill_find(FNode *n, LPWIN32_FIND_DATAW fd) {
    memset(fd, 0, sizeof(*fd));
    fd->dwFileAttributes = (n->dir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL) | n->attrs;
    fd->nFileSizeLow = (DWORD)(n->size & 0xFFFFFFFF);
    size_t len = MIN(wlen(n->name), (size_t)259);
    memcpy(fd->cFileName, n->name, len * sizeof(WCHAR));
}

HANDLE WINAPI FindFirstFileExW(LPCWSTR pattern, FINDEX_INFO_LEVELS l, LPVOID out, FINDEX_SEARCH_OPS op, LPVOID f, DWORD fl) {
    (void)l, (void)op, (void)f, (void)fl;
    WCHAR *dir = path_parent(pattern);  // o EXTIRPA sempre usa "<pasta>\*"
    FNode *d = dir ? fwalk(dir, false, false) : NULL;
    xfree(dir);
    if (!d || !d->dir || !d->kids.n) {
        g_last_error = d ? ERROR_FILE_NOT_FOUND : ERROR_PATH_NOT_FOUND;
        return INVALID_HANDLE_VALUE;
    }
    FFind *ff = xalloc(sizeof(FFind));
    ff->dir = d;
    fill_find(d->kids.items[0], out);
    ff->idx = 1;
    return ff;
}

BOOL WINAPI FindNextFileW(HANDLE h, LPWIN32_FIND_DATAW fd) {
    FFind *ff = h;
    if (ff->idx >= ff->dir->kids.n) {
        g_last_error = ERROR_NO_MORE_FILES;
        return FALSE;
    }
    fill_find(ff->dir->kids.items[ff->idx++], fd);
    return TRUE;
}

BOOL WINAPI FindClose(HANDLE h) {
    xfree(h);
    return TRUE;
}

typedef struct {
    FNode *n;
    size_t pos;
    bool append;
} FHandle;

HANDLE WINAPI CreateFileW(LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE t) {
    (void)share, (void)sa, (void)flags, (void)t;
    FNode *n = fwalk(path, false, false);
    if (disp == OPEN_EXISTING && !n) {
        g_last_error = ERROR_FILE_NOT_FOUND;
        return INVALID_HANDLE_VALUE;
    }
    if (!n) {
        WCHAR *parent = path_parent(path);
        bool ok = parent && fwalk(parent, false, false);
        xfree(parent);
        if (!ok) {
            g_last_error = ERROR_PATH_NOT_FOUND;
            return INVALID_HANDLE_VALUE;
        }
        n = fwalk(path, true, true);
    }
    if (n->dir) return INVALID_HANDLE_VALUE;
    if (disp == CREATE_ALWAYS) n->size = 0;
    FHandle *h = xalloc(sizeof(FHandle));
    h->n = n;
    h->append = (access & FILE_APPEND_DATA) && !(access & GENERIC_WRITE);
    return h;
}

BOOL WINAPI ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD got, LPOVERLAPPED o) {
    (void)o;
    FHandle *f = h;
    size_t left = f->n->size > f->pos ? f->n->size - f->pos : 0;
    size_t k = MIN(left, (size_t)n);
    memcpy(buf, f->n->data + f->pos, k);
    f->pos += k;
    *got = (DWORD)k;
    return TRUE;
}

BOOL WINAPI WriteFile(HANDLE h, LPCVOID buf, DWORD n, LPDWORD w, LPOVERLAPPED o) {
    (void)o;
    FHandle *f = h;
    if (f->append) f->pos = f->n->size;
    size_t end = f->pos + n;
    if (end > f->n->size) {
        f->n->data = xrealloc(f->n->data, end + 1);
        f->n->size = end;
    }
    memcpy(f->n->data + f->pos, buf, n);
    f->pos = end;
    *w = n;
    return TRUE;
}

BOOL WINAPI CloseHandle(HANDLE h) {
    if (h && h != INVALID_HANDLE_VALUE && (uintptr_t)h > 16) free(h);
    return TRUE;
}

// ---------------------------------------------------------------- o resto (não usado nos testes)

#define NOT_USED(ret, name, ...)                                     \
    ret WINAPI name(__VA_ARGS__) {                                   \
        fprintf(stderr, "fakewin: %s não deveria ser chamada\n", #name); \
        abort();                                                     \
    }

NOT_USED(BOOL, CreatePipe, PHANDLE a, PHANDLE b, LPSECURITY_ATTRIBUTES c, DWORD d)
NOT_USED(BOOL, SetHandleInformation, HANDLE a, DWORD b, DWORD c)
NOT_USED(BOOL, CreateProcessW, LPCWSTR a, LPWSTR b, LPSECURITY_ATTRIBUTES c, LPSECURITY_ATTRIBUTES d, BOOL e, DWORD f, LPVOID g, LPCWSTR h, LPSTARTUPINFOW i,
         LPPROCESS_INFORMATION j)
NOT_USED(HANDLE, CreateThread, LPSECURITY_ATTRIBUTES a, SIZE_T b, LPTHREAD_START_ROUTINE c, LPVOID d, DWORD e, LPDWORD f)
NOT_USED(DWORD, WaitForSingleObject, HANDLE a, DWORD b)
NOT_USED(BOOL, GetExitCodeProcess, HANDLE a, LPDWORD b)
NOT_USED(BOOL, TerminateProcess, HANDLE a, UINT b)
NOT_USED(HINSTANCE, ShellExecuteW, HWND a, LPCWSTR b, LPCWSTR c, LPCWSTR d, LPCWSTR e, INT f)
NOT_USED(HANDLE, CreateJobObjectW, LPSECURITY_ATTRIBUTES a, LPCWSTR b)
NOT_USED(HANDLE, CreateIoCompletionPort, HANDLE a, HANDLE b, ULONG_PTR c, DWORD d)
NOT_USED(BOOL, SetInformationJobObject, HANDLE a, JOBOBJECTINFOCLASS b, LPVOID c, DWORD d)
NOT_USED(BOOL, AssignProcessToJobObject, HANDLE a, HANDLE b)
NOT_USED(DWORD, ResumeThread, HANDLE a)
NOT_USED(BOOL, GetQueuedCompletionStatus, HANDLE a, LPDWORD b, PULONG_PTR c, LPOVERLAPPED *d, DWORD e)
NOT_USED(HMODULE, LoadLibraryExW, LPCWSTR a, HANDLE b, DWORD c)
NOT_USED(FARPROC, GetProcAddress, HMODULE a, LPCSTR b)
VOID WINAPI Sleep(DWORD ms) { (void)ms; }

// ---------------------------------------------------------------- processos, nomes curtos, programas externos

HANDLE WINAPI OpenProcess(DWORD a, BOOL b, DWORD pid) {
    (void)a, (void)b, (void)pid;
    return NULL;
}
BOOL WINAPI QueryFullProcessImageNameW(HANDLE h, DWORD f, LPWSTR b, PDWORD n) {
    (void)h, (void)f, (void)b, (void)n;
    return FALSE;
}
BOOL WINAPI K32EnumProcesses(DWORD *p, DWORD cb, LPDWORD got) {
    (void)p, (void)cb;
    *got = 0;
    return TRUE;
}
LRESULT WINAPI SendMessageTimeoutW(HWND h, UINT m, WPARAM w, LPARAM l, UINT f, UINT t, PDWORD_PTR r) {
    (void)h, (void)m, (void)w, (void)l, (void)f, (void)t;
    if (r) *r = 0;
    return 1;
}

static PtrVec g_short_long, g_short_short;
void fake_short_name(const WCHAR *long_path, const WCHAR *short_path) {
    pv_push(&g_short_long, wdup(long_path));
    pv_push(&g_short_short, wdup(short_path));
}
DWORD WINAPI GetShortPathNameW(LPCWSTR l, LPWSTR s, DWORD n) {
    for (size_t i = 0; i < g_short_long.n; i++)
        if (wieq(g_short_long.items[i], l) && fwalk(l, false, false)) {
            size_t len = wlen(g_short_short.items[i]);
            if (len + 1 > n) return (DWORD)(len + 1);
            memcpy(s, g_short_short.items[i], (len + 1) * sizeof(WCHAR));
            return (DWORD)len;
        }
    return 0;
}

// Programas que o EXTIRPA chama (schtasks e PowerShell), com o efeito que teriam.
static PtrVec g_commands;
size_t fake_command_count(void) { return g_commands.n; }
const WCHAR *fake_command(size_t i) { return g_commands.items[i]; }

static int b64v(WCHAR c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static bool ps_quote(WCHAR c) { return c == '\'' || (c >= 0x2018 && c <= 0x201B); }

int fake_run_capture(const WCHAR *cmdline, char **out, size_t *out_len) {
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    pv_push(&g_commands, wdup(cmdline));
    const WCHAR *tn = wifind(cmdline, u"/TN \"");
    if (wifind(cmdline, u"schtasks.exe") && wifind(cmdline, u"/Delete") && tn) {
        tn += 5;
        const WCHAR *e = wfindc(tn, '"');
        WCHAR *name = wdupn(tn, (size_t)(e - tn)), *file = wcat(u"C:\\Windows\\System32\\Tasks", name);
        FNode *n = fwalk(file, false, false);
        if (n) fdetach(n);
        xfree(name), xfree(file);
        return n ? 0 : 1;
    }
    const WCHAR *enc = wifind(cmdline, u"-EncodedCommand ");
    if (wifind(cmdline, u"powershell.exe") && enc) {
        // decodifica o script (UTF-16LE em base64) e aplica Remove-NetFirewallRule
        enc += 16;
        size_t n = wlen(enc), nb = 0;
        uint8_t *bytes = xalloc(n);
        uint32_t acc = 0;
        int bits = 0;
        for (size_t i = 0; i < n; i++) {
            int v = b64v(enc[i]);
            if (v < 0) continue;
            acc = (acc << 6) | (uint32_t)v;
            bits += 6;
            if (bits >= 8) bits -= 8, bytes[nb++] = (uint8_t)(acc >> bits);
        }
        WCHAR *script = wdupn((const WCHAR *)bytes, nb / 2);
        xfree(bytes);
        const WCHAR *ra = wifind(script, u"Remove-AppxPackage -Package '");
        if (ra) {
            // o pacote some do repositório de pacotes (e só ele: as chaves vizinhas ficam)
            ra += wlen(u"Remove-AppxPackage -Package '");
            const WCHAR *e = wfindc(ra, '\'');
            WCHAR *name = e ? wdupn(ra, (size_t)(e - ra)) : NULL;
            if (name) {
                HKEY k;
                if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, u"SOFTWARE\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\PackageRepository\\Packages", 0, 0, &k) == 0)
                    RegDeleteTreeW(k, name);
                xfree(name);
            }
        }
        if (wifind(script, u"Remove-NetFirewallRule")) {
            const WCHAR *p = wifind(script, u"$ids=@(");
            for (p = p ? wfindc(p, '\'') : NULL; p;) {
                StrBuf id = {0};
                const WCHAR *q = p + 1;
                for (; *q; q++) {
                    // como o PowerShell: ' e as aspas tipográficas U+2018 a U+201B fecham o literal;
                    // duas seguidas valem a segunda, literal
                    if (ps_quote(*q) && ps_quote(q[1])) sb_addc(&id, q[1]), q++;
                    else if (ps_quote(*q)) break;
                    else sb_addc(&id, *q);
                }
                HKEY k;
                if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, u"SYSTEM\\CurrentControlSet\\Services\\SharedAccess\\Parameters\\FirewallPolicy\\FirewallRules", 0, 0, &k) == 0)
                    RegDeleteValueW(k, sb_str(&id));
                sb_free(&id);
                if (!*q || q[1] != ',') break;
                p = q + 2;
            }
        }
        xfree(script);
        return 0;
    }
    return 1;
}

static void fake_misc_reset(void) {
    mem_set(&g_commands, 0, sizeof(g_commands));
    mem_set(&g_short_long, 0, sizeof(g_short_long));
    mem_set(&g_short_short, 0, sizeof(g_short_short));
}

// relógio de mentira: avança 1 ms por consulta
ULONGLONG GetTickCount64(void) {
    static ULONGLONG t;
    return ++t;
}
