// SPDX-License-Identifier: MIT
#include "cmdline.h"

#include "pathx.h"

void cmd_free(Cmd *c) {
    xfree(c->exe), xfree(c->args);
    c->exe = c->args = NULL;
}

bool cmd_split(const WCHAR *cmdline, Cmd *out) {
    out->exe = out->args = NULL;
    if (!cmdline) return false;
    WCHAR *t = wtrim_dup(cmdline);
    const WCHAR *s = t, *exe_end = NULL, *rest = NULL;
    if (!*s) {
        xfree(t);
        return false;
    }
    if (*s == '"') {
        const WCHAR *q = wfindc(s + 1, '"');
        if (q) {
            out->exe = wdupn(s + 1, (size_t)(q - s - 1));
            rest = q + 1;
        } else {
            out->exe = wdup(s + 1);
            rest = s + wlen(s);
        }
    } else {
        // sem aspas: o executável termina na primeira extensão de programa seguida de
        // espaço (ou do fim), mesmo que o caminho tenha espaços antes disso
        static const WCHAR *const exts[] = {u".exe", u".com", u".bat", u".cmd"};
        for (size_t i = 0; i < COUNT(exts); i++) {
            size_t le = wlen(exts[i]);
            for (const WCHAR *p = wifind(s, exts[i]); p; p = wifind(p + 1, exts[i])) {
                WCHAR after = p[le];
                if (!after || w_is_space(after) || after == '"') {
                    if (!exe_end || p + le < exe_end) exe_end = p + le;
                    break;
                }
            }
        }
        if (!exe_end) {
            exe_end = s;
            while (*exe_end && !w_is_space(*exe_end)) exe_end++;
        }
        out->exe = wdupn(s, (size_t)(exe_end - s));
        rest = exe_end;
    }
    out->args = wtrim_dup(rest);
    xfree(t);
    return out->exe[0] != 0;
}

WCHAR *cmd_join(const WCHAR *exe, const WCHAR *args) {
    StrBuf b = {0};
    sb_addc(&b, '"');
    sb_add(&b, exe);
    sb_addc(&b, '"');
    if (args && *args) {
        sb_addc(&b, ' ');
        sb_add(&b, args);
    }
    return sb_take(&b);
}

static bool is_hex(WCHAR c) { return w_is_digit(c) || ((c | 32) >= 'a' && (c | 32) <= 'f'); }

bool guid_valid(const WCHAR *s) {
    if (!s || wlen(s) != 38 || s[0] != '{' || s[37] != '}') return false;
    for (int i = 1; i < 37; i++) {
        bool dash = i == 9 || i == 14 || i == 19 || i == 24;
        if (dash ? s[i] != '-' : !is_hex(s[i])) return false;
    }
    return true;
}

bool cmd_msi_product(const Cmd *c, WCHAR product[39]) {
    WCHAR *base = path_strip_ext(path_leaf(c->exe));
    bool msi = wieq(base, u"msiexec");
    xfree(base);
    if (!msi || !c->args) return false;
    // a opção /I, /X ou -x seguida (com ou sem espaço) do código do produto
    for (const WCHAR *p = c->args; *p; p++) {
        if ((*p == '/' || *p == '-') && (wfold(p[1]) == 'i' || wfold(p[1]) == 'x')) {
            const WCHAR *g = p + 2;
            while (*g && w_is_space(*g)) g++;
            if (*g == '{' && wlen(g) >= 38) {
                WCHAR tmp[39];
                mem_copy(tmp, g, 38 * sizeof(WCHAR));
                tmp[38] = 0;
                if (guid_valid(tmp)) {
                    mem_copy(product, tmp, sizeof(tmp));
                    return true;
                }
            }
        }
    }
    return false;
}

static bool is_inno(const WCHAR *exe) {
    const WCHAR *leaf = path_leaf(exe);
    // unins000.exe, unins001.exe...
    return wlen(leaf) == 12 && wieqn(leaf, u"unins", 5) && w_is_digit(leaf[5]) && w_is_digit(leaf[6]) && w_is_digit(leaf[7]) && wieq(leaf + 8, u".exe");
}

UninstKind cmd_kind(const Cmd *c) {
    WCHAR g[39];
    if (cmd_msi_product(c, g)) return UNINST_MSI;
    if (is_inno(c->exe)) return UNINST_INNO;
    return UNINST_UNKNOWN;
}

WCHAR *msi_uninstall_args(const WCHAR *product, bool quiet) {
    StrBuf b = {0};
    sb_add(&b, u"/X");
    sb_add(&b, product);
    if (quiet) sb_add(&b, u" /passive /norestart");
    return sb_take(&b);
}

WCHAR *cmd_quiet_variant(const WCHAR *uninstall, const WCHAR *quiet_uninstall) {
    if (quiet_uninstall && *quiet_uninstall) return wtrim_dup(quiet_uninstall);
    Cmd c;
    if (!cmd_split(uninstall, &c)) return NULL;
    WCHAR *r = NULL;
    WCHAR g[39];
    if (cmd_msi_product(&c, g)) {
        WCHAR *a = msi_uninstall_args(g, true);
        r = cmd_join(u"msiexec.exe", a);
        xfree(a);
    } else if (is_inno(c.exe)) {
        WCHAR *a = wcat(c.args, *c.args ? u" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART" : u"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART");
        r = cmd_join(c.exe, a);
        xfree(a);
    }
    cmd_free(&c);
    return r;
}
