// SPDX-License-Identifier: MIT
#include "match.h"

#include "pathx.h"
#include "protect.h"

// Palavras que aparecem em nomes de pastas e chaves de muitos programas. Sozinhas, não
// identificam ninguém; também nunca viram "palavra característica" de um programa.
static const char *const GENERIC[] = {
    "microsoft", "windows", "program", "programs", "programfiles", "files", "file", "common", "commonfiles", "shared", "software", "system", "system32",
    "update", "updates", "updater", "app", "apps", "application", "applications", "data", "appdata", "local", "locallow", "roaming", "user", "users",
    "temp", "tmp", "cache", "caches", "bin", "lib", "libs", "x64", "x86", "amd64", "arm64", "win32", "win64", "setup", "install", "installer",
    "installers", "uninstall", "uninstaller", "uninst", "driver", "drivers", "runtime", "runtimes", "redistributable", "service", "services",
    "tool", "tools", "plugin", "plugins", "addon", "addons", "extension", "extensions", "help", "docs", "documents", "documentation", "default",
    "version", "edition", "free", "package", "packages", "component", "components", "framework", "library", "sdk", "client", "server", "manager",
    "launcher", "player", "viewer", "studio", "desktop", "online", "cloud", "the", "and", "for", "with", "settings", "config", "configuration",
    "logs", "log", "backup", "backups", "profile", "profiles", "resources", "assets", "data", "start", "startmenu", "menu", "main", "core", "base",
    "home", "public", "global", "machine", "classes", "clsid", "policies", "intel", "nvidia", "amd", "google", "adobe", "apple", "oracle", "java",
    "net", "dotnet", "vc", "vcredist", "visual", "language", "languages", "lang", "locale", "locales", "en", "us", "pt", "br", "helper", "agent",
    "crashreports", "crashpad", "crashdumps", "dumps", "telemetry", "license", "licenses", "readme", "support", "wow6432node", "uninstallers",
    "unins000", "uninst000", "updaterservice", "maintenance", "maintenanceservice", "64bit", "32bit", "bit", "beta", "preview", "stable", "portable",
};

static const char *const PUB_SUFFIX[] = {"inc", "incorporated", "llc", "ltd", "limited", "corporation", "corp", "co", "company", "gmbh", "ag", "sa", "bv",
                                         "srl", "sl", "sas", "plc", "pty", "oy", "ab", "as", "kft", "sro", "spa", "ltda", "eireli", "me", "kk", "nv",
                                         "the", "llp", "lp", "sarl", "se", "pvt", "private"};

static const char *const ARCH[] = {"x64",   "x86",   "x86_64", "amd64", "arm64",   "ia64",  "64-bit", "32-bit",
                                   "64bit", "32bit", "win64",  "win32", "version", "build", "rev",    "release"};

static bool ascii_ieq(const WCHAR *w, const char *a) {
    for (; *w && *a; w++, a++)
        if (wfold(*w) != wfold((unsigned char)*a)) return false;
    return !*w && !*a;
}

bool is_generic_word(const WCHAR *norm) {
    size_t n = wlen(norm);
    if (n < 3) return true;
    bool digits = true;
    for (size_t i = 0; i < n; i++)
        if (!w_is_digit(norm[i])) digits = false;
    if (digits) return true;
    for (size_t i = 0; i < COUNT(GENERIC); i++)
        if (ascii_ieq(norm, GENERIC[i])) return true;
    return false;
}

WCHAR *norm_alnum(const WCHAR *s) {
    StrBuf b = {0};
    for (; s && *s; s++)
        if (w_is_alnum(*s)) sb_addc(&b, wfold(*s));
    return sb_take(&b);
}

static bool is_version_token(const WCHAR *t, size_t n) {
    size_t i = 0;
    if (n >= 2 && (t[0] == 'v' || t[0] == 'V') && w_is_digit(t[1])) i = 1;
    if (i >= n || !w_is_digit(t[i])) return false;
    bool all = true, dot = false;
    for (size_t k = i; k < n; k++) {
        WCHAR c = t[k];
        if (c == '.') dot = true;
        if (!(w_is_digit(c) || c == '.' || c == '_' || c == '-' || c == ',')) all = false;
    }
    return all || dot;  // "23.01", "2015-2022", "1.2.3-beta"
}

static bool is_lang_tag(const WCHAR *t, size_t n) {
    // "pt-BR", "en-US", "zh-Hans"
    if (n < 5 || n > 7 || t[2] != '-') return false;
    for (size_t i = 0; i < n; i++)
        if (i != 2 && !(((t[i] | 32) >= 'a') && ((t[i] | 32) <= 'z'))) return false;
    return true;
}

static bool is_arch_token(const WCHAR *t, size_t n) {
    if (n == 1 && (t[0] == '-' || t[0] == 0x2013 || t[0] == 0x2014)) return true;  // separadores soltos
    WCHAR *w = wdupn(t, n);
    bool r = false;
    for (size_t i = 0; !r && i < COUNT(ARCH); i++) r = ascii_ieq(w, ARCH[i]);
    xfree(w);
    return r;
}

