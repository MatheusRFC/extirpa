// SPDX-License-Identifier: MIT
#include "protect.h"

#include "pathx.h"

static PtrVec g_dirs, g_dir_trees;

// Chaves que nunca são apagadas nem servem de alvo (igual ou ancestral bloqueia).
// "%S" representa as quatro raízes de software: HKLM\SOFTWARE, HKLM\SOFTWARE\WOW6432Node,
// HKCU\Software e HKCU\Software\WOW6432Node. "%C" representa as raízes de classes.
static const char *const KEYS[] = {
    "HKLM",
    "HKCU",
    "HKU",
    "HKCR",
    "%S",
    "%S\\Classes",
    "%S\\Microsoft",
    "%S\\Microsoft\\Windows",
    "%S\\Microsoft\\Windows\\CurrentVersion",
    "%S\\Microsoft\\Windows\\CurrentVersion\\Run",
    "%S\\Microsoft\\Windows\\CurrentVersion\\RunOnce",
    "%S\\Microsoft\\Windows\\CurrentVersion\\RunOnceEx",
    "%S\\Microsoft\\Windows\\CurrentVersion\\RunServices",
    "%S\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
    "%S\\Microsoft\\Windows\\CurrentVersion\\App Paths",
    "%S\\Microsoft\\Windows\\CurrentVersion\\SharedDLLs",
    "%S\\Microsoft\\Windows\\CurrentVersion\\Explorer",
    "%S\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ShellIconOverlayIdentifiers",
    "%S\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Browser Helper Objects",
    "%S\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved",
    "%S\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run",
    "%S\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run32",
    "%S\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\StartupFolder",
    "%S\\Microsoft\\Windows\\CurrentVersion\\Explorer\\AutoplayHandlers",
    "%S\\Microsoft\\Windows\\CurrentVersion\\Explorer\\AutoplayHandlers\\Handlers",
    "%S\\Microsoft\\Windows\\CurrentVersion\\Explorer\\AutoplayHandlers\\EventHandlers",
    "%S\\Microsoft\\Windows\\CurrentVersion\\Search",
    "%S\\Microsoft\\Windows\\CurrentVersion\\Search\\JumplistData",
    "%S\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions",
    "%S\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved",
    "%S\\Microsoft\\Active Setup",
    "%S\\Microsoft\\Active Setup\\Installed Components",
    "%S\\Microsoft\\Internet Explorer",
    "%S\\Microsoft\\Office",
    "%S\\Clients",
    "%S\\RegisteredApplications",
    "%S\\Wow6432Node",
    "%C",
    "%C\\CLSID",
    "%C\\Interface",
    "%C\\TypeLib",
    "%C\\AppID",
    "%C\\*",
    "%C\\Directory",
    "%C\\Directory\\Background",
    "%C\\Directory\\shell",
    "%C\\Directory\\shellex",
    "%C\\Folder",
    "%C\\Folder\\shell",
    "%C\\Folder\\shellex",
    "%C\\Drive",
    "%C\\AllFilesystemObjects",
    "%C\\DesktopBackground",
    "%C\\Unknown",
    "%C\\exefile",
    "%C\\lnkfile",
    "%C\\batfile",
    "%C\\cmdfile",
    "%C\\comfile",
    "%C\\dllfile",
    "%C\\regfile",
    "%C\\txtfile",
    "%C\\htmlfile",
    "%C\\http",
    "%C\\https",
    "%C\\ftp",
    "%C\\mailto",
    "%C\\Applications",
    "%C\\SystemFileAssociations",
    "%C\\MIME",
    "%C\\MIME\\Database",
    "%C\\Protocols",
    "%C\\Protocols\\Handler",
    "%C\\Protocols\\Filter",
    "%C\\Wow6432Node",
    "%C\\Msi.Package",
};

