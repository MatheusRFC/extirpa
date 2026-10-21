// SPDX-License-Identifier: MIT
#include "uninst.h"

#include "../core/cmdline.h"
#include "../core/pathx.h"
#include "platform.h"

// msiexec sempre da pasta do sistema, nunca o primeiro que aparecer no PATH
static WCHAR *fix_exe(const WCHAR *cmdline) {
    Cmd c;
    if (!cmd_split(cmdline, &c)) {
        cmd_free(&c);
        return wdup(cmdline);
    }
    WCHAR *base = path_strip_ext(path_leaf(c.exe));
    WCHAR *r;
    if (wieq(base, u"msiexec")) {
        WCHAR *m = system_exe(u"msiexec.exe");
        r = cmd_join(m, c.args);
        xfree(m);
    } else {
        r = cmd_join(c.exe, c.args);
    }
    xfree(base);
    cmd_free(&c);
    return r;
}

static bool has_bytes(const uint8_t *d, size_t n, const char *needle) {
    size_t m = 0;
    while (needle[m]) m++;
    for (size_t i = 0; m && i + m <= n; i++)
        if (d[i] == (uint8_t)needle[0] && mem_cmp(d + i, needle, m) == 0) return true;
    return false;
}

// Parâmetros silenciosos pelo tipo do desinstalador, olhando o próprio executável: o NSIS
// grava "Nullsoft.NSIS" no manifesto; o Inno Setup, "Inno Setup". NULL se não reconhecer.
static const WCHAR *silent_args_for(const WCHAR *exe) {
    if (file_size(exe) > 64u * 1024 * 1024) return NULL;
    uint8_t *d;
    size_t n;
    if (!file_read_all(exe, &d, &n)) return NULL;
    const WCHAR *r = NULL;
    if (has_bytes(d, n, "Nullsoft.NSIS") || has_bytes(d, n, "Nullsoft Install System")) r = u"/S";
    else if (has_bytes(d, n, "Inno Setup")) r = u"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART";
    xfree(d);
    return r;
}

WCHAR *uninstall_command(const Program *p, bool quiet, bool *is_quiet) {
    if (is_quiet) *is_quiet = false;
    WCHAR *un = p->uninstall ? expand_env(p->uninstall) : NULL;
    WCHAR *qu = p->quiet_uninstall ? expand_env(p->quiet_uninstall) : NULL;
    WCHAR *r = NULL;
    if (quiet && un) {
        WCHAR *q = cmd_quiet_variant(un, qu);
        if (q) {
            r = fix_exe(q);
            xfree(q);
            if (is_quiet) *is_quiet = true;
        } else {
            Cmd c;
            if (cmd_split(un, &c)) {
                const WCHAR *args = silent_args_for(c.exe);
                if (args && !wifind(c.args, args)) {
                    WCHAR *a = c.args[0] ? wcat3(c.args, u" ", args) : wdup(args);
                    r = cmd_join(c.exe, a);
                    xfree(a);
                    if (is_quiet) *is_quiet = true;
                }
            }
            cmd_free(&c);
        }
    }
    if (!r && un) {
        Cmd c;
        WCHAR g[39];
        if (cmd_split(un, &c)) {
            if (cmd_msi_product(&c, g)) {
                // MsiExec.exe /I{...} abre a tela de "modificar/reparar"; /X desinstala
                WCHAR *a = msi_uninstall_args(g, false), *m = system_exe(u"msiexec.exe");
                r = cmd_join(m, a);
                xfree(a), xfree(m);
            } else {
                r = fix_exe(un);
            }
        }
        cmd_free(&c);
    }
    if (!r && p->msi) {
        // registrado pelo Windows Installer sem UninstallString: a chave é o código do produto
        const WCHAR *leaf = path_leaf(p->key_path);
        if (guid_valid(leaf)) {
            WCHAR *a = msi_uninstall_args(leaf, quiet), *m = system_exe(u"msiexec.exe");
            r = cmd_join(m, a);
            xfree(a), xfree(m);
            if (quiet && is_quiet) *is_quiet = true;
        }
    }
    xfree(un), xfree(qu);
    return r;
}

