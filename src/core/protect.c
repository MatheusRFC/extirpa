// SPDX-License-Identifier: MIT
#include "protect.h"

#include "cmdline.h"
#include "pathx.h"

static PtrVec g_dirs, g_dir_trees;
static WCHAR *g_windows;  // pasta do Windows: exceções do Windows Installer e do Prefetch

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
    "%S\\Microsoft\\Windows\\CurrentVersion\\RunNotification",
    "%S\\Microsoft\\Windows\\CurrentVersion\\UFH",
    "%S\\Microsoft\\Windows\\CurrentVersion\\UFH\\SHC",
    "%S\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FeatureUsage",
    "%S\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FeatureUsage\\AppSwitched",
    "%S\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FeatureUsage\\AppLaunch",
    "%S\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FeatureUsage\\ShowJumpView",
    "%S\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FeatureUsage\\AppBadgeUpdated",
    "%S\\Microsoft\\Windows\\CurrentVersion\\ApplicationAssociationToasts",
    "%S\\Microsoft\\Tracing",
    "%S\\Microsoft\\RADAR",
    "%S\\Microsoft\\RADAR\\HeapLeakDetection",
    "%S\\Microsoft\\RADAR\\HeapLeakDetection\\DiagnosedApplications",
    "%S\\Microsoft\\DirectInput",
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
    "HKCU\\Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Compatibility Assistant\\Persisted",
    "HKCU\\Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers",
    "HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers",
};

static const char *const SOFTWARE_ROOTS[] = {"HKLM\\SOFTWARE", "HKLM\\SOFTWARE\\WOW6432Node", "HKCU\\Software", "HKCU\\Software\\WOW6432Node"};
static const char *const CLASSES_ROOTS[] = {"HKLM\\SOFTWARE\\Classes", "HKLM\\SOFTWARE\\WOW6432Node\\Classes", "HKCU\\Software\\Classes",
                                            "HKCU\\Software\\Classes\\WOW6432Node", "HKCR"};

void protect_add_dir(const WCHAR *path) { pv_push(&g_dirs, path_norm(path)); }
void protect_add_dir_tree(const WCHAR *path) { pv_push(&g_dir_trees, path_norm(path)); }

void protect_set_windows(const WCHAR *dir) {
    xfree(g_windows);
    g_windows = dir ? path_norm(dir) : NULL;
}

void protect_reset(void) {
    for (size_t i = 0; i < g_dirs.n; i++) xfree(g_dirs.items[i]);
    for (size_t i = 0; i < g_dir_trees.n; i++) xfree(g_dir_trees.items[i]);
    pv_free(&g_dirs);
    pv_free(&g_dir_trees);
}