// Árvores proibidas por inteiro: nem a chave nem nada dentro dela.
static const char *const KEY_TREES[] = {
    "HKLM\\SYSTEM",
    "HKLM\\SAM",
    "HKLM\\SECURITY",
    "HKLM\\HARDWARE",
    "HKLM\\BCD00000000",
    "HKLM\\COMPONENTS",
    "HKLM\\DRIVERS",
    "HKLM\\SCHEMA",
    "%S\\Policies",
    "%S\\Microsoft\\Windows NT",
    "%S\\Microsoft\\Cryptography",
    "%S\\Microsoft\\Windows Defender",
    "%S\\Microsoft\\Windows\\CurrentVersion\\Installer",
    "%S\\Microsoft\\Windows\\CurrentVersion\\Policies",
    "%S\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing",
    "%S\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate",
    "%S\\Microsoft\\Windows\\CurrentVersion\\SideBySide",
    "%S\\Microsoft\\Windows\\CurrentVersion\\Appx",
    "%S\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts",
    "%S\\Microsoft\\Windows\\CurrentVersion\\Explorer\\UserAssist",
    "%S\\Microsoft\\Installer",
    "%C\\Installer",
    "%C\\Local Settings",
};

// Chaves dentro de árvores proibidas onde o Windows guarda rastros de uso por programa:
// cada valor tem o caminho de um executável no nome. Aqui (e só aqui) valores podem sair;
// as chaves em si continuam intocáveis.
static const char *const USAGE_VALUE_KEYS[] = {
    "HKCU\\Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\Shell\\MuiCache",
    "HKCU\\Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Compatibility Assistant\\Store",
    "HKCU\\Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers",
    "HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers",
};

static const char *const SOFTWARE_ROOTS[] = {"HKLM\\SOFTWARE", "HKLM\\SOFTWARE\\WOW6432Node", "HKCU\\Software", "HKCU\\Software\\WOW6432Node"};
static const char *const CLASSES_ROOTS[] = {"HKLM\\SOFTWARE\\Classes", "HKLM\\SOFTWARE\\WOW6432Node\\Classes", "HKCU\\Software\\Classes",
                                            "HKCU\\Software\\Classes\\WOW6432Node", "HKCR"};

void protect_add_dir(const WCHAR *path) { pv_push(&g_dirs, path_norm(path)); }
void protect_add_dir_tree(const WCHAR *path) { pv_push(&g_dir_trees, path_norm(path)); }

void protect_reset(void) {
    for (size_t i = 0; i < g_dirs.n; i++) xfree(g_dirs.items[i]);
    for (size_t i = 0; i < g_dir_trees.n; i++) xfree(g_dir_trees.items[i]);
    pv_free(&g_dirs);
    pv_free(&g_dir_trees);
}

bool protect_dir_blocked(const WCHAR *path) {
    WCHAR *p = path_norm(path);
    bool blocked = false;
    size_t n = wlen(p);
    // raiz de unidade, caminho relativo, de rede ou vazio
    if (n < 4 || p[1] != ':' || p[2] != '\\' || (p[0] == '\\' && p[1] == '\\')) blocked = true;
    for (size_t i = 0; !blocked && i < g_dir_trees.n; i++)
        if (path_is_under_or_eq(p, g_dir_trees.items[i]) || path_is_under(g_dir_trees.items[i], p)) blocked = true;
    for (size_t i = 0; !blocked && i < g_dirs.n; i++)
        if (path_is_under_or_eq(g_dirs.items[i], p)) blocked = true;
    xfree(p);
    return blocked;
}

// Expande "%S..." e "%C..." para cada raiz e chama f; para no primeiro true.
static bool each_expansion(const char *pat, bool (*f)(const WCHAR *k, const WCHAR *target), const WCHAR *target) {
    const char *const *roots = NULL;
    size_t nroots = 0;
    if (pat[0] == '%' && pat[1] == 'S') roots = SOFTWARE_ROOTS, nroots = COUNT(SOFTWARE_ROOTS);
    if (pat[0] == '%' && pat[1] == 'C') roots = CLASSES_ROOTS, nroots = COUNT(CLASSES_ROOTS);
    if (!roots) {
        WCHAR *k = w_from_ascii(pat);
        bool r = f(k, target);
        xfree(k);
        return r;
    }
    for (size_t i = 0; i < nroots; i++) {
        WCHAR *a = w_from_ascii(roots[i]), *b = w_from_ascii(pat + 2);
        WCHAR *k = wcat(a, b);
        bool r = f(k, target);
        xfree(a), xfree(b), xfree(k);
        if (r) return true;
    }
    return false;
}

