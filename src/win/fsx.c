// SPDX-License-Identifier: MIT
#include "fsx.h"

#include "../core/pathx.h"

// Caminhos longos (> MAX_PATH) só funcionam na API com o prefixo \\?\.
static WCHAR *api_path(const WCHAR *p) {
    if (wlen(p) >= 240 && !(p[0] == '\\' && p[1] == '\\')) return wcat(u"\\\\?\\", p);
    return wdup(p);
}

PtrVec fs_list(const WCHAR *dir) {
    PtrVec v = {0};
    WCHAR *pat = path_join(dir, u"*"), *ap = api_path(pat);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(ap, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    xfree(pat), xfree(ap);
    if (h == INVALID_HANDLE_VALUE) return v;
    do {
        if (fd.cFileName[0] == '.' && (fd.cFileName[1] == 0 || (fd.cFileName[1] == '.' && fd.cFileName[2] == 0))) continue;
        DirEntry *e = xalloc(sizeof(DirEntry));
        e->name = wdup(fd.cFileName);
        e->is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e->is_link = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        e->size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        e->mtime = fd.ftLastWriteTime;
        pv_push(&v, e);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return v;
}

void fs_list_free(PtrVec *v) {
    for (size_t i = 0; i < v->n; i++) {
        DirEntry *e = v->items[i];
        xfree(e->name), xfree(e);
    }
    pv_free(v);
}

static uint64_t size_rec(const WCHAR *dir, size_t *budget, int depth) {
    uint64_t total = 0;
    if (depth > 64 || !*budget) return 0;
    PtrVec v = fs_list(dir);
    for (size_t i = 0; i < v.n && *budget; i++) {
        DirEntry *e = v.items[i];
        if (e->is_dir) {
            if (e->is_link) continue;
            WCHAR *sub = path_join(dir, e->name);
            total += size_rec(sub, budget, depth + 1);
            xfree(sub);
        } else {
            total += e->size;
            (*budget)--;
        }
    }
    fs_list_free(&v);
    return total;
}

uint64_t fs_dir_size(const WCHAR *dir, size_t max_files) {
    size_t budget = max_files;
    return size_rec(dir, &budget, 0);
}

bool fs_dir_empty(const WCHAR *dir) {
    PtrVec v = fs_list(dir);
    bool empty = v.n == 0;
    fs_list_free(&v);
    return empty;
}

DWORD fs_recycle(const WCHAR *path) {
    size_t n = wlen(path);
    WCHAR *from = xalloc((n + 2) * sizeof(WCHAR));  // lista terminada por dois zeros
    mem_copy(from, path, n * sizeof(WCHAR));
    SHFILEOPSTRUCTW op;
    mem_set(&op, 0, sizeof(op));
    op.wFunc = FO_DELETE;
    op.pFrom = from;
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    int r = SHFileOperationW(&op);
    xfree(from);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return ERROR_SUCCESS;
    if (op.fAnyOperationsAborted) return ERROR_CANCELLED;
    return r ? (DWORD)r : ERROR_ACCESS_DENIED;
}

static bool reboot_rec(const WCHAR *path, int depth) {
    DWORD a = GetFileAttributesW(path);
    if (a == INVALID_FILE_ATTRIBUTES) return true;
    bool ok = true;
    if ((a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT) && depth < 64) {
        PtrVec v = fs_list(path);
        for (size_t i = 0; i < v.n; i++) {
            DirEntry *e = v.items[i];
            WCHAR *sub = path_join(path, e->name);
            ok = reboot_rec(sub, depth + 1) && ok;
            xfree(sub);
        }
        fs_list_free(&v);
    }
    if (a & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(path, a & ~(DWORD)FILE_ATTRIBUTE_READONLY);
    WCHAR *ap = api_path(path);
    ok = MoveFileExW(ap, NULL, MOVEFILE_DELAY_UNTIL_REBOOT) && ok;
    xfree(ap);
    return ok;
}

bool fs_delete_on_reboot(const WCHAR *path) { return reboot_rec(path, 0); }