static bool is_hex_n(const WCHAR *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        WCHAR c = s[i] | 32;
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

// Arquivos que o Windows guarda por programa dentro da pasta do Windows e que podem sair com
// ele: o cache do Windows Installer de um produto (C:\Windows\Installer\{código}, o
// SourceHash{código} e o pacote .msi/.msp em cache) e os arquivos do Prefetch
// (C:\Windows\Prefetch\PROGRAMA.EXE-1A2B3C4D.pf). Só esses nomes, só direto nessas pastas.
bool protect_windows_exception(const WCHAR *path) {
    if (!g_windows) return false;
    WCHAR *p = path_norm(path), *parent = path_parent(p);
    const WCHAR *leaf = path_leaf(p);
    bool ok = false;
    if (parent) {
        WCHAR *inst = path_join(g_windows, u"Installer"), *pf = path_join(g_windows, u"Prefetch");
        size_t n = wlen(leaf);
        if (path_eq(parent, inst)) {
            if (guid_valid(leaf)) ok = true;
            else if (wistarts(leaf, u"SourceHash") && guid_valid(leaf + 10)) ok = true;
            else if (n >= 5 && n <= 20 && (wiends(leaf, u".msi") || wiends(leaf, u".msp"))) {
                ok = true;
                for (size_t i = 0; i + 4 < n && ok; i++) ok = w_is_alnum(leaf[i]);
            }
        } else if (path_eq(parent, pf) && n > 16 && wiends(leaf, u".pf")) {
            // NOME.EXE-XXXXXXXX.pf
            ok = leaf[n - 12] == '-' && is_hex_n(leaf + n - 11, 8) && !wfindc(leaf, '\\');
        }
        xfree(inst), xfree(pf);
    }
    xfree(p), xfree(parent);
    return ok;
}

bool protect_dir_blocked(const WCHAR *path) {
    if (protect_windows_exception(path)) return false;
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

// Resto do caminho depois de base + '\', ou NULL.
static const WCHAR *after_base(const WCHAR *k, const char *base_ascii) {
    WCHAR *base = w_from_ascii(base_ascii);
    size_t lb = wlen(base);
    const WCHAR *r = wlen(k) > lb + 1 && wistarts(k, base) && k[lb] == '\\' ? k + lb + 1 : NULL;
    xfree(base);
    return r;
}

// O trecho até a próxima '\' é um código compactado do Windows Installer (32 hexadecimais)?
static bool packed_segment(const WCHAR *s) { return s && is_hex_n(s, 32) && (s[32] == 0 || s[32] == '\\'); }

// Registro de um produto do Windows Installer pelo código compactado: a chave do produto (e o
// que há dentro) nos lugares onde o Windows Installer registra produtos, recursos, códigos de
// atualização e componentes. As chaves que agrupam tudo continuam protegidas.
bool protect_msi_product_key(const WCHAR *key) {
    static const char *const BASES[] = {
        "HKLM\\SOFTWARE\\Classes\\Installer\\Products",
        "HKLM\\SOFTWARE\\Classes\\Installer\\Features",
        "HKLM\\SOFTWARE\\Classes\\Installer\\UpgradeCodes",
        "HKCU\\Software\\Classes\\Installer\\Products",
        "HKCU\\Software\\Classes\\Installer\\Features",
        "HKCU\\Software\\Classes\\Installer\\UpgradeCodes",
        "HKCU\\Software\\Microsoft\\Installer\\Products",
        "HKCU\\Software\\Microsoft\\Installer\\Features",
        "HKCU\\Software\\Microsoft\\Installer\\UpgradeCodes",
        "HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Installer\\UpgradeCodes",
    };
    WCHAR *k = path_norm(key);
    bool ok = false;
    for (size_t i = 0; i < COUNT(BASES) && !ok; i++) ok = packed_segment(after_base(k, BASES[i]));
    // UserData\<SID>\Products\<código> (com tudo dentro) e UserData\<SID>\Components\<componente>
    const WCHAR *r = ok ? NULL : after_base(k, "HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Installer\\UserData");
    if (r && wistarts(r, u"S-1-")) {
        const WCHAR *sl = wfindc(r, '\\');
        if (sl && wistarts(sl + 1, u"Products\\")) ok = packed_segment(sl + 10);
        else if (sl && wistarts(sl + 1, u"Components\\")) ok = packed_segment(sl + 12) && !sl[12 + 32];
    }
    xfree(k);
    return ok;
}

#define MSI_FOLDERS_KEY "HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Installer\\Folders"

bool protect_key_blocked(const WCHAR *key) {
    if (protect_msi_product_key(key)) return false;
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
    WCHAR *folders = w_from_ascii(MSI_FOLDERS_KEY);
    // pastas registradas pelo Windows Installer: um valor por pasta, e só valores saem
    bool msi = path_eq(k, folders) || protect_msi_product_key(k);
    xfree(folders);
    if (msi || protect_usage_key(k)) {
        xfree(k);
        return false;
    }
    bool blocked = !wfindc(k, '\\');
    for (size_t i = 0; !blocked && i < COUNT(KEY_TREES); i++) blocked = each_expansion(KEY_TREES[i], in_tree_strict, k);
    xfree(k);
    return blocked;
}