static bool eq_or_ancestor(const WCHAR *protected_key, const WCHAR *target) { return path_is_under_or_eq(protected_key, target); }
static bool in_tree(const WCHAR *tree, const WCHAR *target) { return path_is_under_or_eq(target, tree) || path_is_under(tree, target); }
static bool in_tree_strict(const WCHAR *tree, const WCHAR *target) { return path_is_under_or_eq(target, tree); }

bool protect_key_blocked(const WCHAR *key) {
    WCHAR *k = path_norm(key);
    bool blocked = !wfindc(k, '\\');  // uma raiz sozinha
    for (size_t i = 0; !blocked && i < COUNT(KEYS); i++) blocked = each_expansion(KEYS[i], eq_or_ancestor, k);
    for (size_t i = 0; !blocked && i < COUNT(KEY_TREES); i++) blocked = each_expansion(KEY_TREES[i], in_tree, k);
    // eventos do AutoPlay (EventHandlers\\<evento>): são do Windows; só os valores saem
    for (size_t i = 0; !blocked && i < COUNT(SOFTWARE_ROOTS); i++) {
        WCHAR *a = w_from_ascii(SOFTWARE_ROOTS[i]), *ev = wcat(a, u"\\Microsoft\\Windows\\CurrentVersion\\Explorer\\AutoplayHandlers\\EventHandlers");
        blocked = path_is_under(k, ev);
        xfree(a), xfree(ev);
    }
    // extensões de arquivo diretamente sob uma raiz de classes (".txt", ".pdf"...)
    if (!blocked && path_leaf(k)[0] == '.') {
        WCHAR *parent = path_parent(k);
        for (size_t i = 0; parent && !blocked && i < COUNT(CLASSES_ROOTS); i++) {
            WCHAR *r = w_from_ascii(CLASSES_ROOTS[i]);
            blocked = path_eq(parent, r);
            xfree(r);
        }
        xfree(parent);
    }
    xfree(k);
    return blocked;
}

// k está exatamente um nível abaixo de base e a folha dele é leaf (leaf NULL: qualquer)?
static bool child_of(const WCHAR *k, const char *base_ascii, const WCHAR *leaf, size_t levels) {
    WCHAR *base = w_from_ascii(base_ascii);
    bool r = false;
    size_t lb = wlen(base);
    if (wlen(k) > lb + 1 && wistarts(k, base) && k[lb] == '\\') {
        size_t seps = 0;
        for (const WCHAR *p = k + lb + 1; *p; p++) seps += *p == '\\';
        r = seps + 1 == levels && (!leaf || wieq(path_leaf(k), leaf));
    }
    xfree(base);
    return r;
}

bool protect_usage_key(const WCHAR *key) {
    for (size_t i = 0; i < COUNT(USAGE_VALUE_KEYS); i++) {
        WCHAR *u = w_from_ascii(USAGE_VALUE_KEYS[i]);
        bool eq = path_eq(key, u);
        xfree(u);
        if (eq) return true;
    }
    // UserAssist\{GUID}\Count (programas abertos, com o nome em ROT13) e
    // FileExts\.ext\OpenWithProgids (tipos de arquivo oferecidos em "Abrir com")
    return child_of(key, "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\UserAssist", u"Count", 2) ||
           child_of(key, "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts", u"OpenWithProgids", 2);
}

bool protect_value_blocked(const WCHAR *key) {
    WCHAR *k = path_norm(key);
    if (protect_usage_key(k)) {
        xfree(k);
        return false;
    }
    bool blocked = !wfindc(k, '\\');
    for (size_t i = 0; !blocked && i < COUNT(KEY_TREES); i++) blocked = each_expansion(KEY_TREES[i], in_tree_strict, k);
    xfree(k);
    return blocked;
}
