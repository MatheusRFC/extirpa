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

// Palavras de empresa que muitos editores tiram do nome da pasta ("Adobe Systems" -> "Adobe").
static const char *const COMPANY_WORDS[] = {"software", "systems", "system", "technologies", "technology", "tech", "studios", "studio", "games",
                                            "interactive", "labs", "lab", "group", "solutions", "digital", "media", "entertainment", "development",
                                            "developers", "international", "global", "networks", "computing", "corporation", "incorporated"};

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
    for (; s && *s; s++) {
        if (w_is_alnum(*s)) sb_addc(&b, wfold(*s));
        // "+" e "#" fazem parte do nome: o Notepad++ não pode virar o Bloco de Notas ("Notepad")
        else if (*s == '+') sb_add(&b, u"plus");
        else if (*s == '#') sb_add(&b, u"sharp");
    }
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
    if (id->publisher_norm) {
        // tira palavras de empresa do fim, uma de cada vez, enquanto sobrar um nome próprio
        WCHAR *words = wdup(id->publisher);
        for (;;) {
            WCHAR *sp = (WCHAR *)wrfindc(words, ' ');
            if (!sp) break;
            WCHAR *last = norm_alnum(sp + 1);
            bool company = false;
            for (size_t i = 0; i < COUNT(COMPANY_WORDS); i++)
                if (ascii_ieq(last, COMPANY_WORDS[i])) company = true;
            xfree(last);
            if (!company) break;
            *sp = 0;
        }
        WCHAR *ps = norm_alnum(words);
        xfree(words);
        // nomes de fabricantes conhecidos (adobe, intel...) servem; palavras de estrutura não
        static const char *const STRUCTURAL[] = {"program", "programs", "programfiles", "files", "common", "update", "data", "apps", "application",
                                                 "windows", "system32", "users", "public"};
        bool bad = wlen(ps) < 4 || wieq(ps, id->publisher_norm);
        for (size_t i = 0; !bad && i < COUNT(STRUCTURAL); i++) bad = ascii_ieq(ps, STRUCTURAL[i]);
        for (size_t i = 0; !bad && i < COUNT(COMPANY_WORDS); i++) bad = ascii_ieq(ps, COMPANY_WORDS[i]);
        if (!bad) id->publisher_short = ps;
        else xfree(ps);
    }

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

void identity_set_version(Identity *id, const WCHAR *version) {
    // "3.12.0" -> "312"; "131.0" -> "1310"; "2024" (sem ponto) -> nada
    if (!version || id->vtag) return;
    const WCHAR *p = version;
    while (*p && !w_is_digit(*p)) p++;
    StrBuf b = {0};
    int parts = 0;
    while (*p && parts < 2) {
        size_t start = b.len;
        while (w_is_digit(*p)) sb_addc(&b, *p++);
        if (b.len == start) break;
        parts++;
        if (*p != '.') break;
        p++;
    }
    if (parts == 2 && b.len >= 2) id->vtag = sb_take(&b);
    else sb_free(&b);
}

void identity_add_alt_dir(Identity *id, const WCHAR *dir) {
    if (!dir || !*dir) return;
    WCHAR *d = path_norm(dir);
    if (wlen(d) < 4 || vec_has(&id->dirs, d) || vec_has(&id->alt_dirs, d) || protect_dir_blocked(d)) {
        xfree(d);
        return;
    }
    pv_push(&id->alt_dirs, d);
}

void identity_add_dir(Identity *id, const WCHAR *dir, bool leaf_is_name) {
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
    if (leaf_is_name || match_name(id, path_leaf(d)) >= CONF_LOW) identity_add_exact(id, path_leaf(d));
}

void identity_add_exe_name(Identity *id, const WCHAR *file_name) {
    if (!file_name || wlen(file_name) < 5 || !wiends(file_name, u".exe") || wfindc(file_name, '\\')) return;
    WCHAR *l = wdup(file_name);
    for (WCHAR *c = l; *c; c++) *c = wfold(*c);
    if (!vec_has(&id->exes, l)) pv_push(&id->exes, l);
    else xfree(l);
}

void identity_add_exe(Identity *id, const WCHAR *exe_path) {
    if (!exe_path || !*exe_path) return;
    WCHAR *p = path_norm(exe_path);
    // "C:\x\app.exe,0" (ícone com índice)
    WCHAR *comma = (WCHAR *)wrfindc(p, ',');
    if (comma && wifind(p, u".exe") && comma > wifind(p, u".exe")) *comma = 0;
    identity_add_exe_name(id, path_leaf(p));
    WCHAR *parent = path_parent(p);
    if (parent && !protect_dir_blocked(parent)) {
        WCHAR *base = path_strip_ext(path_leaf(p)), *nb = norm_alnum(base);
        // nomes de 3 letras só quando o nome do programa começa com eles ("vlc" do VLC media player)
        bool longo = wlen(nb) >= 4 || (wlen(nb) == 3 && wistarts(id->name_norm, nb));
        if (longo && !wistarts(nb, u"unins") && !wistarts(nb, u"setup")) identity_add_exact(id, base);
        xfree(base), xfree(nb);
    }
    xfree(parent), xfree(p);
}

