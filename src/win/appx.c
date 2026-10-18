// SPDX-License-Identifier: MIT
#include "appx.h"

#include "../core/fmt.h"
#include "../core/pathx.h"
#include "platform.h"

static WCHAR *powershell_cmd(const WCHAR *script) {
    WCHAR *b64 = base64_utf16le(script);
    WCHAR *ps = g_paths.system32 ? path_join(g_paths.system32, u"WindowsPowerShell\\v1.0\\powershell.exe") : wdup(u"powershell.exe");
    WCHAR *cmd = wfmt(u"\"%s\" -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand %s", ps, b64);
    xfree(b64), xfree(ps);
    return cmd;
}

static WCHAR *field(const WCHAR **p) {
    const WCHAR *s = *p, *e = s;
    while (*e && *e != '\t') e++;
    WCHAR *r = wdupn(s, (size_t)(e - s));
    *p = *e ? e + 1 : e;
    return r;
}

// "CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond..." -> "Microsoft Corporation"
static WCHAR *publisher_name(const WCHAR *dn) {
    static const WCHAR *const keys[] = {u"O=", u"CN="};
    for (size_t k = 0; k < COUNT(keys); k++) {
        for (const WCHAR *p = dn; p && *p; p++) {
            if ((p == dn || p[-1] == ' ' || p[-1] == ',') && wistarts(p, keys[k])) {
                const WCHAR *s = p + wlen(keys[k]), *e = s;
                while (*e && *e != ',') e++;
                if (e > s) return wdupn(s, (size_t)(e - s));
            }
        }
    }
    return wdup(dn);
}

PtrVec appx_list(WCHAR **error_text) {
    PtrVec v = {0};
    *error_text = NULL;
    WCHAR *cmd = powershell_cmd(u"$ErrorActionPreference='Stop'\n"
                                u"[Console]::OutputEncoding=[Text.Encoding]::UTF8\n"
                                u"$t=[char]9\n"
                                u"Get-AppxPackage -PackageTypeFilter Main | ForEach-Object { "
                                u"$_.Name+$t+$_.Version+$t+$_.Publisher+$t+$_.PackageFullName+$t+$_.InstallLocation+$t+$_.SignatureKind+$t+$_.NonRemovable }\n");
    char *out = NULL;
    size_t len = 0;
    int code = run_capture(cmd, &out, &len, 120000);
    xfree(cmd);
    if (code != 0) {
        *error_text = out ? utf8_to_w(out, len, NULL) : wdup(u"PowerShell");
        xfree(out);
        return v;
    }
    WCHAR *w = utf8_to_w(out, len, NULL);
    xfree(out);
    for (const WCHAR *p = w; *p;) {
        const WCHAR *eol = p;
        while (*eol && *eol != '\n') eol++;
        WCHAR *line = wdupn(p, (size_t)(eol - p));
        size_t n = wlen(line);
        if (n && line[n - 1] == '\r') line[n - 1] = 0;
        const WCHAR *q = line;
        if (wfindc(line, '\t')) {
            AppxPackage *a = xalloc(sizeof(AppxPackage));
            a->name = field(&q);
            a->version = field(&q);
            WCHAR *pub = field(&q);
            a->publisher = publisher_name(pub);
            xfree(pub);
            a->full_name = field(&q);
            a->location = field(&q);
            WCHAR *sig = field(&q), *nonrem = field(&q);
            a->removable = !wieq(sig, u"System") && !wieq(nonrem, u"True");
            xfree(sig), xfree(nonrem);
            if (a->full_name[0]) pv_push(&v, a);
            else xfree(a->name), xfree(a->version), xfree(a->publisher), xfree(a->full_name), xfree(a->location), xfree(a);
        }
        xfree(line);
        p = *eol ? eol + 1 : eol;
    }
    xfree(w);
    return v;
}

void appx_list_free(PtrVec *v) {
    for (size_t i = 0; i < v->n; i++) {
        AppxPackage *a = v->items[i];
        xfree(a->name), xfree(a->version), xfree(a->publisher), xfree(a->full_name), xfree(a->location), xfree(a);
    }
    pv_free(v);
}

bool appx_valid_name(const WCHAR *full_name) {
    if (!full_name || !full_name[0] || wlen(full_name) > 255) return false;
    for (const WCHAR *p = full_name; *p; p++) {
        WCHAR c = *p;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' || c == '~';
        if (!ok) return false;
    }
    return true;
}

WCHAR *appx_remove(const WCHAR *full_name, bool all_users) {
    // o nome entra no script entre aspas simples: só aceita os caracteres de um nome de pacote
    if (!appx_valid_name(full_name)) return wdup(u"invalid package name");
    WCHAR *script = wfmt(u"$ErrorActionPreference='Stop'\n"
                         u"[Console]::OutputEncoding=[Text.Encoding]::UTF8\n"
                         u"try { Remove-AppxPackage -Package '%s'%s; exit 0 } catch { Write-Output $_.Exception.Message; exit 1 }\n",
                         full_name, all_users ? u" -AllUsers" : u"");
    WCHAR *cmd = powershell_cmd(script);
    xfree(script);
    log_fmt(u"Removendo aplicativo do Windows: %s%s", full_name, all_users ? u" (todos os usuários)" : u"");
    char *out = NULL;
    size_t len = 0;
    int code = run_capture(cmd, &out, &len, 600000);
    xfree(cmd);
    WCHAR *r = NULL;
    if (code != 0) {
        r = out && len ? utf8_to_w(out, len, NULL) : wfmt(u"PowerShell: %d", code);
        WCHAR *t = wtrim_dup(r);
        xfree(r);
        r = t;
        log_fmt(u"  falhou: %s", r);
    } else {
        log_text(u"  removido");
    }
    xfree(out);
    return r;
}