void run_and_wait(const WCHAR *cmdline, HANDLE skip_event, RunResult *r) {
    mem_set(r, 0, sizeof(*r));
    log_fmt(u"Executando: %s", cmdline);
    HANDLE job = CreateJobObjectW(NULL, NULL);
    HANDLE port = job ? CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1) : NULL;
    if (job && port) {
        JOBOBJECT_ASSOCIATE_COMPLETION_PORT acp = {job, port};
        if (!SetInformationJobObject(job, JobObjectAssociateCompletionPortInformation, &acp, sizeof(acp))) {
            CloseHandle(port);
            port = NULL;
        }
    }

    // pasta de trabalho: a do desinstalador (alguns dependem disso)
    Cmd c;
    WCHAR *workdir = NULL;
    if (cmd_split(cmdline, &c)) {
        workdir = path_parent(c.exe);
        if (workdir && !dir_exists(workdir)) xfree(workdir), workdir = NULL;
    }
    cmd_free(&c);

    STARTUPINFOW si;
    mem_set(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    mem_set(&pi, 0, sizeof(pi));
    WCHAR *cmd = wdup(cmdline);
    BOOL ok = CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, NULL, workdir, &si, &pi);
    xfree(cmd), xfree(workdir);
    if (!ok) {
        r->error = GetLastError();
        log_fmt(u"  não iniciou (erro %u)", (unsigned)r->error);
        if (port) CloseHandle(port);
        if (job) CloseHandle(job);
        return;
    }
    r->started = true;
    bool in_job = job && port && AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);

    for (;;) {
        if (skip_event && WaitForSingleObject(skip_event, 0) == WAIT_OBJECT_0) {
            r->skipped = true;
            break;
        }
        if (in_job) {
            // o job avisa quando não sobra nenhum processo: o desinstalador e tudo o que ele abriu
            DWORD code = 0;
            ULONG_PTR key = 0;
            LPOVERLAPPED ov = NULL;
            if (GetQueuedCompletionStatus(port, &code, &key, &ov, 200) && key == (ULONG_PTR)job && code == JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO) break;
        } else if (WaitForSingleObject(pi.hProcess, 200) == WAIT_OBJECT_0) {
            break;
        }
    }
    DWORD code = STILL_ACTIVE;
    GetExitCodeProcess(pi.hProcess, &code);
    r->exit_code = code;
    log_fmt(u"  terminou: código %u%s", (unsigned)code, r->skipped ? u" (espera interrompida pelo usuário)" : u"");
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (port) CloseHandle(port);
    if (job) CloseHandle(job);  // fechar o job não encerra os processos
}

typedef BOOL(WINAPI *SRSetRestorePointW_t)(PRESTOREPOINTINFOW, PSTATEMGRSTATUS);

static SRSetRestorePointW_t sr_func(void) {
    static SRSetRestorePointW_t f;
    static bool tried;
    if (!tried) {
        tried = true;
        HMODULE m = LoadLibraryExW(u"srclient.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (m) f = (SRSetRestorePointW_t)(void *)GetProcAddress(m, "SRSetRestorePointW");
    }
    return f;
}

bool restore_point_begin(const WCHAR *description, INT64 *sequence, DWORD *error) {
    *sequence = 0;
    *error = 0;
    SRSetRestorePointW_t f = sr_func();
    if (!f) {
        *error = ERROR_SERVICE_DISABLED;
        return false;
    }
    RESTOREPOINTINFOW info;
    mem_set(&info, 0, sizeof(info));
    info.dwEventType = BEGIN_SYSTEM_CHANGE;
    info.dwRestorePtType = APPLICATION_UNINSTALL;
    size_t n = MIN(wlen(description), COUNT(info.szDescription) - 1);
    mem_copy(info.szDescription, description, n * sizeof(WCHAR));
    STATEMGRSTATUS st;
    mem_set(&st, 0, sizeof(st));
    if (!f(&info, &st)) {
        *error = st.nStatus ? st.nStatus : GetLastError();
        log_fmt(u"Ponto de restauração não criado (erro %u)", (unsigned)*error);
        return false;
    }
    *sequence = st.llSequenceNumber;
    log_fmt(u"Ponto de restauração iniciado: %s", description);
    return true;
}

void restore_point_end(INT64 sequence, bool cancelled) {
    SRSetRestorePointW_t f = sr_func();
    if (!f) return;
    RESTOREPOINTINFOW info;
    mem_set(&info, 0, sizeof(info));
    info.dwEventType = END_SYSTEM_CHANGE;
    info.dwRestorePtType = cancelled ? CANCELLED_OPERATION : APPLICATION_UNINSTALL;
    info.llSequenceNumber = sequence;
    STATEMGRSTATUS st;
    mem_set(&st, 0, sizeof(st));
    f(&info, &st);
}