void identity_free(Identity *id) {
    if (!id) return;
    xfree(id->display), xfree(id->clean), xfree(id->name_norm), xfree(id->publisher), xfree(id->publisher_norm), xfree(id->publisher_short), xfree(id->pub_name_norm);
    for (size_t i = 0; i < id->tokens.n; i++) xfree(id->tokens.items[i]);
    for (size_t i = 0; i < id->exact.n; i++) xfree(id->exact.items[i]);
    for (size_t i = 0; i < id->dirs.n; i++) xfree(id->dirs.items[i]);
    for (size_t i = 0; i < id->alt_dirs.n; i++) xfree(id->alt_dirs.items[i]);
    for (size_t i = 0; i < id->exes.n; i++) xfree(id->exes.items[i]);
    pv_free(&id->tokens), pv_free(&id->exact), pv_free(&id->dirs), pv_free(&id->alt_dirs), pv_free(&id->exes);
    xfree(id->vtag);
    xfree(id);
}

int match_name(const Identity *id, const WCHAR *candidate) {
    WCHAR *c = norm_alnum(candidate);
    int r = CONF_NONE;
    size_t lc = wlen(c);
    // o nome do editor sozinho é a pasta dele, não uma sobra do programa (ver match_publisher)
    if (is_generic_word(c) || (id->publisher_norm && wieq(c, id->publisher_norm)) || (id->publisher_short && wieq(c, id->publisher_short))) goto done;
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

// O nome é um nome exato do programa seguido só de um número (versão da configuração),
// como "WinSCP 2" para o WinSCP 6.7. Para não pegar outra versão do mesmo programa
// ("IntelliJ IDEA 2023" desinstalando a 2024), o número tem de ser um dígito só ou o começo
// da versão do programa.
static bool versioned_of(const WCHAR *c, const WCHAR *base, const WCHAR *vtag) {
    size_t lb = wlen(base), lc = wlen(c);
    if (lb < 4 || lc <= lb || !wistarts(c, base)) return false;
    const WCHAR *suf = c + lb;
    for (const WCHAR *p = suf; *p; p++)
        if (*p < '0' || *p > '9') return false;
    return wlen(suf) == 1 || (vtag && wistarts(vtag, suf));
}

bool match_name_versioned(const Identity *id, const WCHAR *candidate) {
    WCHAR *c = norm_alnum(candidate);
    bool r = versioned_of(c, id->name_norm, id->vtag) || (id->primary && versioned_of(c, id->primary, id->vtag));
    for (size_t i = 0; !r && i < id->exact.n; i++) r = versioned_of(c, id->exact.items[i], id->vtag);
    xfree(c);
    return r;
}

bool match_publisher(const Identity *id, const WCHAR *candidate) {
    if (!id->publisher_norm) return false;
    WCHAR *c = norm_alnum(candidate);
    bool r = wieq(c, id->publisher_norm) || (id->publisher_short && wieq(c, id->publisher_short));
    if (!r) {
        WCHAR *raw = clean_publisher(candidate), *nr = norm_alnum(raw);
        r = wieq(nr, id->publisher_norm);
        xfree(raw), xfree(nr);
    }
    xfree(c);
    return r;
}

static bool boundary(WCHAR c) { return !c || c == '\\' || c == '/' || c == '"' || c == '\'' || c == ',' || c == ';' || c == '\t' || c == '|'; }

// A pasta d aparece no texto como caminho inteiro (não como pedaço de outro nome)?
static bool cites(const WCHAR *text, const WCHAR *d) {
    size_t ld = wlen(d);
    if (!ld) return false;
    for (const WCHAR *p = wifind(text, d); p; p = wifind(p + 1, d)) {
        bool before_ok = p == text || boundary(p[-1]) || p[-1] == ' ' || p[-1] == '=' || p[-1] == '@';
        if (before_ok && boundary(p[ld])) return true;
    }
    return false;
}

size_t path_ref_len(const PtrVec *dirs, const WCHAR *text) {
    size_t best = 0;
    if (!text) return 0;
    for (size_t i = 0; i < dirs->n; i++) {
        size_t ld = wlen(dirs->items[i]);
        if (ld > best && cites(text, dirs->items[i])) best = ld;
    }
    return best;
}

bool match_path_ref(const Identity *id, const WCHAR *text) { return path_ref_len(&id->dirs, text) > 0 || path_ref_len(&id->alt_dirs, text) > 0; }