WCHAR *clean_display_name(const WCHAR *display) {
    // 1. tira grupos entre parênteses e colchetes: "(x64 pt-BR)", "[64-bit]", "(remove only)"
    StrBuf b = {0};
    int depth = 0;
    for (const WCHAR *p = display ? display : u""; *p; p++) {
        if (*p == '(' || *p == '[') depth++;
        else if ((*p == ')' || *p == ']') && depth > 0) depth--;
        else if (!depth) sb_addc(&b, *p);
    }
    // 2. tira palavras de versão, arquitetura e idioma
    StrBuf out = {0};
    const WCHAR *s = sb_str(&b);
    while (*s) {
        while (*s && w_is_space(*s)) s++;
        const WCHAR *start = s;
        while (*s && !w_is_space(*s)) s++;
        size_t n = (size_t)(s - start);
        if (!n) break;
        if (is_version_token(start, n) || is_lang_tag(start, n) || is_arch_token(start, n)) continue;
        if (out.len) sb_addc(&out, ' ');
        sb_addn(&out, start, n);
    }
    sb_free(&b);
    // 3. pontuação solta no fim ("Foo -", "Bar,")
    while (out.len && (out.p[out.len - 1] == '-' || out.p[out.len - 1] == ',' || out.p[out.len - 1] == ':' || w_is_space(out.p[out.len - 1])))
        out.p[--out.len] = 0;
    if (!out.len) {  // o nome era só versão: fica o original
        sb_free(&out);
        return wtrim_dup(display);
    }
    return sb_take(&out);
}

WCHAR *clean_publisher(const WCHAR *publisher) {
    StrBuf out = {0};
    const WCHAR *s = publisher ? publisher : u"";
    while (*s) {
        while (*s && (w_is_space(*s) || *s == ',')) s++;
        const WCHAR *start = s;
        while (*s && !w_is_space(*s) && *s != ',') s++;
        size_t n = (size_t)(s - start);
        if (!n) break;
        WCHAR *word = wdupn(start, n), *nw = norm_alnum(word);
        bool suffix = false;
        for (size_t i = 0; i < COUNT(PUB_SUFFIX); i++)
            if (ascii_ieq(nw, PUB_SUFFIX[i])) suffix = true;
        if (!suffix && nw[0]) {
            if (out.len) sb_addc(&out, ' ');
            sb_add(&out, word);
        }
        xfree(word), xfree(nw);
    }
    while (out.len && (out.p[out.len - 1] == '.' || out.p[out.len - 1] == '-')) out.p[--out.len] = 0;
    return sb_take(&out);
}

static bool vec_has(const PtrVec *v, const WCHAR *s) {
    for (size_t i = 0; i < v->n; i++)
        if (wieq(v->items[i], s)) return true;
    return false;
}

void identity_add_exact(Identity *id, const WCHAR *name) {
    WCHAR *n = norm_alnum(name);
    if (!is_generic_word(n) && !vec_has(&id->exact, n)) pv_push(&id->exact, n);
    else xfree(n);
}

Identity *identity_new(const WCHAR *display_name, const WCHAR *publisher) {
    Identity *id = xalloc(sizeof(Identity));
    id->display = wdup(display_name);
    id->clean = clean_display_name(display_name);
    id->name_norm = norm_alnum(id->clean);
    id->publisher = clean_publisher(publisher);
    WCHAR *pn = norm_alnum(id->publisher);
    if (wlen(pn) >= 3 && !wistarts(pn, u"microsoft") && !wieq(pn, u"windows")) id->publisher_norm = pn;
    else xfree(pn);

    if (wlen(id->name_norm) >= 3) {
        identity_add_exact(id, id->clean);
        if (id->publisher_norm) {
            if (wistarts(id->name_norm, id->publisher_norm)) {
                id->pub_name_norm = wdup(id->name_norm);
                identity_add_exact(id, id->name_norm + wlen(id->publisher_norm));  // "Mozilla Firefox" -> "firefox"
            } else {
                id->pub_name_norm = wcat(id->publisher_norm, id->name_norm);
            }
        }
    }
    // palavras características
    const WCHAR *s = id->clean;
    while (*s) {
        while (*s && !w_is_alnum(*s)) s++;
        const WCHAR *start = s;
        while (*s && w_is_alnum(*s)) s++;
        if (s == start) break;
        WCHAR *w = wdupn(start, (size_t)(s - start)), *nw = norm_alnum(w);
        xfree(w);
        bool is_pub = id->publisher_norm && (wieq(nw, id->publisher_norm) || wifind(id->publisher_norm, nw));
        if (wlen(nw) >= 4 && !is_generic_word(nw) && !is_pub && !vec_has(&id->tokens, nw)) {
            pv_push(&id->tokens, nw);
            if (wlen(nw) >= 5 && (!id->primary || wlen(nw) > wlen(id->primary))) id->primary = nw;
        } else {
            xfree(nw);
        }
    }
    return id;
}

