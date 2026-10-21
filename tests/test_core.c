// SPDX-License-Identifier: MIT
// Testes do núcleo (src/core), rodam no Linux ou no Windows com qualquer compilador C:
//     python3 tools/build.py test
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/cmdline.h"
#include "../src/core/fmt.h"
#include "../src/core/leftover.h"
#include "../src/core/lnk.h"
#include "../src/core/match.h"
#include "../src/core/pathx.h"
#include "../src/core/protect.h"
#include "../src/core/regtext.h"
#include "../src/core/trace.h"
#include "../src/core/wstr.h"

// Memória da plataforma de teste
void *xalloc(size_t n) {
    void *p = calloc(1, n ? n : 1);
    if (!p) abort();
    return p;
}
void *xrealloc(void *p, size_t n) {
    void *r = realloc(p, n ? n : 1);
    if (!r) abort();
    return r;
}
void xfree(void *p) { free(p); }

static int g_fail, g_checks;

static char *u8(const WCHAR *s) {
    if (!s) return w_to_utf8(u"(null)", 6, NULL);
    size_t n;
    return w_to_utf8(s, wlen(s), &n);
}

#define CHECK(cond)                                                     \
    do {                                                                \
        g_checks++;                                                     \
        if (!(cond)) {                                                  \
            g_fail++;                                                   \
            fprintf(stderr, "%s:%d: falhou: %s\n", __FILE__, __LINE__, #cond); \
        }                                                               \
    } while (0)

#define CHECK_WEQ(got, want)                                                                         \
    do {                                                                                             \
        g_checks++;                                                                                  \
        const WCHAR *g_ = (got), *w_ = (want);                                                       \
        if (!g_ || !w_ || !weq(g_, w_)) {                                                            \
            char *a_ = u8(g_), *b_ = u8(w_);                                                         \
            g_fail++;                                                                                \
            fprintf(stderr, "%s:%d: esperado [%s], veio [%s]\n", __FILE__, __LINE__, b_, a_);        \
            xfree(a_), xfree(b_);                                                                      \
        }                                                                                            \
    } while (0)

#define CHECK_WEQ_FREE(expr, want)  \
    do {                            \
        WCHAR *t_ = (expr);         \
        CHECK_WEQ(t_, want);        \
        xfree(t_);                  \
    } while (0)

// ---------------------------------------------------------------- strings

static void test_wstr(void) {
    CHECK(wlen(u"abc") == 3);
    CHECK(wlen(NULL) == 0);
    CHECK(wieq(u"ÁRVORE Ção", u"árvore ção"));
    CHECK(!wieq(u"abc", u"abd"));
    CHECK(wieq(u"ПРИВЕТ", u"привет"));
    CHECK(wistarts(u"HKEY_Local", u"hkey_"));
    CHECK(wiends(u"setup.EXE", u".exe"));
    CHECK(wifind(u"C:\\Program Files\\Foo", u"files\\f") != NULL);
    CHECK(wifind(u"abc", u"x") == NULL);
    CHECK(wicmp(u"a", u"B") < 0);

    uint64_t v;
    CHECK(w_to_u64(u"18446744073709551615", &v) && v == UINT64_MAX);
    CHECK(!w_to_u64(u"18446744073709551616", &v));
    CHECK(!w_to_u64(u"12a", &v));
    CHECK(!w_to_u64(u"", &v));

    StrBuf b = {0};
    sb_add(&b, u"x=");
    sb_add_u64(&b, 1234567890123ULL);
    sb_addc(&b, ' ');
    sb_add_hex(&b, 0xBEEF, 8);
    sb_addc(&b, ' ');
    sb_add_u64_pad(&b, 7, 3);
    sb_add_ascii(&b, " ok");
    CHECK_WEQ(sb_str(&b), u"x=1234567890123 0000beef 007 ok");
    sb_free(&b);

    // UTF-8 ida e volta, com acento, CJK e emoji (par substituto)
    const WCHAR *orig = u"ação 中文 \U0001F600 fim";
    size_t n8, n16;
    char *s8 = w_to_utf8(orig, wlen(orig), &n8);
    CHECK(strcmp(s8, "ação 中文 \xF0\x9F\x98\x80 fim") == 0);
    WCHAR *back = utf8_to_w(s8, n8, &n16);
    CHECK_WEQ(back, orig);
    CHECK(n16 == wlen(orig));
    xfree(s8), xfree(back);
    // bytes inválidos viram U+FFFD
    WCHAR *bad = utf8_to_w("a\xC3(b\xE2\x82", 7, NULL);
    CHECK_WEQ(bad, u"a\xFFFD(b\xFFFD\xFFFD");
    xfree(bad);
    // sobrelonga (overlong) e substituto codificado
    WCHAR *ov = utf8_to_w("\xC0\xAF\xED\xA0\x80", 5, NULL);
    CHECK(ov[0] == 0xFFFD);
    xfree(ov);

    CHECK_WEQ_FREE(wtrim_dup(u"  \t abc \r\n"), u"abc");
    CHECK_WEQ_FREE(wcat3(u"a", u"b", u"c"), u"abc");
}

static int cmp_int(const void *a, const void *b, void *ctx) {
    (void)ctx;
    return (int)((const int *)a)[0] - (int)((const int *)b)[0];
}

static void test_sort(void) {
    // estável: pares (chave, ordem original)
    static int data[200][2];
    PtrVec v = {0};
    for (int i = 0; i < 200; i++) {
        data[i][0] = (i * 7919) % 13;
        data[i][1] = i;
        pv_push(&v, data[i]);
    }
    pv_sort(&v, cmp_int, NULL);
    bool ok = true;
    for (size_t i = 1; i < v.n; i++) {
        int *a = v.items[i - 1], *b = v.items[i];
        if (a[0] > b[0] || (a[0] == b[0] && a[1] > b[1])) ok = false;
    }
    CHECK(ok && v.n == 200);
    pv_free(&v);
    // inserção no meio, no começo e no fim (mais de uma realocação)
    static int nums[40];
    for (int i = 0; i < 40; i++) nums[i] = i;
    for (int i = 0; i < 40; i += 2) pv_push(&v, &nums[i]);
    for (int i = 1; i < 40; i += 2) pv_insert(&v, (size_t)i, &nums[i]);
    ok = v.n == 40;
    for (size_t i = 0; i < v.n && ok; i++) ok = *(int *)v.items[i] == (int)i;
    pv_insert(&v, 0, &nums[5]);
    pv_insert(&v, v.n, &nums[7]);
    CHECK(ok && v.n == 42 && *(int *)v.items[0] == 5 && *(int *)v.items[1] == 0 && *(int *)v.items[41] == 7);
    pv_free(&v);
}

// ---------------------------------------------------------------- caminhos

static void test_paths(void) {
    CHECK_WEQ_FREE(path_norm(u"\"C:/Program Files//Foo\\\" "), u"C:\\Program Files\\Foo");
    CHECK_WEQ_FREE(path_norm(u"C:\\"), u"C:\\");
    CHECK_WEQ_FREE(path_norm(u"C:\\x\\"), u"C:\\x");
    CHECK_WEQ_FREE(path_norm(u"\\\\server\\share\\"), u"\\\\server\\share");
    CHECK_WEQ_FREE(path_join(u"C:\\a\\", u"\\b"), u"C:\\a\\b");
    CHECK_WEQ_FREE(path_join(u"C:\\a", u"b"), u"C:\\a\\b");
    CHECK_WEQ(path_leaf(u"C:\\a\\b.exe"), u"b.exe");
    CHECK_WEQ_FREE(path_parent(u"C:\\a\\b"), u"C:\\a");
    CHECK_WEQ_FREE(path_parent(u"C:\\a"), u"C:\\");
    CHECK(path_parent(u"abc") == NULL);
    CHECK(path_is_under(u"C:\\A\\B", u"c:\\a"));
    CHECK(!path_is_under(u"C:\\AB", u"C:\\A"));
    CHECK(!path_is_under(u"C:\\A", u"C:\\A"));
    CHECK(path_is_under_or_eq(u"C:\\A", u"c:\\a"));
    CHECK(path_is_under(u"C:\\x", u"C:\\"));
    CHECK_WEQ(path_ext(u"C:\\a.b\\c.EXE"), u".EXE");
    CHECK_WEQ(path_ext(u"C:\\a.b\\c"), u"");
    CHECK_WEQ_FREE(path_strip_ext(u"app.exe"), u"app");
}

// ---------------------------------------------------------------- proteção

static void setup_protect(void) {
    protect_reset();
    protect_add_dir_tree(u"C:\\Windows");
    protect_add_dir(u"C:\\Program Files");
    protect_add_dir(u"C:\\Program Files (x86)");
    protect_add_dir(u"C:\\Program Files\\Common Files");
    protect_add_dir(u"C:\\ProgramData");
    protect_add_dir(u"C:\\Users\\ana\\AppData\\Roaming");
    protect_add_dir(u"C:\\Users\\ana\\AppData\\Local");
    protect_add_dir(u"C:\\Users\\ana\\Desktop");
}

static void test_protect(void) {
    setup_protect();
    CHECK(protect_dir_blocked(u"C:\\"));
    CHECK(protect_dir_blocked(u"D:"));
    CHECK(protect_dir_blocked(u"\\\\srv\\share\\x"));
    CHECK(protect_dir_blocked(u"relativo\\x"));
    CHECK(protect_dir_blocked(u"C:\\Program Files"));
    CHECK(protect_dir_blocked(u"c:\\program files\\"));
    CHECK(protect_dir_blocked(u"C:\\Users"));       // ancestral de pasta protegida
    CHECK(protect_dir_blocked(u"C:\\Users\\ana"));  // idem
    CHECK(protect_dir_blocked(u"C:\\Windows\\System32\\drivers\\x.sys"));
    CHECK(!protect_dir_blocked(u"C:\\Program Files\\Mozilla Firefox"));
    CHECK(!protect_dir_blocked(u"C:\\Users\\ana\\AppData\\Roaming\\Foo"));
    CHECK(!protect_dir_blocked(u"C:\\Python312"));
    CHECK(!protect_dir_blocked(u"C:\\WindowsApps2"));  // só parece com a pasta do Windows

    CHECK(protect_key_blocked(u"HKLM"));
    CHECK(protect_key_blocked(u"HKLM\\SOFTWARE"));
    CHECK(protect_key_blocked(u"HKLM\\Software\\Microsoft"));
    CHECK(protect_key_blocked(u"HKLM\\SOFTWARE\\WOW6432Node"));
    CHECK(protect_key_blocked(u"HKCU\\Software\\Classes\\.txt"));
    CHECK(protect_key_blocked(u"HKLM\\SOFTWARE\\Classes\\CLSID"));
    CHECK(protect_key_blocked(u"HKLM\\SYSTEM\\CurrentControlSet\\Services\\Foo"));
    CHECK(protect_key_blocked(u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"));
    CHECK(protect_key_blocked(u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Installer\\UserData\\X"));
    CHECK(protect_key_blocked(u"HKLM\\SOFTWARE\\Classes\\Installer\\Products\\ABC"));
    CHECK(protect_key_blocked(u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.foo"));
    CHECK(protect_key_blocked(u"HKLM\\SOFTWARE\\Policies\\Google"));
    CHECK(!protect_key_blocked(u"HKLM\\SOFTWARE\\Mozilla"));
    CHECK(!protect_key_blocked(u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Foo"));
    CHECK(!protect_key_blocked(u"HKCU\\Software\\Classes\\FooFile"));
    CHECK(!protect_key_blocked(u"HKCU\\Software\\Classes\\.foo\\OpenWithProgids"));
    CHECK(!protect_key_blocked(u"HKLM\\SOFTWARE\\WOW6432Node\\Foo"));
    CHECK(!protect_key_blocked(u"HKLM\\SOFTWARE\\Classes\\CLSID\\{00000000-0000-0000-0000-000000000001}"));

    CHECK(!protect_value_blocked(u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"));
    CHECK(protect_value_blocked(u"HKLM\\SYSTEM\\CurrentControlSet\\Services\\Foo"));
    CHECK(protect_value_blocked(u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Installer\\Folders"));
    CHECK(protect_value_blocked(u"HKLM"));
    CHECK(!protect_value_blocked(u"HKCU\\Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\Shell\\MuiCache"));
    CHECK(!protect_value_blocked(u"HKCU\\Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Compatibility Assistant\\Store"));
    CHECK(protect_value_blocked(u"HKCU\\Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags"));
    CHECK(protect_key_blocked(u"HKCU\\Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\Shell\\MuiCache"));
}

// ---------------------------------------------------------------- identificação

static void test_clean_names(void) {
    CHECK_WEQ_FREE(clean_display_name(u"Mozilla Firefox (x64 pt-BR)"), u"Mozilla Firefox");
    CHECK_WEQ_FREE(clean_display_name(u"7-Zip 23.01 (x64)"), u"7-Zip");
    CHECK_WEQ_FREE(clean_display_name(u"WinRAR 6.24 (64-bit)"), u"WinRAR");
    CHECK_WEQ_FREE(clean_display_name(u"Python 3.12.0 (64-bit)"), u"Python");
    CHECK_WEQ_FREE(clean_display_name(u"Microsoft Visual C++ 2015-2022 Redistributable (x64) - 14.38.33135"), u"Microsoft Visual C++ Redistributable");
    CHECK_WEQ_FREE(clean_display_name(u"Notepad++ (64-bit x64)"), u"Notepad++");
    CHECK_WEQ_FREE(clean_display_name(u"VLC media player"), u"VLC media player");
    CHECK_WEQ_FREE(clean_display_name(u"Git version 2.45.1"), u"Git");
    CHECK_WEQ_FREE(clean_display_name(u"Foo v1.2.3 en-US"), u"Foo");
    CHECK_WEQ_FREE(clean_display_name(u"3DMark"), u"3DMark");
    CHECK_WEQ_FREE(clean_display_name(u"2048"), u"2048");  // só número: fica o original
    CHECK_WEQ_FREE(clean_publisher(u"Mozilla Corporation"), u"Mozilla");
    CHECK_WEQ_FREE(clean_publisher(u"Google LLC"), u"Google");
    CHECK_WEQ_FREE(clean_publisher(u"Valve Corp."), u"Valve");
    CHECK_WEQ_FREE(clean_publisher(u"The Document Foundation"), u"Document Foundation");
    CHECK_WEQ_FREE(clean_publisher(u"Igor Pavlov"), u"Igor Pavlov");
    CHECK_WEQ_FREE(clean_publisher(u"Foo, Inc."), u"Foo");
    CHECK_WEQ_FREE(norm_alnum(u"Mozilla-Firefox 2!"), u"mozillafirefox2");
    CHECK_WEQ_FREE(norm_alnum(u"Notepad++"), u"notepadplusplus");  // não pode virar "notepad"
    CHECK_WEQ_FREE(norm_alnum(u"C# Tools"), u"csharptools");
    CHECK(is_generic_word(u"update"));
    CHECK(is_generic_word(u"2024"));
    CHECK(is_generic_word(u"ab"));
    CHECK(!is_generic_word(u"firefox"));
}

static void test_match(void) {
    setup_protect();
    Identity *ff = identity_new(u"Mozilla Firefox (x64 pt-BR)", u"Mozilla");
    identity_add_dir(ff, u"C:\\Program Files\\Mozilla Firefox", true);
    identity_add_exe(ff, u"C:\\Program Files\\Mozilla Firefox\\firefox.exe,0");
    identity_add_dir(ff, u"C:\\Program Files", true);                      // protegida: ignorada
    identity_add_dir(ff, u"C:\\Program Files\\Mozilla Firefox\\bin", true);  // já coberta
    CHECK(ff->dirs.n == 1);
    CHECK_WEQ(ff->primary, u"firefox");
    CHECK(match_name(ff, u"Mozilla Firefox") == CONF_HIGH);
    CHECK(match_name(ff, u"Firefox") == CONF_HIGH);
    CHECK(match_name(ff, u"MozillaFirefox") == CONF_HIGH);
    CHECK(match_name(ff, u"Mozilla") == CONF_NONE);  // pasta do editor, não do programa
    CHECK(match_name(ff, u"Thunderbird") == CONF_NONE);
    CHECK(match_name(ff, u"Mozilla Maintenance Service") == CONF_NONE);
    CHECK(match_name(ff, u"FirefoxPortable") == CONF_MEDIUM);
    CHECK(match_name(ff, u"Mozilla Firefox Data") == CONF_MEDIUM);
    CHECK(match_name(ff, u"Common Files") == CONF_NONE);
    CHECK(match_name(ff, u"Update") == CONF_NONE);
    CHECK(match_publisher(ff, u"Mozilla"));
    CHECK(match_publisher(ff, u"Mozilla Corporation"));
    CHECK(!match_publisher(ff, u"Firefox"));
    CHECK(match_path_ref(ff, u"\"C:\\Program Files\\Mozilla Firefox\\firefox.exe\" -osint -url \"%1\""));
    CHECK(match_path_ref(ff, u"c:\\program files\\mozilla firefox"));
    CHECK(!match_path_ref(ff, u"C:\\Program Files\\Mozilla Firefox Beta\\firefox.exe"));
    CHECK(!match_path_ref(ff, u"C:\\Program Files\\Foo\\bar.exe"));
    identity_free(ff);

    Identity *z = identity_new(u"7-Zip 23.01 (x64)", u"Igor Pavlov");
    identity_add_dir(z, u"C:\\Program Files\\7-Zip\\", true);
    CHECK(match_name(z, u"7-Zip") == CONF_HIGH);
    CHECK(match_name(z, u"7zip") == CONF_HIGH);
    CHECK(match_name(z, u"Zip") == CONF_NONE);
    identity_free(z);

    Identity *st = identity_new(u"Steam", u"Valve Corporation");
    CHECK(match_name(st, u"Steam") == CONF_HIGH);
    CHECK(match_name(st, u"SteamLibrary") == CONF_MEDIUM);
    CHECK(match_name(st, u"Valve") == CONF_NONE);
    CHECK(match_publisher(st, u"Valve"));
    identity_free(st);

    // nome curto do editor: "Nimbus Software Ltd." usa a pasta "Nimbus"
    Identity *nb = identity_new(u"Super Editor 4.2.1", u"Nimbus Software Ltd.");
    CHECK_WEQ(nb->publisher_short, u"nimbus");
    CHECK(match_publisher(nb, u"Nimbus"));
    CHECK(match_publisher(nb, u"Nimbus Software"));
    CHECK(match_name(nb, u"Nimbus") == CONF_NONE);
    CHECK(match_name(nb, u"Super Editor") == CONF_HIGH);
    identity_free(nb);
    Identity *ea = identity_new(u"Origin", u"Electronic Arts");
    CHECK(ea->publisher_short == NULL);
    identity_free(ea);
    Identity *ad = identity_new(u"Adobe Acrobat", u"Adobe Systems Incorporated");
    CHECK_WEQ(ad->publisher_short, u"adobe");
    identity_free(ad);
    Identity *gs = identity_new(u"X", u"Digital Media Group");  // só palavras de empresa: não vira nome curto
    CHECK(gs->publisher_short == NULL);
    identity_free(gs);

    // editor Microsoft nunca vira pasta de editor
    Identity *ms = identity_new(u"Microsoft Teams", u"Microsoft Corporation");
    CHECK(ms->publisher_norm == NULL);
    CHECK(!match_publisher(ms, u"Microsoft"));
    CHECK(match_name(ms, u"Microsoft Teams") == CONF_HIGH);
    CHECK(match_name(ms, u"Teams") == CONF_HIGH);  // pasta AppData\Local\Microsoft\Teams
    identity_free(ms);

    // pasta genérica escolhida pelo usuário: o nome dela só vira nome do programa se for dito
    Identity *g = identity_new(u"Jogo Legal", u"Estúdio X");
    identity_add_dir(g, u"C:\\Jogos", false);
    CHECK(match_name(g, u"Jogos") == CONF_NONE);
    CHECK(match_path_ref(g, u"C:\\Jogos\\jogo.exe"));
    identity_add_dir(g, u"D:\\JogoLegalFiles", false);
    CHECK(match_name(g, u"JogoLegalFiles") == CONF_HIGH);  // parece com o nome: conta
    identity_free(g);
    Identity *g2 = identity_new(u"Jogo Legal", NULL);
    identity_add_dir(g2, u"C:\\Jogos", true);
    CHECK(match_name(g2, u"Jogos") == CONF_HIGH);
    identity_free(g2);

    // nomes curtos 8.3 e versão
    Identity *py = identity_new(u"Python 3.12.0 (64-bit)", u"Python Software Foundation");
    identity_set_version(py, u"3.12.0");
    CHECK_WEQ(py->vtag, u"312");
    identity_add_dir(py, u"C:\\Program Files\\Python312", true);
    identity_add_alt_dir(py, u"C:\\PROGRA~1\\PYTHON~1");
    CHECK(match_path_ref(py, u"C:\\PROGRA~1\\PYTHON~1\\python.exe"));
    CHECK(match_path_ref(py, u"App=C:\\Program Files\\Python312\\python.exe|Name=x"));
    identity_free(py);
    Identity *v = identity_new(u"x", NULL);
    identity_set_version(v, u"2024");
    CHECK(v->vtag == NULL);
    identity_set_version(v, u"v131.0.2");
    CHECK_WEQ(v->vtag, u"1310");
    identity_free(v);

    // a pasta mais específica decide de quem é a referência
    PtrVec a = {0}, b = {0};
    pv_push(&a, u"C:\\Program Files\\Acme");
    pv_push(&b, u"C:\\Program Files\\Acme\\Other");
    const WCHAR *t1 = u"\"C:\\Program Files\\Acme\\Other\\o.exe\" /x", *t2 = u"C:\\Program Files\\Acme\\tool.exe";
    CHECK(path_ref_len(&a, t1) == 21 && path_ref_len(&b, t1) == 27);
    CHECK(path_ref_len(&a, t2) == 21 && path_ref_len(&b, t2) == 0);
    CHECK(path_ref_len(&a, u"C:\\Program Files\\Acme2\\x") == 0);
    pv_free(&a), pv_free(&b);

    // executável de desinstalação não vira nome do programa
    Identity *u = identity_new(u"Foo Bar Editor", u"Foo Ltd");
    identity_add_exe(u, u"C:\\Program Files\\FooBar\\unins000.exe");
    identity_add_exe(u, u"C:\\Windows\\System32\\msiexec.exe");
    CHECK(match_name(u, u"unins000") == CONF_NONE);
    CHECK(match_name(u, u"msiexec") == CONF_NONE);
    CHECK(match_name(u, u"Foo Bar Editor") == CONF_HIGH);
    CHECK(match_name(u, u"Foo") == CONF_NONE);  // é o editor
    identity_free(u);
}

// ---------------------------------------------------------------- linhas de comando

static void test_cmdline(void) {
    Cmd c;
    CHECK(cmd_split(u"\"C:\\Program Files\\Foo\\uninst.exe\" /S", &c));
    CHECK_WEQ(c.exe, u"C:\\Program Files\\Foo\\uninst.exe");
    CHECK_WEQ(c.args, u"/S");
    cmd_free(&c);

    CHECK(cmd_split(u"C:\\Program Files (x86)\\Foo Bar\\unins000.exe /SILENT", &c));
    CHECK_WEQ(c.exe, u"C:\\Program Files (x86)\\Foo Bar\\unins000.exe");
    CHECK_WEQ(c.args, u"/SILENT");
    CHECK(cmd_kind(&c) == UNINST_INNO);
    cmd_free(&c);

    CHECK(cmd_split(u"C:\\Tools\\app.exe", &c));
    CHECK_WEQ(c.exe, u"C:\\Tools\\app.exe");
    CHECK_WEQ(c.args, u"");
    cmd_free(&c);

    CHECK(cmd_split(u"rundll32.exe dfshim.dll,ShArpMaintain foo.application", &c));
    CHECK_WEQ(c.exe, u"rundll32.exe");
    CHECK_WEQ(c.args, u"dfshim.dll,ShArpMaintain foo.application");
    cmd_free(&c);

    CHECK(cmd_split(u"uninstall --all", &c));
    CHECK_WEQ(c.exe, u"uninstall");
    CHECK_WEQ(c.args, u"--all");
    cmd_free(&c);

    CHECK(!cmd_split(u"   ", &c));
    cmd_free(&c);

    CHECK(guid_valid(u"{23170F69-40C1-2702-2301-000001000000}"));
    CHECK(!guid_valid(u"{23170F69-40C1-2702-2301-00000100000}"));
    CHECK(!guid_valid(u"{23170F69x40C1-2702-2301-000001000000}"));
    {
        // código compactado do Windows Installer (conferido no Registro de um Windows com o GridCraft 0.9.0)
        WCHAR packed[33];
        CHECK(guid_pack(u"{923A99B5-32DE-4270-ABCE-026019B0C12F}", packed) && weq(packed, u"5B99A329ED230724BAEC2006910B1CF2"));
        CHECK(guid_pack(u"{923a99b5-32de-4270-abce-026019b0c12f}", packed) && weq(packed, u"5B99A329ED230724BAEC2006910B1CF2"));
        CHECK(!guid_pack(u"Mozilla Firefox", packed) && !guid_pack(u"{923A99B5-32DE-4270-ABCE-026019B0C12}", packed));
    }

    WCHAR g[39];
    CHECK(cmd_split(u"MsiExec.exe /I{23170F69-40C1-2702-2301-000001000000}", &c));
    CHECK(cmd_msi_product(&c, g));
    CHECK_WEQ(g, u"{23170F69-40C1-2702-2301-000001000000}");
    CHECK(cmd_kind(&c) == UNINST_MSI);
    cmd_free(&c);
    CHECK(cmd_split(u"C:\\Windows\\System32\\msiexec.exe /x {23170F69-40C1-2702-2301-000001000000} /qb", &c));
    CHECK(cmd_msi_product(&c, g));
    cmd_free(&c);
    CHECK(cmd_split(u"\"C:\\x\\setup.exe\" /I{23170F69-40C1-2702-2301-000001000000}", &c));
    CHECK(!cmd_msi_product(&c, g));
    cmd_free(&c);

    CHECK_WEQ_FREE(msi_uninstall_args(u"{23170F69-40C1-2702-2301-000001000000}", false), u"/X{23170F69-40C1-2702-2301-000001000000}");
    CHECK_WEQ_FREE(cmd_quiet_variant(u"MsiExec.exe /I{23170F69-40C1-2702-2301-000001000000}", NULL),
                   u"\"msiexec.exe\" /X{23170F69-40C1-2702-2301-000001000000} /passive /norestart");
    CHECK_WEQ_FREE(cmd_quiet_variant(u"\"C:\\Program Files\\Foo\\unins000.exe\"", u""),
                   u"\"C:\\Program Files\\Foo\\unins000.exe\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART");
    CHECK_WEQ_FREE(cmd_quiet_variant(u"\"C:\\x\\uninst.exe\"", u" \"C:\\x\\uninst.exe\" /S "), u"\"C:\\x\\uninst.exe\" /S");
    CHECK(cmd_quiet_variant(u"\"C:\\x\\uninst.exe\"", NULL) == NULL);
    CHECK_WEQ_FREE(cmd_join(u"C:\\a b\\c.exe", u"/x"), u"\"C:\\a b\\c.exe\" /x");
}

// ---------------------------------------------------------------- arquivos .reg

static void test_regtext(void) {
    CHECK_WEQ_FREE(reg_long_name(u"HKLM\\SOFTWARE\\Foo"), u"HKEY_LOCAL_MACHINE\\SOFTWARE\\Foo");
    CHECK_WEQ_FREE(reg_long_name(u"HKCU"), u"HKEY_CURRENT_USER");
    CHECK_WEQ_FREE(reg_long_name(u"HKLMX\\a"), u"HKLMX\\a");
    CHECK_WEQ_FREE(reg_short_name(u"HKEY_CURRENT_USER\\Software"), u"HKCU\\Software");

    StrBuf b = {0};
    regtext_header(&b);
    regtext_key(&b, u"HKCU\\Software\\Foo");
    const WCHAR sz[] = u"C:\\a \"b\"";
    regtext_value(&b, u"Path", RT_SZ, (const uint8_t *)sz, sizeof(sz));
    regtext_value(&b, NULL, RT_SZ, (const uint8_t *)u"x", 4);
    const uint8_t dw[4] = {0x01, 0x00, 0x01, 0x00};
    regtext_value(&b, u"Ver", RT_DWORD, dw, 4);
    const WCHAR ex[] = u"%A%";
    regtext_value(&b, u"E", RT_EXPAND_SZ, (const uint8_t *)ex, sizeof(ex));
    const WCHAR multi[] = u"a\0b\0";
    regtext_value(&b, u"M", RT_MULTI_SZ, (const uint8_t *)multi, sizeof(multi));
    const WCHAR nl[] = u"l1\r\nl2";
    regtext_value(&b, u"N\"q", RT_SZ, (const uint8_t *)nl, sizeof(nl));
    regtext_value(&b, u"Q", RT_QWORD, (const uint8_t *)"\x01\0\0\0\0\0\0\0", 8);
    regtext_value(&b, u"Empty", RT_SZ, (const uint8_t *)"", 0);
    CHECK_WEQ(sb_str(&b), u"Windows Registry Editor Version 5.00\r\n"
                          u"\r\n[HKEY_CURRENT_USER\\Software\\Foo]\r\n"
                          u"\"Path\"=\"C:\\\\a \\\"b\\\"\"\r\n"
                          u"@=\"x\"\r\n"
                          u"\"Ver\"=dword:00010001\r\n"
                          u"\"E\"=hex(2):25,00,41,00,25,00,00,00\r\n"
                          u"\"M\"=hex(7):61,00,00,00,62,00,00,00,00,00\r\n"
                          u"\"N\\\"q\"=hex(1):6c,00,31,00,0d,00,0a,00,6c,00,32,00,00,00\r\n"
                          u"\"Q\"=hex(b):01,00,00,00,00,00,00,00\r\n"
                          u"\"Empty\"=\"\"\r\n");
    sb_free(&b);

    // dados binários longos quebram a linha em no máximo 80 colunas
    uint8_t big[100];
    for (int i = 0; i < 100; i++) big[i] = (uint8_t)i;
    regtext_value(&b, u"Bin", RT_BINARY, big, sizeof(big));
    const WCHAR *s = sb_str(&b);
    size_t col = 0, maxcol = 0, lines = 1;
    for (const WCHAR *p = s; *p; p++) {
        if (*p == '\n') lines++, col = 0;
        else if (*p != '\r') col++, maxcol = col > maxcol ? col : maxcol;
    }
    CHECK(maxcol <= 80);
    CHECK(lines > 3);
    CHECK(wistarts(s, u"\"Bin\"=hex:00,01,02,"));
    CHECK(wifind(s, u",\\\r\n  ") != NULL);
    CHECK(wiends(s, u"62,63\r\n"));
    sb_free(&b);
}

// ---------------------------------------------------------------- rastreamento

static void test_trace(void) {
    HashSet hs = {0};
    for (uint64_t i = 1; i <= 100000; i++) hs_add(&hs, i * 0x9E3779B97F4A7C15ULL);
    bool all = true;
    for (uint64_t i = 1; i <= 100000; i++) all = all && hs_has(&hs, i * 0x9E3779B97F4A7C15ULL);
    CHECK(all);
    CHECK(hs.n == 100000);
    CHECK(!hs_has(&hs, 12345));
    hs_add(&hs, 0);  // 0 é aceito (vira 1)
    CHECK(hs_has(&hs, 0));
    hs_free(&hs);

    CHECK(hash_entry(TR_KEY, u"HKLM\\SOFTWARE\\Foo", NULL) == hash_entry(TR_KEY, u"hklm\\software\\FOO", NULL));
    CHECK(hash_entry(TR_KEY, u"A", NULL) != hash_entry(TR_DIR, u"A", NULL));
    CHECK(hash_entry(TR_VALUE, u"A", u"") != hash_entry(TR_KEY, u"A", NULL));

    Snapshot s = {0};
    s.out = trace_new();
    snap_record(&s, TR_KEY, u"HKLM\\SOFTWARE\\Old", NULL);
    snap_record(&s, TR_VALUE, u"HKLM\\SOFTWARE\\Old", u"v1");
    snap_record(&s, TR_DIR, u"C:\\Program Files\\Old", NULL);
    CHECK(snap_check(&s, TR_KEY, u"hklm\\software\\old", NULL));
    CHECK(snap_check(&s, TR_VALUE, u"HKLM\\SOFTWARE\\Old", u"V1"));
    CHECK(!snap_check(&s, TR_VALUE, u"HKLM\\SOFTWARE\\Old", u"v2"));
    CHECK(!snap_check(&s, TR_KEY, u"HKLM\\SOFTWARE\\New", NULL));
    CHECK(!snap_check(&s, TR_FILE, u"C:\\Program Files\\Old\\new.dll", NULL));
    CHECK(s.added == 3 && s.out->items.n == 3);
    hs_free(&s.before);

    Trace *t = s.out;
    t->name = wdup(u"Programa\tcom tab, 100% e ção");
    t->installer = wdup(u"C:\\Downloads\\setup.exe");
    t->date = wdup(u"2026-10-18 14:05");
    trace_add(t, TR_DIR, u"C:\\Program Files\\Novo\nlinha", NULL);
    trace_add(t, TR_VALUE, u"HKCU\\Software\\X", u"");
    size_t len;
    char *txt = trace_serialize(t, &len);
    CHECK(strncmp(txt, "EXTIRPA-TRACE 1\nname\tPrograma%09com tab, 100%25 e ção\n", 55) == 0);
    Trace *p = trace_parse(txt, len);
    CHECK(p != NULL);
    if (p) {
        CHECK_WEQ(p->name, t->name);
        CHECK_WEQ(p->installer, t->installer);
        CHECK_WEQ(p->date, t->date);
        CHECK(p->items.n == t->items.n);
        bool same = p->items.n == t->items.n;
        for (size_t i = 0; same && i < p->items.n; i++) {
            TraceItem *a = p->items.items[i], *b2 = t->items.items[i];
            same = a->kind == b2->kind && weq(a->path, b2->path) && (a->kind != TR_VALUE || weq(a->value, b2->value));
        }
        CHECK(same);
        trace_free(p);
    }
    xfree(txt);
    trace_free(t);

    // CRLF e BOM (arquivo salvo no Bloco de Notas)
    const char *crlf = "\xEF\xBB\xBF" "EXTIRPA-TRACE 1\r\nname\tX\r\nK\tHKLM\\SOFTWARE\\X\r\nV\tHKCU\\Software\\Y\tval\r\nZ\tignorado\r\n";
    Trace *c = trace_parse(crlf, strlen(crlf));
    CHECK(c && c->items.n == 2);
    if (c) {
        CHECK_WEQ(c->name, u"X");
        CHECK_WEQ(((TraceItem *)c->items.items[1])->value, u"val");
        trace_free(c);
    }
    CHECK(trace_parse("lixo", 4) == NULL);
    CHECK(trace_parse("EXTIRPA-TRACE 2\n", 16) == NULL);
}

// ---------------------------------------------------------------- sobras

static void test_leftovers(void) {
    setup_protect();
    LeftList l = {0};
    CHECK(left_add(&l, LK_DIR, u"C:\\Program Files", NULL, CONF_HIGH, LR_INSTALL_DIR, NULL) == NULL);  // protegida
    CHECK(left_add(&l, LK_KEY, u"HKLM\\SOFTWARE\\Microsoft", NULL, CONF_HIGH, LR_NAME, NULL) == NULL);
    Leftover *a = left_add(&l, LK_DIR, u"C:\\Program Files\\Foo\\", NULL, CONF_MEDIUM, LR_NAME, NULL);
    Leftover *b = left_add(&l, LK_DIR, u"c:\\program files\\foo", NULL, CONF_HIGH, LR_INSTALL_DIR, NULL);
    CHECK(a == b && a->conf == CONF_HIGH && a->reason == LR_INSTALL_DIR);
    left_add(&l, LK_FILE, u"C:\\Program Files\\Foo\\bin\\x.dll", NULL, CONF_HIGH, LR_TRACE, NULL);  // coberto pela pasta
    left_add(&l, LK_DIR, u"C:\\ProgramData\\Foo", NULL, CONF_LOW, LR_NAME, NULL);
    left_add(&l, LK_FILE, u"C:\\ProgramData\\Foo\\cfg.ini", NULL, CONF_HIGH, LR_TRACE, NULL);  // pai mais fraco: fica
    left_add(&l, LK_KEY, u"HKCU\\Software\\Foo", NULL, CONF_HIGH, LR_NAME, NULL);
    left_add(&l, LK_VALUE, u"HKCU\\Software\\Foo", u"x", CONF_HIGH, LR_TRACE, NULL);  // coberto pela chave
    left_add(&l, LK_VALUE, u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", u"Foo", CONF_HIGH, LR_RUN, u"cmd");
    left_add(&l, LK_KEY_IF_EMPTY, u"HKCU\\Software\\FooCorp", NULL, CONF_MEDIUM, LR_PUBLISHER, NULL);
    left_add(&l, LK_SERVICE, u"FooSvc", NULL, CONF_HIGH, LR_SERVICE, NULL);
    CHECK(left_add(&l, LK_SERVICE, u"a\\b", NULL, CONF_HIGH, LR_SERVICE, NULL) == NULL);
    // tarefas: nunca as do Windows nem nomes com aspas
    CHECK(left_add(&l, LK_TASK, u"\\Microsoft\\Windows\\Defrag\\ScheduledDefrag", NULL, CONF_HIGH, LR_TASK, NULL) == NULL);
    CHECK(left_add(&l, LK_TASK, u"\\a\"b", NULL, CONF_HIGH, LR_TASK, NULL) == NULL);
    CHECK(left_add(&l, LK_TASK, u"semBarra", NULL, CONF_HIGH, LR_TASK, NULL) == NULL);
    // PATH: só as duas chaves de ambiente, e com a entrada preenchida
    CHECK(left_add(&l, LK_PATHENTRY, u"HKCU\\Software\\X", u"C:\\Foo", CONF_HIGH, LR_PATH_VAR, NULL) == NULL);
    CHECK(left_add(&l, LK_PATHENTRY, ENV_KEY_USER, u"", CONF_HIGH, LR_PATH_VAR, NULL) == NULL);
    left_finalize(&l);
    CHECK(left_add(&l, LK_TASK, u"\\FooUpdate", NULL, CONF_HIGH, LR_TASK, NULL) != NULL);
    CHECK(left_add(&l, LK_FWRULE, u"TCP Query User{1}C:\\foo.exe", NULL, CONF_HIGH, LR_FIREWALL, NULL) != NULL);
    CHECK(left_add(&l, LK_PATHENTRY, ENV_KEY_USER, u"C:\\Foo\\bin", CONF_HIGH, LR_PATH_VAR, NULL) != NULL);
    CHECK(left_add(&l, LK_PATHENTRY, ENV_KEY_USER, u"C:\\Foo\\bin", CONF_HIGH, LR_PATH_VAR, NULL) == l.items.items[l.items.n - 1]);  // repetido
    size_t before = l.items.n;
    left_add(&l, LK_PATHENTRY, ENV_KEY_MACHINE, u"C:\\Foo\\bin", CONF_HIGH, LR_PATH_VAR, NULL);  // outra chave: outro item
    CHECK(l.items.n == before + 1);
    left_finalize(&l);
    CHECK(l.items.n == 11);
    Leftover *first = l.items.items[0];
    CHECK(first->kind == LK_SERVICE || first->kind == LK_TASK || first->kind == LK_FWRULE || first->kind == LK_PATHENTRY);
    bool found_cfg = false, found_dll = false, if_empty_checked = false, low_unchecked = false;
    for (size_t i = 0; i < l.items.n; i++) {
        Leftover *it = l.items.items[i];
        if (wiends(it->path, u"cfg.ini")) found_cfg = true;
        if (wiends(it->path, u"x.dll")) found_dll = true;
        if (it->kind == LK_KEY_IF_EMPTY) if_empty_checked = it->checked;
        if (it->conf == CONF_LOW) low_unchecked = !it->checked;
    }
    CHECK(found_cfg && !found_dll && if_empty_checked && low_unchecked);
    left_free(&l);

    // rastreamento -> sobras
    Trace *t = trace_new();
    trace_add(t, TR_KEY, u"HKLM\\SYSTEM\\CurrentControlSet\\Services\\FooSvc", NULL);
    trace_add(t, TR_KEY, u"HKLM\\SOFTWARE\\Foo", NULL);
    trace_add(t, TR_KEY, u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\UserAssist\\X", NULL);
    trace_add(t, TR_KEY, u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Foo", NULL);
    trace_add(t, TR_VALUE, u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", u"Foo");
    trace_add(t, TR_KEY, u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Installer\\UserData\\S-1\\Products\\X", NULL);
    trace_add(t, TR_DIR, u"C:\\Users\\ana\\AppData\\Local\\Temp\\setup123", NULL);
    trace_add(t, TR_DIR, u"C:\\Program Files\\Foo", NULL);
    trace_add(t, TR_FILE, u"C:\\Windows\\System32\\Tasks\\Foo\\FooUpdate", NULL);
    trace_add(t, TR_DIR, u"C:\\Windows\\System32\\Tasks\\Foo", NULL);
    trace_add(t, TR_VALUE, FIREWALL_RULES_KEY, u"{ABC-123}");
    trace_add(t, TR_KEY, APPX_PACKAGES_KEY u"\\Foo_1.0.0.0_neutral__abc123", NULL);
    trace_add(t, TR_KEY, APPX_PACKAGES_KEY u"\\Microsoft.Foo_1.0_x64__8wekyb3d8bbwe", NULL);  // da Microsoft: nunca
    LeftList m = {0};
    left_add_trace(&m, t);
    left_finalize(&m);
    int high = 0, low = 0, svc = 0, task = 0, fw = 0, appx = 0;
    for (size_t i = 0; i < m.items.n; i++) {
        Leftover *it = m.items.items[i];
        if (it->kind == LK_SERVICE) svc++;
        if (it->kind == LK_TASK && weq(it->path, u"\\Foo\\FooUpdate")) task++;
        if (it->kind == LK_FWRULE && weq(it->path, u"{ABC-123}")) fw++;
        if (it->kind == LK_APPX && weq(it->path, u"Foo_1.0.0.0_neutral__abc123")) appx++;
        if (it->conf == CONF_HIGH) high++;
        if (it->conf == CONF_LOW) low++;
    }
    CHECK(m.items.n == 9);  // as do Windows Installer, do UserAssist e o pacote da Microsoft são bloqueadas
    CHECK(svc == 1 && task == 1 && fw == 1 && appx == 1 && high == 8 && low == 1);
    left_free(&m);
    trace_free(t);
}

// ---------------------------------------------------------------- atalhos

static size_t put16(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8);
    return 2;
}
static size_t put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24);
    return 4;
}

// Monta um .lnk mínimo conforme a especificação MS-SHLLINK.
static size_t make_lnk(uint8_t *buf, bool unicode_info) {
    static const uint8_t clsid[16] = {0x01, 0x14, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46};
    memset(buf, 0, 4096);
    size_t o = 0;
    o += put32(buf + o, 0x4C);
    memcpy(buf + o, clsid, 16), o += 16;
    o += put32(buf + o, 0x2 | 0x10 | 0x20 | 0x80);  // LinkInfo, WorkingDir, Arguments, Unicode
    o = 76;
    // LinkInfo
    const char *base = "C:\\Program Files\\Foo\\foo.exe";
    const WCHAR *ubase = u"C:\\Program Files\\Foo\\fôo.exe";
    size_t li = o, hdr = unicode_info ? 0x24 : 0x1C;
    size_t vol = hdr, vol_size = 0x11;  // VolumeID com rótulo vazio
    size_t basep = vol + vol_size, base_len = strlen(base) + 1;
    size_t suffix = basep + base_len;
    size_t ubp = suffix + 1, ub_len = (wlen(ubase) + 1) * 2;
    size_t usuf = ubp + ub_len;
    size_t total = unicode_info ? usuf + 2 : suffix + 1;
    put32(buf + li + 0, (uint32_t)total);
    put32(buf + li + 4, (uint32_t)hdr);
    put32(buf + li + 8, 1);
    put32(buf + li + 12, (uint32_t)vol);
    put32(buf + li + 16, (uint32_t)basep);
    put32(buf + li + 20, 0);
    put32(buf + li + 24, (uint32_t)suffix);
    if (unicode_info) put32(buf + li + 28, (uint32_t)ubp), put32(buf + li + 32, (uint32_t)usuf);
    put32(buf + li + vol, (uint32_t)vol_size);
    put32(buf + li + vol + 4, 3);
    put32(buf + li + vol + 12, 0x10);
    memcpy(buf + li + basep, base, base_len);
    if (unicode_info)
        for (size_t i = 0; i <= wlen(ubase); i++) put16(buf + li + ubp + 2 * i, ubase[i]);
    o = li + total;
    // StringData: WorkingDir, Arguments
    const WCHAR *wd = u"C:\\Program Files\\Foo", *args = u"--x";
    o += put16(buf + o, (uint32_t)wlen(wd));
    for (size_t i = 0; i < wlen(wd); i++) o += put16(buf + o, wd[i]);
    o += put16(buf + o, (uint32_t)wlen(args));
    for (size_t i = 0; i < wlen(args); i++) o += put16(buf + o, args[i]);
    o += put32(buf + o, 0);  // fim dos blocos extras
    return o;
}

static void test_lnk(void) {
    static uint8_t buf[4096];
    LnkInfo l;
    size_t n = make_lnk(buf, false);
    CHECK(lnk_parse(buf, n, &l));
    CHECK_WEQ(l.target, u"C:\\Program Files\\Foo\\foo.exe");
    CHECK_WEQ(l.workdir, u"C:\\Program Files\\Foo");
    CHECK_WEQ(l.args, u"--x");
    lnk_free(&l);
    n = make_lnk(buf, true);
    CHECK(lnk_parse(buf, n, &l));
    CHECK_WEQ(l.target, u"C:\\Program Files\\Foo\\fôo.exe");
    lnk_free(&l);
    // truncado ou lixo: não lê fora do buffer
    for (size_t cut = 0; cut < n; cut += 7) {
        if (lnk_parse(buf, cut, &l)) lnk_free(&l);
    }
    CHECK(!lnk_parse((const uint8_t *)"not a link at all, really not a link at all, really not a link at all, ok", 76, &l));
}

// ---------------------------------------------------------------- formatos

static void test_fmt(void) {
    FmtStyle br = {',', true}, us = {'.', false};
    CHECK_WEQ_FREE(fmt_size(0, &br), u"0 B");
    CHECK_WEQ_FREE(fmt_size(1023, &br), u"1023 B");
    CHECK_WEQ_FREE(fmt_size(1024, &br), u"1,00 KB");
    CHECK_WEQ_FREE(fmt_size(1536, &us), u"1.50 KB");
    CHECK_WEQ_FREE(fmt_size(10ULL * 1024 * 1024, &br), u"10,0 MB");
    CHECK_WEQ_FREE(fmt_size(1048575, &br), u"1,00 MB");
    CHECK_WEQ_FREE(fmt_size(123456789, &br), u"118 MB");
    CHECK_WEQ_FREE(fmt_size(5ULL * 1024 * 1024 * 1024 * 1024 * 1024, &br), u"5120 TB");
    CHECK_WEQ_FREE(fmt_date(2026, 10, 8, &br), u"08/10/2026");
    CHECK_WEQ_FREE(fmt_date(2026, 10, 8, &us), u"2026-10-08");
    CHECK_WEQ_FREE(fmt_datetime(2026, 1, 2, 3, 4, &br), u"02/01/2026 03:04");
    int y, m, d;
    CHECK(parse_install_date(u"20261018", &y, &m, &d) && y == 2026 && m == 10 && d == 18);
    CHECK(parse_install_date(u"2026-10-18", &y, &m, &d) && d == 18);
    CHECK(parse_install_date(u"18/10/2026", &y, &m, &d) && m == 10);
    CHECK(!parse_install_date(u"20261318", &y, &m, &d));
    CHECK(!parse_install_date(u"abc", &y, &m, &d));
    CHECK(!parse_install_date(NULL, &y, &m, &d));
    CHECK_WEQ_FREE(wfmt(u"%s tem %d itens (%u, %U) 100%% %x", u"Lista", -42, 7u, (uint64_t)1 << 40), u"Lista tem -42 itens (7, 1099511627776) 100% %x");
    CHECK_WEQ_FREE(wfmt(u"%s|%s", NULL, u"b"), u"|b");
    CHECK_WEQ_FREE(wfmt(u"[%c] %s", (WCHAR)'K', u"HKCU"), u"[K] HKCU");
    CHECK_WEQ_FREE(base64_utf16le(u"dir"), u"ZABpAHIA");
    CHECK_WEQ_FREE(base64_utf16le(u"a"), u"YQA=");
    CHECK_WEQ_FREE(base64_utf16le(u""), u"");
    CHECK_WEQ_FREE(base64_utf16le(u"ação"), u"YQDnAOMAbwA=");
}

int main(void) {
    test_wstr();
    test_sort();
    test_paths();
    test_protect();
    test_clean_names();
    test_match();
    test_cmdline();
    test_regtext();
    test_trace();
    test_leftovers();
    test_lnk();
    test_fmt();
    protect_reset();
    printf("%d verificações, %d falhas\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