void identity_add_dir(Identity *id, const WCHAR *dir) {
    if (!dir || !*dir) return;
    WCHAR *d = path_norm(dir);
    if (wlen(d) < 4 || protect_dir_blocked(d) || vec_has(&id->dirs, d)) {
        xfree(d);
        return;
    }
    // uma pasta dentro de outra que já está na lista não acrescenta nada
    for (size_t i = 0; i < id->dirs.n; i++)
        if (path_is_under(d, id->dirs.items[i])) {
            xfree(d);
            return;
        }
    pv_push(&id->dirs, d);
    identity_add_exact(id, path_leaf(d));
}

void identity_add_exe(Identity *id, const WCHAR *exe_path) {
    if (!exe_path || !*exe_path) return;
    WCHAR *p = path_norm(exe_path);
    // "C:\x\app.exe,0" (ícone com índice)
    WCHAR *comma = (WCHAR *)wrfindc(p, ',');
    if (comma && wifind(p, u".exe") && comma > wifind(p, u".exe")) *comma = 0;
    WCHAR *parent = path_parent(p);
    if (parent && !protect_dir_blocked(parent)) {
        WCHAR *base = path_strip_ext(path_leaf(p)), *nb = norm_alnum(base);
        if (wlen(nb) >= 4 && !wistarts(nb, u"unins") && !wistarts(nb, u"setup")) identity_add_exact(id, base);
        xfree(base), xfree(nb);
    }
    xfree(parent), xfree(p);
}

void identity_free(Identity *id) {
    if (!id) return;
    xfree(id->display), xfree(id->clean), xfree(id->name_norm), xfree(id->publisher), xfree(id->publisher_norm), xfree(id->pub_name_norm);
    for (size_t i = 0; i < id->tokens.n; i++) xfree(id->tokens.items[i]);
    for (size_t i = 0; i < id->exact.n; i++) xfree(id->exact.items[i]);
    for (size_t i = 0; i < id->dirs.n; i++) xfree(id->dirs.items[i]);
    pv_free(&id->tokens), pv_free(&id->exact), pv_free(&id->dirs);
    xfree(id);
}

int match_name(const Identity *id, const WCHAR *candidate) {
    WCHAR *c = norm_alnum(candidate);
    int r = CONF_NONE;
    size_t lc = wlen(c);
    // o nome do editor sozinho é a pasta dele, não uma sobra do programa (ver match_publisher)
    if (is_generic_word(c) || (id->publisher_norm && wieq(c, id->publisher_norm))) goto done;
    if (vec_has(&id->exact, c) || (id->primary && wieq(c, id->primary)) || (id->pub_name_norm && wieq(c, id->pub_name_norm))) {
        r = CONF_HIGH;
        goto done;
    }
    if (wlen(id->name_norm) >= 5 && wistarts(c, id->name_norm)) r = CONF_MEDIUM;                 // "mozillafirefoxdata"
    else if (id->primary && wlen(id->primary) >= 6 && wifind(c, id->primary)) r = CONF_MEDIUM;  // "firefoxportable"
    else if (lc >= 6 && wifind(id->name_norm, c)) r = CONF_MEDIUM;                               // pedaço longo do nome
    if (r) goto done;
    for (size_t i = 0; i < id->tokens.n; i++) {
        const WCHAR *t = id->tokens.items[i];
        if (wlen(t) >= 5 && wifind(c, t)) {
            r = CONF_LOW;
            break;
        }
    }
done:
    xfree(c);
    return r;
}

bool match_publisher(const Identity *id, const WCHAR *candidate) {
    if (!id->publisher_norm) return false;
    WCHAR *c = norm_alnum(candidate);
    bool r = wieq(c, id->publisher_norm);
    if (!r) {
        WCHAR *raw = clean_publisher(candidate), *nr = norm_alnum(raw);
        r = wieq(nr, id->publisher_norm);
        xfree(raw), xfree(nr);
    }
    xfree(c);
    return r;
}

static bool boundary(WCHAR c) { return !c || c == '\\' || c == '/' || c == '"' || c == '\'' || c == ',' || c == ';' || c == '\t'; }

bool match_path_ref(const Identity *id, const WCHAR *text) {
    if (!text) return false;
    for (size_t i = 0; i < id->dirs.n; i++) {
        const WCHAR *d = id->dirs.items[i];
        size_t ld = wlen(d);
        for (const WCHAR *p = wifind(text, d); p; p = wifind(p + 1, d)) {
            bool before_ok = p == text || boundary(p[-1]) || p[-1] == ' ' || p[-1] == '=' || p[-1] == '@';
            if (before_ok && boundary(p[ld])) return true;
        }
    }
    return false;
}
