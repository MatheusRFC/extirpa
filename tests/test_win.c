// SPDX-License-Identifier: MIT
// Testes da parte Windows contra o Registro e o disco simulados (tests/fakewin.c):
// leitura dos programas instalados, busca de sobras nos três modos, exclusão com backup,
// inicialização, rastreamento de instalação e a lista de proteção de ponta a ponta.
//     python3 tools/build.py test
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/pathx.h"
#include "../src/core/regtext.h"
#include "../src/win/fsx.h"
#include "../src/win/i18n.h"
#include "../src/win/monitor.h"
#include "../src/win/platform.h"
#include "../src/win/programs.h"
#include "../src/win/reg.h"
#include "../src/win/scan.h"
#include "../src/win/startup.h"
#include "../src/win/uninst.h"
#include "fakewin.h"

static int g_fail, g_checks;

static char *u8(const WCHAR *s) { return s ? w_to_utf8(s, wlen(s), NULL) : w_to_utf8(u"(null)", 6, NULL); }

#define CHECK(cond)                                                              \
    do {                                                                         \
        g_checks++;                                                              \
        if (!(cond)) {                                                           \
            g_fail++;                                                            \
            fprintf(stderr, "%s:%d: falhou: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                        \
    } while (0)

#define CHECK_WEQ(got, want)                                                                  \
    do {                                                                                      \
        g_checks++;                                                                           \
        const WCHAR *g_ = (got), *w_ = (want);                                                \
        if (!g_ || !w_ || !weq(g_, w_)) {                                                     \
            char *a_ = u8(g_), *b_ = u8(w_);                                                  \
            g_fail++;                                                                         \
            fprintf(stderr, "%s:%d: esperado [%s], veio [%s]\n", __FILE__, __LINE__, b_, a_); \
            xfree(a_), xfree(b_);                                                             \
        }                                                                                     \
    } while (0)

// ---------------------------------------------------------------- montagem do sistema simulado

#define UNINST u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
#define UNINST32 u"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
#define RUN_HKCU u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define APPROVED_HKCU u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run"
#define START_MENU u"C:\\Users\\ana\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs"

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }

// Atalho .lnk mínimo (MS-SHLLINK) apontando para target
static void make_lnk(const WCHAR *path, const WCHAR *target) {
    static const uint8_t clsid[16] = {0x01, 0x14, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46};
    uint8_t buf[2048];
    memset(buf, 0, sizeof(buf));
    put32(buf, 0x4C);
    memcpy(buf + 4, clsid, 16);
    put32(buf + 20, 0x2 | 0x80);  // LinkInfo, Unicode
    size_t li = 76, hdr = 0x24, vol = hdr, ub = vol + 0x11, n = wlen(target);
    size_t us = ub + (n + 1) * 2, total = us + 2;
    put32(buf + li, (uint32_t)total);
    put32(buf + li + 4, (uint32_t)hdr);
    put32(buf + li + 8, 1);
    put32(buf + li + 12, (uint32_t)vol);
    put32(buf + li + 16, (uint32_t)(vol + 0x10));  // base ANSI vazia (o rótulo do volume termina ali)
    put32(buf + li + 24, (uint32_t)(vol + 0x10));
    put32(buf + li + 28, (uint32_t)ub);
    put32(buf + li + 32, (uint32_t)us);
    put32(buf + li + vol, 0x11);
    for (size_t i = 0; i <= n; i++) put16(buf + li + ub + 2 * i, target[i]);
    fake_file(path, buf, li + total + 4);
}

static void base_system(void) {
    fake_reset();
    static const WCHAR *const dirs[] = {
        u"C:\\Windows\\System32",
        u"C:\\Program Files\\Common Files",
        u"C:\\Program Files (x86)\\Common Files",
        u"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\Startup",
        u"C:\\Users\\ana\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\Startup",
        u"C:\\Users\\ana\\AppData\\Local\\Microsoft\\Windows",
        u"C:\\Users\\ana\\AppData\\Local\\Temp",
        u"C:\\Users\\ana\\AppData\\LocalLow",
        u"C:\\Users\\ana\\Desktop",
        u"C:\\Users\\ana\\Documents",
        u"C:\\Users\\Public\\Desktop",
        u"C:\\Users\\Public\\Documents",
    };
    for (size_t i = 0; i < COUNT(dirs); i++) fake_dir(dirs[i]);
    fake_file(u"C:\\Tools\\extirpa.exe", "MZ", 2);
    fake_file(u"C:\\Windows\\System32\\msiexec.exe", "MZ", 2);
    fake_key(u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall");
    fake_key(u"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall");
    fake_key(u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall");
    fake_key(u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run");
    fake_key(u"HKCU\\Software\\Microsoft\\Office\\16.0");
    fake_str(u"HKLM\\SYSTEM\\CurrentControlSet\\Services\\Dnscache", u"ImagePath", u"%SystemRoot%\\system32\\svchost.exe -k NetworkService");
    fake_key(u"HKLM\\SOFTWARE\\Classes\\.txt");
    fake_key(u"HKLM\\SOFTWARE\\Update");  // nome genérico: nunca é sobra de ninguém
    plat_init();
    i18n_init();
}

static void firefox_installed(void) {
    const WCHAR *k = UNINST u"Mozilla Firefox 131.0 (x64 pt-BR)";
    fake_str(k, u"DisplayName", u"Mozilla Firefox (x64 pt-BR)");
    fake_str(k, u"DisplayVersion", u"131.0");
    fake_str(k, u"Publisher", u"Mozilla");
    fake_str(k, u"InstallLocation", u"C:\\Program Files\\Mozilla Firefox");
    fake_str(k, u"UninstallString", u"\"C:\\Program Files\\Mozilla Firefox\\uninstall\\helper.exe\"");
    fake_str(k, u"DisplayIcon", u"C:\\Program Files\\Mozilla Firefox\\firefox.exe,0");
    fake_dword(k, u"EstimatedSize", 250000);
    const WCHAR *t = UNINST u"Mozilla Thunderbird 128.0 (x64 pt-BR)";
    fake_str(t, u"DisplayName", u"Mozilla Thunderbird (x64 pt-BR)");
    fake_str(t, u"Publisher", u"Mozilla");
    fake_str(t, u"InstallLocation", u"C:\\Program Files\\Mozilla Thunderbird");
    fake_str(t, u"UninstallString", u"\"C:\\Program Files\\Mozilla Thunderbird\\uninstall\\helper.exe\"");
    const WCHAR *m = UNINST32 u"MozillaMaintenanceService";
    fake_str(m, u"DisplayName", u"Mozilla Maintenance Service");
    fake_str(m, u"Publisher", u"Mozilla");
    fake_str(m, u"UninstallString", u"\"C:\\Program Files (x86)\\Mozilla Maintenance Service\\Uninstall.exe\"");

    fake_str(u"HKLM\\SOFTWARE\\Mozilla\\Mozilla Firefox\\131.0 (x64 pt-BR)\\Main", u"PathToExe", u"C:\\Program Files\\Mozilla Firefox\\firefox.exe");
    fake_str(u"HKLM\\SOFTWARE\\Mozilla\\Mozilla Thunderbird\\128.0\\Main", u"PathToExe", u"C:\\Program Files\\Mozilla Thunderbird\\thunderbird.exe");
    fake_str(u"HKLM\\SOFTWARE\\Mozilla\\Firefox\\TaskBarIDs", u"C:\\Program Files\\Mozilla Firefox", u"308046B0AF4A39CB");
    fake_str(u"HKLM\\SOFTWARE\\mozilla.org\\Mozilla", u"CurrentVersion", u"131.0");
    fake_str(u"HKCU\\Software\\Mozilla\\Firefox\\Launcher", u"x", u"1");
    fake_str(u"HKCU\\Software\\FireApp", u"x", u"1");
    fake_str(u"HKLM\\SOFTWARE\\Classes\\FirefoxHTML-308046B0AF4A39CB\\shell\\open\\command", u"",
             u"\"C:\\Program Files\\Mozilla Firefox\\firefox.exe\" -osint -url \"%1\"");
    fake_str(u"HKLM\\SOFTWARE\\Classes\\CLSID\\{0D68D6D0-D93D-4D08-A30D-F00DD1F45B24}\\LocalServer32", u"", u"C:\\Program Files\\Mozilla Firefox\\firefox.exe");
    fake_str(RUN_HKCU, u"Firefox Background", u"\"C:\\Program Files\\Mozilla Firefox\\firefox.exe\" -background");
    fake_str(RUN_HKCU, u"OneDrive", u"\"C:\\Users\\ana\\AppData\\Local\\Microsoft\\OneDrive\\OneDrive.exe\" /background");
    uint8_t on[12] = {2};
    fake_val(APPROVED_HKCU, u"Firefox Background", REG_BINARY, on, 12);
    fake_str(u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\firefox.exe", u"", u"C:\\Program Files\\Mozilla Firefox\\firefox.exe");
    fake_str(u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\firefox.exe", u"Path", u"C:\\Program Files\\Mozilla Firefox");
    fake_dword(u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\SharedDLLs", u"C:\\Program Files\\Mozilla Firefox\\mozglue.dll", 1);
    fake_str(u"HKLM\\SYSTEM\\CurrentControlSet\\Services\\MozillaMaintenance", u"ImagePath",
             u"\"C:\\Program Files (x86)\\Mozilla Maintenance Service\\maintenanceservice.exe\"");

    fake_file(u"C:\\Program Files\\Mozilla Firefox\\firefox.exe", "MZ", 2);
    fake_file(u"C:\\Program Files\\Mozilla Firefox\\mozglue.dll", "MZ", 2);
    fake_file(u"C:\\Program Files\\Mozilla Firefox\\uninstall\\helper.exe", "MZ", 2);
    fake_file(u"C:\\Program Files\\Mozilla Thunderbird\\thunderbird.exe", "MZ", 2);
    fake_file(u"C:\\Program Files (x86)\\Mozilla Maintenance Service\\maintenanceservice.exe", "MZ", 2);
    fake_file(u"C:\\Users\\ana\\AppData\\Roaming\\Mozilla\\Firefox\\Profiles\\abc.default\\prefs.js", "x", 1);
    fake_dir(u"C:\\Users\\ana\\AppData\\Roaming\\Mozilla\\Extensions");
    fake_file(u"C:\\Users\\ana\\AppData\\Roaming\\Thunderbird\\Profiles\\x\\prefs.js", "x", 1);
    fake_file(u"C:\\Users\\ana\\AppData\\Local\\Mozilla\\Firefox\\Profiles\\abc\\cache2\\index", "x", 1);
    fake_file(u"C:\\ProgramData\\FirefoxSync\\state.json", "{}", 2);
    make_lnk(START_MENU u"\\Firefox.lnk", u"C:\\Program Files\\Mozilla Firefox\\firefox.exe");
    make_lnk(u"C:\\Users\\Public\\Desktop\\Firefox.lnk", u"C:\\Program Files\\Mozilla Firefox\\firefox.exe");
    make_lnk(u"C:\\Users\\Public\\Desktop\\Thunderbird.lnk", u"C:\\Program Files\\Mozilla Thunderbird\\thunderbird.exe");
}

// O desinstalador do Firefox: tira a entrada e quase todos os arquivos (deixa uma pasta para trás)
static void firefox_uninstaller_ran(void) {
    WCHAR *k = wdup(UNINST u"Mozilla Firefox 131.0 (x64 pt-BR)");
    WCHAR *parent = path_parent(k);
    HKEY p;
    RegOpenKeyExW(HKEY_LOCAL_MACHINE, parent + 5, 0, KEY_ALL_ACCESS, &p);
    RegDeleteTreeW(p, path_leaf(k));
    xfree(k), xfree(parent);
    DeleteFileW(u"C:\\Program Files\\Mozilla Firefox\\firefox.exe");
    DeleteFileW(u"C:\\Program Files\\Mozilla Firefox\\uninstall\\helper.exe");
    fake_file(u"C:\\Program Files\\Mozilla Firefox\\distribution\\policies.json", "{}", 2);
}

static Program *find_program(PtrVec *v, const WCHAR *name) {
    for (size_t i = 0; i < v->n; i++)
        if (wieq(((Program *)v->items[i])->name, name)) return v->items[i];
    return NULL;
}

static Leftover *find_item(LeftList *l, LeftKind kind, const WCHAR *path, const WCHAR *value) {
    for (size_t i = 0; i < l->items.n; i++) {
        Leftover *it = l->items.items[i];
        if (it->kind == kind && wieq(it->path, path) && (!value || (it->value && wieq(it->value, value)))) return it;
    }
    return NULL;
}

static bool any_path_contains(LeftList *l, const WCHAR *needle) {
    for (size_t i = 0; i < l->items.n; i++) {
        Leftover *it = l->items.items[i];
        if (wifind(it->path, needle) || (it->value && wifind(it->value, needle))) return true;
    }
    return false;
}

static void dump(LeftList *l) {
    for (size_t i = 0; i < l->items.n; i++) {
        Leftover *it = l->items.items[i];
        char *p = u8(it->path), *v = u8(it->value ? it->value : u"");
        fprintf(stderr, "    [%d conf=%d %s] %s %s\n", it->kind, it->conf, it->checked ? "x" : " ", p, v);
        xfree(p), xfree(v);
    }
}

// ---------------------------------------------------------------- testes

static void test_programs_load(void) {
    base_system();
    firefox_installed();
    // oculto (componente do sistema), atualização, duplicado 32/64, MSI com data
    fake_str(UNINST u"Hidden", u"DisplayName", u"Componente Oculto");
    fake_dword(UNINST u"Hidden", u"SystemComponent", 1);
    fake_str(UNINST u"KB123", u"DisplayName", u"Atualização KB123");
    fake_str(UNINST u"KB123", u"ParentKeyName", u"Office");
    fake_str(UNINST u"{23170F69-40C1-2702-2301-000001000000}", u"DisplayName", u"7-Zip 23.01 (x64 edition)");
    fake_str(UNINST u"{23170F69-40C1-2702-2301-000001000000}", u"UninstallString", u"MsiExec.exe /I{23170F69-40C1-2702-2301-000001000000}");
    fake_str(UNINST u"{23170F69-40C1-2702-2301-000001000000}", u"InstallDate", u"20240115");
    fake_dword(UNINST u"{23170F69-40C1-2702-2301-000001000000}", u"WindowsInstaller", 1);
    fake_str(UNINST32 u"Dup", u"DisplayName", u"Mozilla Thunderbird (x64 pt-BR)");
    fake_str(UNINST32 u"Dup", u"Publisher", u"Mozilla");
    fake_str(UNINST32 u"Dup", u"UninstallString", u"\"C:\\Program Files\\Mozilla Thunderbird\\uninstall\\helper.exe\"");
    fake_str(u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Code", u"DisplayName", u"Visual Studio Code (User)");
    fake_str(u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Code", u"UninstallString",
             u"\"%LOCALAPPDATA%\\Programs\\Microsoft VS Code\\unins000.exe\"");

    PtrVec v = programs_load();
    CHECK(v.n == 5);  // Firefox, Thunderbird, Maintenance Service, 7-Zip, VS Code
    CHECK(find_program(&v, u"Componente Oculto") == NULL);
    CHECK(find_program(&v, u"Atualização KB123") == NULL);
    Program *z = find_program(&v, u"7-Zip 23.01 (x64 edition)");
    CHECK(z && z->msi && z->year == 2024 && z->month == 1 && z->day == 15);
    Program *ff = find_program(&v, u"Mozilla Firefox (x64 pt-BR)");
    CHECK(ff && ff->size == 250000ULL * 1024 && ff->scope == SCOPE_MACHINE64 && ff->year == 2026);
    Program *code = find_program(&v, u"Visual Studio Code (User)");
    CHECK(code && code->scope == SCOPE_USER);
    // ordenados por nome
    bool sorted = true;
    for (size_t i = 1; i < v.n; i++) sorted = sorted && wicmp(((Program *)v.items[i - 1])->name, ((Program *)v.items[i])->name) <= 0;
    CHECK(sorted);

    // comandos de desinstalação
    bool q;
    WCHAR *c = uninstall_command(z, false, &q);
    CHECK_WEQ(c, u"\"C:\\Windows\\System32\\msiexec.exe\" /X{23170F69-40C1-2702-2301-000001000000}");
    CHECK(!q);
    xfree(c);
    c = uninstall_command(z, true, &q);
    CHECK_WEQ(c, u"\"C:\\Windows\\System32\\msiexec.exe\" /X{23170F69-40C1-2702-2301-000001000000} /passive /norestart");
    CHECK(q);
    xfree(c);
    c = uninstall_command(code, true, &q);
    CHECK_WEQ(c, u"\"C:\\Users\\ana\\AppData\\Local\\Programs\\Microsoft VS Code\\unins000.exe\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART");
    xfree(c);
    // Inno Setup com QuietUninstallString /SILENT (que ainda faz perguntas): vale o tipo do executável
    fake_str(u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Code", u"QuietUninstallString",
             u"\"%LOCALAPPDATA%\\Programs\\Microsoft VS Code\\unins000.exe\" /SILENT");
    fake_file(u"C:\\Users\\ana\\AppData\\Local\\Programs\\Microsoft VS Code\\unins000.exe", "MZ..Inno Setup Uninstall", 24);
    {
        PtrVec v2 = programs_load();
        c = uninstall_command(find_program(&v2, u"Visual Studio Code (User)"), true, &q);
        CHECK_WEQ(c, u"\"C:\\Users\\ana\\AppData\\Local\\Programs\\Microsoft VS Code\\unins000.exe\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART");
        CHECK(q);
        xfree(c);
        programs_free(&v2);
    }
    c = uninstall_command(ff, true, &q);  // desinstalador desconhecido: o normal
    CHECK_WEQ(c, u"\"C:\\Program Files\\Mozilla Firefox\\uninstall\\helper.exe\"");
    CHECK(!q);
    xfree(c);
    // NSIS reconhecido pelo próprio executável: /S
    fake_file(u"C:\\Program Files\\Mozilla Firefox\\uninstall\\helper.exe", "MZ....<assemblyIdentity name=\"Nullsoft.NSIS.exehead\"", 44);
    c = uninstall_command(ff, true, &q);
    CHECK_WEQ(c, u"\"C:\\Program Files\\Mozilla Firefox\\uninstall\\helper.exe\" /S");
    CHECK(q);
    xfree(c);
    c = uninstall_command(ff, false, &q);  // desinstalação normal: nunca silenciosa
    CHECK_WEQ(c, u"\"C:\\Program Files\\Mozilla Firefox\\uninstall\\helper.exe\"");
    xfree(c);

    // identidade: pasta de instalação e o executável do ícone
    Identity *id = program_identity(ff);
    CHECK(id->dirs.n == 1 && weq(id->dirs.items[0], u"C:\\Program Files\\Mozilla Firefox"));
    identity_free(id);
    Identity *idc = program_identity(code);
    CHECK(idc->dirs.n == 1 && weq(idc->dirs.items[0], u"C:\\Users\\ana\\AppData\\Local\\Programs\\Microsoft VS Code"));
    identity_free(idc);
    CHECK(programs_find_by_exe(&v, u"C:\\Program Files\\Mozilla Firefox\\firefox.exe") == ff);
    CHECK(programs_find_by_exe(&v, u"C:\\Windows\\notepad.exe") == NULL);
    programs_free(&v);
}

static void scan_firefox(ScanMode mode, LeftList *out, Identity **id_out) {
    base_system();
    firefox_installed();
    PtrVec v = programs_load();
    Program *ff = find_program(&v, u"Mozilla Firefox (x64 pt-BR)");
    Identity *id = program_identity(ff);  // lida ANTES de desinstalar, como no programa
    WCHAR *key = wdup(ff->key_path);
    programs_free(&v);
    firefox_uninstaller_ran();
    mem_set(out, 0, sizeof(*out));
    scan_leftovers(out, id, key, mode, NULL);
    left_finalize(out);
    xfree(key);
    *id_out = id;
}

static void test_scan_safe(void) {
    LeftList l;
    Identity *id;
    scan_firefox(SCAN_SAFE, &l, &id);
    CHECK(find_item(&l, LK_DIR, u"C:\\Program Files\\Mozilla Firefox", NULL) != NULL);
    CHECK(find_item(&l, LK_KEY, u"HKLM\\SOFTWARE\\Mozilla\\Mozilla Firefox", NULL) != NULL);
    CHECK(find_item(&l, LK_KEY, u"HKLM\\SOFTWARE\\Mozilla\\Firefox", NULL) != NULL);
    CHECK(find_item(&l, LK_KEY_IF_EMPTY, u"HKLM\\SOFTWARE\\Mozilla", NULL) != NULL);
    CHECK(find_item(&l, LK_KEY, u"HKCU\\Software\\Mozilla\\Firefox", NULL) != NULL);
    CHECK(find_item(&l, LK_DIR, u"C:\\Users\\ana\\AppData\\Roaming\\Mozilla\\Firefox", NULL) != NULL);
    CHECK(find_item(&l, LK_DIR_IF_EMPTY, u"C:\\Users\\ana\\AppData\\Roaming\\Mozilla", NULL) != NULL);
    CHECK(find_item(&l, LK_DIR, u"C:\\Users\\ana\\AppData\\Local\\Mozilla\\Firefox", NULL) != NULL);
    // nada do Thunderbird, do serviço de manutenção, do mozilla.org nem de nomes genéricos
    CHECK(!any_path_contains(&l, u"Thunderbird"));
    CHECK(!any_path_contains(&l, u"Maintenance"));
    CHECK(!any_path_contains(&l, u"mozilla.org"));
    CHECK(!any_path_contains(&l, u"FireApp"));
    CHECK(!any_path_contains(&l, u"\\Update"));
    // modo seguro não olha inicialização, atalhos nem nomes parecidos
    CHECK(!any_path_contains(&l, u"\\Run"));
    CHECK(!any_path_contains(&l, u".lnk"));
    CHECK(!any_path_contains(&l, u"FirefoxSync"));
    for (size_t i = 0; i < l.items.n; i++) CHECK(((Leftover *)l.items.items[i])->conf == CONF_HIGH || ((Leftover *)l.items.items[i])->kind == LK_KEY_IF_EMPTY ||
                                                ((Leftover *)l.items.items[i])->kind == LK_DIR_IF_EMPTY);
    if (g_fail) dump(&l);
    left_free(&l);
    identity_free(id);
}

static void test_scan_moderate_and_delete(void) {
    LeftList l;
    Identity *id;
    scan_firefox(SCAN_MODERATE, &l, &id);
    Leftover *run = find_item(&l, LK_VALUE, RUN_HKCU, u"Firefox Background");
    CHECK(run && run->conf == CONF_HIGH && run->checked);
    CHECK(find_item(&l, LK_VALUE, APPROVED_HKCU, u"Firefox Background") != NULL);
    CHECK(find_item(&l, LK_KEY, u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\firefox.exe", NULL) != NULL);
    CHECK(find_item(&l, LK_VALUE, u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\SharedDLLs", u"C:\\Program Files\\Mozilla Firefox\\mozglue.dll") != NULL);
    CHECK(find_item(&l, LK_FILE, START_MENU u"\\Firefox.lnk", NULL) != NULL);
    CHECK(find_item(&l, LK_FILE, u"C:\\Users\\Public\\Desktop\\Firefox.lnk", NULL) != NULL);
    Leftover *sync = find_item(&l, LK_DIR, u"C:\\ProgramData\\FirefoxSync", NULL);
    CHECK(sync && sync->conf == CONF_MEDIUM && !sync->checked);  // parecido: entra desmarcado
    CHECK(!any_path_contains(&l, u"Thunderbird"));
    CHECK(!any_path_contains(&l, u"OneDrive"));
    CHECK(!any_path_contains(&l, u"MozillaMaintenance"));
    Leftover *html = find_item(&l, LK_KEY, u"HKLM\\SOFTWARE\\Classes\\FirefoxHTML-308046B0AF4A39CB", NULL);
    CHECK(html && html->conf == CONF_HIGH && html->checked);  // tipo de arquivo que abre o firefox.exe
    if (g_fail) dump(&l);

    DelResult r;
    delete_leftovers(&l, u"Mozilla Firefox", NULL, &r);
    CHECK(!r.backup_failed && r.backup_path);
    CHECK(r.failed == 0);
    // Registro
    CHECK(!fake_key_exists(u"HKLM\\SOFTWARE\\Mozilla\\Mozilla Firefox"));
    CHECK(!fake_key_exists(u"HKLM\\SOFTWARE\\Mozilla\\Firefox"));
    CHECK(fake_key_exists(u"HKLM\\SOFTWARE\\Mozilla\\Mozilla Thunderbird\\128.0\\Main"));  // o vizinho fica
    CHECK(fake_key_exists(u"HKLM\\SOFTWARE\\Mozilla"));                                    // e a pasta do editor também
    CHECK(!fake_key_exists(u"HKCU\\Software\\Mozilla"));                                   // essa ficou vazia: sai
    CHECK(!fake_val_exists(RUN_HKCU, u"Firefox Background"));
    CHECK(fake_val_exists(RUN_HKCU, u"OneDrive"));
    CHECK(!fake_val_exists(APPROVED_HKCU, u"Firefox Background"));
    CHECK(!fake_key_exists(u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\firefox.exe"));
    CHECK(fake_key_exists(u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths"));
    CHECK(fake_key_exists(UNINST u"Mozilla Thunderbird 128.0 (x64 pt-BR)"));
    CHECK(fake_key_exists(u"HKLM\\SYSTEM\\CurrentControlSet\\Services\\MozillaMaintenance"));
    CHECK(fake_key_exists(u"HKLM\\SOFTWARE\\mozilla.org\\Mozilla"));
    // Disco
    CHECK(!fake_exists(u"C:\\Program Files\\Mozilla Firefox"));
    CHECK(fake_exists(u"C:\\Program Files\\Mozilla Thunderbird\\thunderbird.exe"));
    CHECK(!fake_exists(u"C:\\Users\\ana\\AppData\\Roaming\\Mozilla\\Firefox"));
    CHECK(fake_exists(u"C:\\Users\\ana\\AppData\\Roaming\\Mozilla\\Extensions"));  // pasta do editor com outra coisa: fica
    CHECK(!fake_exists(u"C:\\Users\\ana\\AppData\\Local\\Mozilla"));               // ficou vazia: sai
    CHECK(fake_exists(u"C:\\Users\\ana\\AppData\\Roaming\\Thunderbird\\Profiles\\x\\prefs.js"));
    CHECK(!fake_exists(START_MENU u"\\Firefox.lnk"));
    CHECK(fake_exists(u"C:\\Users\\Public\\Desktop\\Thunderbird.lnk"));
    CHECK(fake_exists(u"C:\\ProgramData\\FirefoxSync\\state.json"));  // desmarcado: não mexeu
    CHECK(fake_recycled_count() >= 4);
    // o backup tem o que saiu do Registro e restaura com o formato do regedit
    uint8_t *data;
    size_t len;
    CHECK(fake_read(r.backup_path, &data, &len));
    if (len > 2) {
        CHECK(data[0] == 0xFF && data[1] == 0xFE);  // UTF-16LE com BOM
        WCHAR *text = wdupn((const WCHAR *)(data + 2), (len - 2) / 2);
        CHECK(wistarts(text, u"Windows Registry Editor Version 5.00\r\n"));
        CHECK(wifind(text, u"[HKEY_LOCAL_MACHINE\\SOFTWARE\\Mozilla\\Mozilla Firefox\\131.0 (x64 pt-BR)\\Main]") != NULL);
        CHECK(wifind(text, u"\"PathToExe\"=\"C:\\\\Program Files\\\\Mozilla Firefox\\\\firefox.exe\"") != NULL);
        CHECK(wifind(text, u"\"Firefox Background\"=\"\\\"C:\\\\Program Files\\\\Mozilla Firefox\\\\firefox.exe\\\" -background\"") != NULL);
        CHECK(wifind(text, u"\"Firefox Background\"=hex:02,00,00,00") != NULL);
        CHECK(wifind(text, u"[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\firefox.exe]") != NULL);
        CHECK(!wifind(text, u"Thunderbird"));
        xfree(text);
    }
    // o log registrou as exclusões
    CHECK(fake_read(u"C:\\Users\\ana\\AppData\\Local\\EXTIRPA\\extirpa.log", &data, &len) && len > 0);
    del_result_free(&r);
    left_free(&l);
    identity_free(id);
}

static void test_scan_advanced(void) {
    LeftList l;
    Identity *id;
    scan_firefox(SCAN_ADVANCED, &l, &id);
    Leftover *progid = find_item(&l, LK_KEY, u"HKLM\\SOFTWARE\\Classes\\FirefoxHTML-308046B0AF4A39CB", NULL);
    CHECK(progid && progid->conf == CONF_HIGH && progid->checked);
    CHECK(find_item(&l, LK_KEY, u"HKLM\\SOFTWARE\\Classes\\CLSID\\{0D68D6D0-D93D-4D08-A30D-F00DD1F45B24}", NULL) != NULL);
    CHECK(!any_path_contains(&l, u"Thunderbird"));
    CHECK(!any_path_contains(&l, u".txt"));
    if (g_fail) dump(&l);
    left_free(&l);
    identity_free(id);
}

static void test_shared_install_dir(void) {
    base_system();
    // um programa registrou a pasta do editor como pasta de instalação; outro mora dentro dela
    fake_str(UNINST u"VendorTool", u"DisplayName", u"Vendor Tool");
    fake_str(UNINST u"VendorTool", u"Publisher", u"Acme Corp");
    fake_str(UNINST u"VendorTool", u"InstallLocation", u"C:\\Program Files\\Acme");
    fake_str(UNINST u"VendorOther", u"DisplayName", u"Acme Other Suite");
    fake_str(UNINST u"VendorOther", u"InstallLocation", u"C:\\Program Files\\Acme\\Other Suite");
    fake_file(u"C:\\Program Files\\Acme\\Other Suite\\other.exe", "MZ", 2);
    fake_file(u"C:\\Program Files\\Acme\\tool.exe", "MZ", 2);
    PtrVec v = programs_load();
    Program *p = find_program(&v, u"Vendor Tool");
    Identity *id = program_identity(p);
    WCHAR *key = wdup(p->key_path);
    programs_free(&v);
    LeftList l = {0};
    scan_leftovers(&l, id, key, SCAN_MODERATE, NULL);
    left_finalize(&l);
    // a pasta não sai inteira (levaria o outro programa junto): sai item por item, menos o outro
    CHECK(find_item(&l, LK_DIR, u"C:\\Program Files\\Acme", NULL) == NULL);
    Leftover *d = find_item(&l, LK_DIR_IF_EMPTY, u"C:\\Program Files\\Acme", NULL);
    CHECK(d && d->checked);
    Leftover *tool = find_item(&l, LK_FILE, u"C:\\Program Files\\Acme\\tool.exe", NULL);
    CHECK(tool && tool->conf == CONF_HIGH && tool->checked);
    CHECK(!any_path_contains(&l, u"Other Suite"));
    DelResult r;
    delete_leftovers(&l, u"Vendor Tool", NULL, &r);
    CHECK(r.failed == 0 && r.kept == 1);
    CHECK(fake_exists(u"C:\\Program Files\\Acme\\Other Suite\\other.exe"));
    CHECK(!fake_exists(u"C:\\Program Files\\Acme\\tool.exe"));
    del_result_free(&r);
    if (g_fail) dump(&l);
    left_free(&l);
    identity_free(id);
    xfree(key);
}

static void test_microsoft_program(void) {
    base_system();
    fake_str(UNINST u"Teams", u"DisplayName", u"Microsoft Teams");
    fake_str(UNINST u"Teams", u"Publisher", u"Microsoft Corporation");
    fake_file(u"C:\\Users\\ana\\AppData\\Local\\Microsoft\\Teams\\current\\Teams.exe", "MZ", 2);
    fake_file(u"C:\\Users\\ana\\AppData\\Local\\Microsoft\\Edge\\x", "x", 1);
    fake_str(u"HKCU\\Software\\Microsoft\\Teams", u"x", u"1");
    PtrVec v = programs_load();
    Program *p = find_program(&v, u"Microsoft Teams");
    Identity *id = program_identity(p);
    WCHAR *key = wdup(p->key_path);
    programs_free(&v);
    LeftList l = {0};
    scan_leftovers(&l, id, key, SCAN_ADVANCED, NULL);
    xfree(key);
    left_finalize(&l);
    CHECK(find_item(&l, LK_DIR, u"C:\\Users\\ana\\AppData\\Local\\Microsoft\\Teams", NULL) != NULL);
    CHECK(find_item(&l, LK_KEY, u"HKCU\\Software\\Microsoft\\Teams", NULL) != NULL);
    CHECK(!any_path_contains(&l, u"Edge"));
    CHECK(!any_path_contains(&l, u"Office"));
    CHECK(!find_item(&l, LK_DIR, u"C:\\Users\\ana\\AppData\\Local\\Microsoft", NULL));
    if (g_fail) dump(&l);
    left_free(&l);
    identity_free(id);
}

static void test_protection_end_to_end(void) {
    base_system();
    fake_file(u"C:\\Program Files\\x.txt", "x", 1);
    // itens perigosos enfiados direto na lista (sem passar por left_add): a exclusão recusa
    LeftList l = {0};
    const WCHAR *paths[] = {u"C:\\Program Files", u"C:\\Users\\ana\\AppData\\Roaming", u"C:\\Windows\\System32"};
    for (size_t i = 0; i < COUNT(paths); i++) {
        Leftover *it = xalloc(sizeof(Leftover));
        it->kind = LK_DIR;
        it->path = wdup(paths[i]);
        it->checked = true;
        it->conf = CONF_HIGH;
        pv_push(&l.items, it);
    }
    Leftover *k = xalloc(sizeof(Leftover));
    k->kind = LK_KEY;
    k->path = wdup(u"HKLM\\SOFTWARE\\Microsoft");
    k->checked = true;
    pv_push(&l.items, k);
    DelResult r;
    delete_leftovers(&l, u"teste", NULL, &r);
    CHECK(r.deleted == 0 && r.failed == 4);
    CHECK(fake_exists(u"C:\\Program Files\\x.txt"));
    CHECK(fake_exists(u"C:\\Windows\\System32"));
    CHECK(fake_key_exists(u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run"));
    CHECK(fake_recycled_count() == 0);
    del_result_free(&r);
    left_free(&l);
    // e left_add nem aceita
    LeftList m = {0};
    CHECK(!left_add(&m, LK_DIR, u"C:\\Users", NULL, CONF_HIGH, LR_NAME, NULL));
    CHECK(!left_add(&m, LK_DIR, u"C:\\Users\\ana\\AppData\\Local\\EXTIRPA\\Backups", NULL, CONF_HIGH, LR_NAME, NULL));  // os próprios backups
    CHECK(!left_add(&m, LK_FILE, u"C:\\Tools\\extirpa.exe", NULL, CONF_HIGH, LR_NAME, NULL));  // o próprio programa
    left_free(&m);
}

static void test_registry_helpers(void) {
    base_system();
    fake_str(u"HKCU\\Software\\Teste\\A", u"s", u"texto");
    fake_dword(u"HKCU\\Software\\Teste\\A\\B", u"d", 7);
    StrBuf b = {0};
    CHECK(reg_export_key(&b, u"HKCU\\Software\\Teste"));
    CHECK_WEQ(sb_str(&b), u"\r\n[HKEY_CURRENT_USER\\Software\\Teste]\r\n"
                          u"\r\n[HKEY_CURRENT_USER\\Software\\Teste\\A]\r\n\"s\"=\"texto\"\r\n"
                          u"\r\n[HKEY_CURRENT_USER\\Software\\Teste\\A\\B]\r\n\"d\"=dword:00000007\r\n");
    sb_free(&b);
    CHECK(!reg_key_empty(u"HKCU\\Software\\Teste"));
    CHECK(reg_delete_key(u"HKCU\\Software\\Teste") == ERROR_SUCCESS);
    CHECK(!fake_key_exists(u"HKCU\\Software\\Teste"));
    CHECK(reg_delete_key(u"HKCU\\Software\\NaoExiste") == ERROR_SUCCESS);
    CHECK(reg_delete_value(u"HKCU\\Software\\NaoExiste", u"x") == ERROR_SUCCESS);
    fake_key(u"HKCU\\Software\\Vazia");
    CHECK(reg_key_empty(u"HKCU\\Software\\Vazia"));
}

static void test_startup(void) {
    base_system();
    fake_str(RUN_HKCU, u"Discord", u"\"C:\\Users\\ana\\AppData\\Local\\Discord\\Update.exe\" --processStart Discord.exe");
    fake_str(u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce", u"Limpeza", u"cmd /c del x");
    fake_str(u"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run", u"Velho32", u"C:\\Velho\\v.exe");
    uint8_t off[12] = {3};
    fake_val(u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run32", u"Velho32", REG_BINARY, off, 12);
    make_lnk(u"C:\\Users\\ana\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\Notas.lnk", u"C:\\Notas\\notas.exe");
    PtrVec v = startup_list();
    CHECK(v.n == 4);
    StartupEntry *discord = NULL, *once = NULL, *old = NULL, *lnk = NULL;
    for (size_t i = 0; i < v.n; i++) {
        StartupEntry *e = v.items[i];
        if (weq(e->name, u"Discord")) discord = e;
        if (weq(e->name, u"Limpeza")) once = e;
        if (weq(e->name, u"Velho32")) old = e;
        if (weq(e->name, u"Notas.lnk")) lnk = e;
    }
    CHECK(discord && discord->enabled && discord->can_toggle && discord->source == SRC_HKCU_RUN);
    CHECK(once && !once->can_toggle);
    CHECK(old && !old->enabled && old->source == SRC_HKLM32_RUN);
    CHECK(lnk && weq(lnk->command, u"C:\\Notas\\notas.exe") && lnk->source == SRC_USER_FOLDER);
    WCHAR *exe = startup_exe(discord);
    CHECK_WEQ(exe, u"C:\\Users\\ana\\AppData\\Local\\Discord\\Update.exe");
    xfree(exe);

    CHECK(startup_set_enabled(discord, false) == ERROR_SUCCESS);
    HKEY k = reg_open(APPROVED_HKCU, KEY_READ);
    uint8_t data[16];
    DWORD type, size = sizeof(data);
    CHECK(k && RegQueryValueExW(k, u"Discord", NULL, &type, data, &size) == ERROR_SUCCESS && size == 12 && data[0] == 3);
    CHECK(startup_set_enabled(discord, true) == ERROR_SUCCESS);
    size = sizeof(data);
    CHECK(RegQueryValueExW(k, u"Discord", NULL, &type, data, &size) == ERROR_SUCCESS && data[0] == 2);

    WCHAR *bk = NULL;
    CHECK(startup_delete(discord, &bk) == ERROR_SUCCESS);
    CHECK(!fake_val_exists(RUN_HKCU, u"Discord"));
    CHECK(!fake_val_exists(APPROVED_HKCU, u"Discord"));
    CHECK(bk && fake_exists(bk));
    xfree(bk);
    CHECK(startup_delete(lnk, &bk) == ERROR_SUCCESS);
    CHECK(!fake_exists(u"C:\\Users\\ana\\AppData\\Roaming\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\Notas.lnk"));
    xfree(bk);
    startup_list_free(&v);
}

static void test_monitor(void) {
    base_system();
    fake_str(u"HKLM\\SOFTWARE\\Existente", u"a", u"1");
    fake_file(u"C:\\Program Files\\Existente\\a.dll", "x", 1);
    Snapshot s = {0};
    monitor_snapshot(&s, false, NULL);
    // a "instalação"
    fake_str(u"HKLM\\SOFTWARE\\NovoApp\\Config\\Sub", u"x", u"1");
    fake_str(u"HKLM\\SOFTWARE\\Existente", u"b", u"2");
    fake_str(UNINST u"NovoApp", u"DisplayName", u"Novo App 1.0");
    fake_file(u"C:\\Program Files\\NovoApp\\bin\\novo.exe", "MZ", 2);
    fake_file(u"C:\\Program Files\\Existente\\b.dll", "x", 1);
    fake_str(u"HKLM\\SYSTEM\\CurrentControlSet\\Services\\NovoSvc", u"ImagePath", u"C:\\Program Files\\NovoApp\\bin\\novo.exe");
    fake_file(u"C:\\Users\\ana\\AppData\\Local\\Temp\\setup123\\x.tmp", "x", 1);  // pasta temporária: fora da foto
    s.out = trace_new();
    monitor_snapshot(&s, true, NULL);
    Trace *t = s.out;
    bool key = false, val = false, unk = false, dir = false, file = false, svc = false, temp = false, sub = false;
    for (size_t i = 0; i < t->items.n; i++) {
        TraceItem *it = t->items.items[i];
        if (it->kind == TR_KEY && weq(it->path, u"HKLM\\SOFTWARE\\NovoApp")) key = true;
        if (it->kind == TR_KEY && wifind(it->path, u"NovoApp\\Config")) sub = true;  // não desce em chave nova
        if (it->kind == TR_VALUE && weq(it->path, u"HKLM\\SOFTWARE\\Existente") && weq(it->value, u"b")) val = true;
        if (it->kind == TR_KEY && weq(it->path, UNINST u"NovoApp")) unk = true;
        if (it->kind == TR_DIR && weq(it->path, u"C:\\Program Files\\NovoApp")) dir = true;
        if (it->kind == TR_FILE && weq(it->path, u"C:\\Program Files\\Existente\\b.dll")) file = true;
        if (it->kind == TR_KEY && weq(it->path, u"HKLM\\SYSTEM\\CurrentControlSet\\Services\\NovoSvc")) svc = true;
        if (wifind(it->path, u"setup123")) temp = true;
    }
    CHECK(key && val && unk && dir && file && svc && !temp && !sub);
    CHECK(t->items.n == 6);
    WCHAR *name = trace_guess_name(t);
    CHECK_WEQ(name, u"Novo App 1.0");
    xfree(name);
    // salvar e ler de volta
    t->name = wdup(u"Novo App 1.0");
    t->installer = wdup(u"C:\\Downloads\\novo-setup.exe");
    t->date = wdup(u"2026-10-18 12:00");
    WCHAR *path = NULL;
    CHECK(trace_save(t, &path));
    PtrVec list = traces_list();
    CHECK(list.n == 1 && ((TraceFile *)list.items[0])->items == 6);
    traces_list_free(&list);
    // vira lista de sobras com o serviço
    LeftList l = {0};
    left_add_trace(&l, t);
    left_finalize(&l);
    CHECK(find_item(&l, LK_SERVICE, u"NovoSvc", NULL) != NULL);
    CHECK(find_item(&l, LK_DIR, u"C:\\Program Files\\NovoApp", NULL) != NULL);
    left_free(&l);
    xfree(path);
    hs_free(&s.before);
    trace_free(t);
}


// ---------------------------------------------------------------- limpeza completa (todas as fontes)

#define SE_DIR u"C:\\Program Files\\Nimbus\\Super Editor"
#define SE_KEY UNINST u"SuperEditor_is1"
#define UP_DIR u"C:\\Program Files\\Nimbus\\Updater"
#define FW_KEY u"HKLM\\SYSTEM\\CurrentControlSet\\Services\\SharedAccess\\Parameters\\FirewallPolicy\\FirewallRules"
#define MUI u"HKCU\\Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\Shell\\MuiCache"

static void task_file(const WCHAR *name, const WCHAR *command) {
    StrBuf b = {0};
    sb_addc(&b, 0xFEFF);
    sb_add(&b, u"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n<Task><RegistrationInfo><URI>");
    sb_add(&b, name);
    sb_add(&b, u"</URI></RegistrationInfo><Actions Context=\"Author\"><Exec><Command>");
    sb_add(&b, command);
    sb_add(&b, u"</Command><Arguments>/silent &amp; /x</Arguments></Exec></Actions></Task>");
    WCHAR *path = wcat(u"C:\\Windows\\System32\\Tasks", name);
    fake_file(path, sb_str(&b), b.len * sizeof(WCHAR));
    xfree(path);
    sb_free(&b);
}

static void super_editor_installed(void) {
    fake_str(SE_KEY, u"DisplayName", u"Super Editor 4.2.1");
    fake_str(SE_KEY, u"DisplayVersion", u"4.2.1");
    fake_str(SE_KEY, u"Publisher", u"Nimbus Software Ltd.");
    fake_str(SE_KEY, u"UninstallString", u"\"" SE_DIR u"\\uninstall\\unins000.exe\"");  // sem InstallLocation
    fake_str(UNINST u"NimbusUpdater", u"DisplayName", u"Nimbus Updater");
    fake_str(UNINST u"NimbusUpdater", u"Publisher", u"Nimbus Software Ltd.");
    fake_str(UNINST u"NimbusUpdater", u"InstallLocation", UP_DIR);
    // arquivos
    fake_file(SE_DIR u"\\se.exe", "MZ", 2);
    fake_file(SE_DIR u"\\shellext.dll", "MZ", 2);
    fake_file(SE_DIR u"\\bin\\se-cli.exe", "MZ", 2);
    fake_file(SE_DIR u"\\uninstall\\unins000.exe", "MZ", 2);
    fake_short_name(SE_DIR, u"C:\\PROGRA~1\\Nimbus\\SUPERE~1");
    fake_file(UP_DIR u"\\nu.exe", "MZ", 2);
    fake_file(u"C:\\Users\\ana\\AppData\\Roaming\\Nimbus\\Super Editor\\settings.json", "{}", 2);
    fake_file(u"C:\\Users\\ana\\AppData\\Roaming\\Nimbus\\Updater\\state.json", "{}", 2);
    fake_file(u"C:\\Users\\ana\\AppData\\Local\\SuperEditor\\cache\\a.bin", "x", 1);
    make_lnk(u"C:\\Users\\ana\\Desktop\\Super Editor.lnk", SE_DIR u"\\se.exe");
    make_lnk(START_MENU u"\\Nimbus\\Super Editor.lnk", SE_DIR u"\\se.exe");
    make_lnk(START_MENU u"\\Nimbus\\Nimbus Updater.lnk", UP_DIR u"\\nu.exe");
    make_lnk(u"C:\\Users\\ana\\AppData\\Roaming\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar\\Super Editor.lnk", SE_DIR u"\\se.exe");
    // Registro: configurações
    fake_str(u"HKCU\\Software\\Nimbus\\Super Editor\\Prefs", u"Theme", u"dark");
    fake_str(u"HKCU\\Software\\Nimbus\\Updater", u"Channel", u"stable");
    fake_str(u"HKLM\\SOFTWARE\\Nimbus\\Super Editor\\Capabilities", u"ApplicationName", u"Super Editor");
    fake_str(u"HKLM\\SOFTWARE\\RegisteredApplications", u"Super Editor", u"Software\\Nimbus\\Super Editor\\Capabilities");
    fake_str(u"HKLM\\SOFTWARE\\RegisteredApplications", u"Firefox", u"Software\\Clients\\StartMenuInternet\\Firefox\\Capabilities");
    // tipos de arquivo, COM, menus do Explorer
    fake_str(u"HKLM\\SOFTWARE\\Classes\\SuperEditor.Document\\shell\\open\\command", u"", u"\"" SE_DIR u"\\se.exe\" \"%1\"");
    fake_str(u"HKCU\\Software\\Classes\\.sed", u"", u"SuperEditor.Document");
    fake_str(u"HKCU\\Software\\Classes\\.txt\\OpenWithProgids", u"SuperEditor.Document", u"");
    fake_str(u"HKCU\\Software\\Classes\\.txt\\OpenWithProgids", u"txtfile", u"");
    fake_str(u"HKLM\\SOFTWARE\\Classes\\CLSID\\{11111111-2222-3333-4444-555555555555}\\InprocServer32", u"", u"C:\\PROGRA~1\\Nimbus\\SUPERE~1\\shellext.dll");
    fake_str(u"HKLM\\SOFTWARE\\Classes\\*\\shellex\\ContextMenuHandlers\\SuperEditor", u"", u"{11111111-2222-3333-4444-555555555555}");
    fake_str(u"HKLM\\SOFTWARE\\Classes\\*\\shellex\\ContextMenuHandlers\\Outro", u"", u"{99999999-2222-3333-4444-555555555555}");
    fake_str(u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved", u"{11111111-2222-3333-4444-555555555555}", u"Super Editor Shell");
    fake_str(u"HKLM\\SOFTWARE\\Classes\\Directory\\shell\\SuperEditor\\command", u"", u"\"" SE_DIR u"\\se.exe\" \"%V\"");
    fake_str(u"HKLM\\SOFTWARE\\Classes\\Directory\\shell\\cmd\\command", u"", u"cmd.exe /s /k pushd \"%V\"");
    fake_str(u"HKLM\\SOFTWARE\\Classes\\TypeLib\\{22222222-2222-3333-4444-555555555555}\\1.0\\0\\win64", u"", SE_DIR u"\\shellext.dll");
    // inicialização, tarefas, firewall, PATH, rastros de uso, serviço
    fake_str(u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", u"SuperEditorTray", u"\"" SE_DIR u"\\se.exe\" --tray");
    task_file(u"\\Nimbus\\SuperEditorUpdate", u"%ProgramFiles%\\Nimbus\\Super Editor\\se.exe");
    task_file(u"\\NimbusUpdater", UP_DIR u"\\nu.exe");
    task_file(u"\\Microsoft\\Windows\\Fake\\Task", SE_DIR u"\\se.exe");  // do Windows: nunca
    fake_str(FW_KEY, u"{AAAA-SE}", u"v2.30|Action=Allow|Active=TRUE|Dir=In|App=%ProgramFiles%\\Nimbus\\Super Editor\\se.exe|Name=Super Editor|");
    fake_str(FW_KEY, u"TCP Query User{B}C:\\program files\\nimbus\\super editor\\se.exe", u"v2.30|Action=Block|Dir=In|App=C:\\program files\\nimbus\\super editor\\se.exe|");
    // nome com aspas simples e tipográficas: não pode quebrar o literal do script do PowerShell
    fake_str(FW_KEY, u"Ana's \u2019rule\u2019", u"v2.30|Action=Allow|Dir=In|App=" SE_DIR u"\\se.exe|");
    fake_str(FW_KEY, u"{BBBB-UP}", u"v2.30|Action=Allow|Dir=Out|App=" UP_DIR u"\\nu.exe|Name=Updater|");
    const WCHAR path_val[] = u"C:\\Users\\ana\\bin;%ProgramFiles%\\Nimbus\\Super Editor\\bin;" UP_DIR u";";
    fake_val(u"HKCU\\Environment", u"Path", REG_EXPAND_SZ, path_val, sizeof(path_val));
    fake_str(MUI, SE_DIR u"\\se.exe.FriendlyAppName", u"Super Editor");
    fake_str(MUI, UP_DIR u"\\nu.exe.FriendlyAppName", u"Updater");
    fake_str(u"HKLM\\SYSTEM\\CurrentControlSet\\Services\\SuperEditorSvc", u"ImagePath", u"\"" SE_DIR u"\\se.exe\" --service");
    // um programa qualquer que lembra um arquivo aberto dentro da pasta (só o modo avançado acha)
    fake_str(u"HKCU\\Software\\OutroApp\\Recent", u"File1", SE_DIR u"\\readme.txt");
}

static void test_full_cleanup(void) {
    base_system();
    super_editor_installed();
    PtrVec v = programs_load();
    Program *p = find_program(&v, u"Super Editor 4.2.1");
    CHECK(p != NULL);
    if (!p) return;
    Identity *id = program_identity(p);  // antes de desinstalar
    CHECK(id->dirs.n == 1 && weq(id->dirs.items[0], SE_DIR));  // a pasta saiu do desinstalador em \uninstall
    CHECK(id->alt_dirs.n == 1);
    WCHAR *key = wdup(p->key_path);
    programs_free(&v);
    // o desinstalador do programa tira a entrada e alguns arquivos, e deixa o resto
    HKEY u;
    RegOpenKeyExW(HKEY_LOCAL_MACHINE, u"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, 0, &u);
    RegDeleteTreeW(u, u"SuperEditor_is1");
    DeleteFileW(SE_DIR u"\\uninstall\\unins000.exe");

    LeftList l = {0};
    scan_leftovers(&l, id, key, SCAN_MODERATE, NULL);
    left_finalize(&l);
    struct {
        LeftKind kind;
        const WCHAR *path, *value;
    } expect[] = {
        {LK_DIR, SE_DIR, NULL},
        {LK_DIR, u"C:\\Users\\ana\\AppData\\Roaming\\Nimbus\\Super Editor", NULL},
        {LK_DIR, u"C:\\Users\\ana\\AppData\\Local\\SuperEditor", NULL},
        {LK_FILE, u"C:\\Users\\ana\\Desktop\\Super Editor.lnk", NULL},
        {LK_FILE, START_MENU u"\\Nimbus\\Super Editor.lnk", NULL},
        {LK_FILE, u"C:\\Users\\ana\\AppData\\Roaming\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar\\Super Editor.lnk", NULL},
        {LK_KEY, u"HKCU\\Software\\Nimbus\\Super Editor", NULL},
        {LK_KEY, u"HKLM\\SOFTWARE\\Nimbus\\Super Editor", NULL},
        {LK_VALUE, u"HKLM\\SOFTWARE\\RegisteredApplications", u"Super Editor"},
        {LK_KEY, u"HKLM\\SOFTWARE\\Classes\\SuperEditor.Document", NULL},
        {LK_VALUE, u"HKCU\\Software\\Classes\\.txt\\OpenWithProgids", u"SuperEditor.Document"},
        {LK_KEY, u"HKLM\\SOFTWARE\\Classes\\CLSID\\{11111111-2222-3333-4444-555555555555}", NULL},
        {LK_KEY, u"HKLM\\SOFTWARE\\Classes\\*\\shellex\\ContextMenuHandlers\\SuperEditor", NULL},
        {LK_VALUE, u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Shell Extensions\\Approved", u"{11111111-2222-3333-4444-555555555555}"},
        {LK_KEY, u"HKLM\\SOFTWARE\\Classes\\Directory\\shell\\SuperEditor", NULL},
        {LK_KEY, u"HKLM\\SOFTWARE\\Classes\\TypeLib\\{22222222-2222-3333-4444-555555555555}", NULL},
        {LK_VALUE, u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", u"SuperEditorTray"},
        {LK_TASK, u"\\Nimbus\\SuperEditorUpdate", NULL},
        {LK_FWRULE, u"{AAAA-SE}", NULL},
        {LK_FWRULE, u"TCP Query User{B}C:\\program files\\nimbus\\super editor\\se.exe", NULL},
        {LK_FWRULE, u"Ana's \u2019rule\u2019", NULL},
        {LK_PATHENTRY, u"HKCU\\Environment", u"%ProgramFiles%\\Nimbus\\Super Editor\\bin"},
        {LK_VALUE, MUI, SE_DIR u"\\se.exe.FriendlyAppName"},
        {LK_SERVICE, u"SuperEditorSvc", NULL},
    };
    for (size_t i = 0; i < COUNT(expect); i++) {
        Leftover *it = find_item(&l, expect[i].kind, expect[i].path, expect[i].value);
        if (!it || it->conf != CONF_HIGH || !it->checked) {
            char *a = u8(expect[i].path), *b = u8(expect[i].value ? expect[i].value : u"");
            fprintf(stderr, "  esperado e marcado: %s %s\n", a, b);
            xfree(a), xfree(b);
        }
        CHECK(it && it->conf == CONF_HIGH && it->checked);
    }
    Leftover *ext = find_item(&l, LK_VALUE, u"HKCU\\Software\\Classes\\.sed", u"");
    CHECK(ext && !ext->checked);  // associação padrão: entra, mas desmarcada
    // nada do Nimbus Updater, do Windows nem de outros
    CHECK(!any_path_contains(&l, u"Updater"));
    CHECK(!any_path_contains(&l, u"{BBBB-UP}"));
    CHECK(!any_path_contains(&l, u"\\Microsoft\\Windows\\Fake"));
    CHECK(!any_path_contains(&l, u"Outro"));
    CHECK(!any_path_contains(&l, u"txtfile"));
    CHECK(!any_path_contains(&l, u"Firefox"));
    CHECK(!any_path_contains(&l, u"Directory\\shell\\cmd"));
    CHECK(!find_item(&l, LK_DIR, u"C:\\Program Files\\Nimbus", NULL));  // a pasta do editor não sai inteira
    if (g_fail) dump(&l);

    DelResult r;
    delete_leftovers(&l, u"Super Editor", NULL, &r);
    CHECK(r.failed == 0);
    // tudo do programa saiu
    CHECK(!fake_exists(SE_DIR));
    CHECK(!fake_exists(u"C:\\Users\\ana\\AppData\\Roaming\\Nimbus\\Super Editor"));
    CHECK(!fake_exists(u"C:\\Users\\ana\\AppData\\Local\\SuperEditor"));
    CHECK(!fake_exists(u"C:\\Users\\ana\\Desktop\\Super Editor.lnk"));
    CHECK(!fake_key_exists(u"HKCU\\Software\\Nimbus\\Super Editor"));
    CHECK(!fake_key_exists(u"HKLM\\SOFTWARE\\Nimbus"));  // ficou vazia: saiu
    CHECK(!fake_val_exists(u"HKLM\\SOFTWARE\\RegisteredApplications", u"Super Editor"));
    CHECK(!fake_key_exists(u"HKLM\\SOFTWARE\\Classes\\SuperEditor.Document"));
    CHECK(!fake_val_exists(u"HKCU\\Software\\Classes\\.txt\\OpenWithProgids", u"SuperEditor.Document"));
    CHECK(!fake_key_exists(u"HKLM\\SOFTWARE\\Classes\\CLSID\\{11111111-2222-3333-4444-555555555555}"));
    CHECK(!fake_key_exists(u"HKLM\\SOFTWARE\\Classes\\*\\shellex\\ContextMenuHandlers\\SuperEditor"));
    CHECK(!fake_key_exists(u"HKLM\\SOFTWARE\\Classes\\Directory\\shell\\SuperEditor"));
    CHECK(!fake_key_exists(u"HKLM\\SOFTWARE\\Classes\\TypeLib\\{22222222-2222-3333-4444-555555555555}"));
    CHECK(!fake_val_exists(u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", u"SuperEditorTray"));
    CHECK(!fake_exists(u"C:\\Windows\\System32\\Tasks\\Nimbus\\SuperEditorUpdate"));
    CHECK(!fake_val_exists(FW_KEY, u"{AAAA-SE}"));
    CHECK(!fake_val_exists(FW_KEY, u"TCP Query User{B}C:\\program files\\nimbus\\super editor\\se.exe"));
    CHECK(!fake_val_exists(FW_KEY, u"Ana's \u2019rule\u2019"));
    CHECK(!fake_val_exists(MUI, SE_DIR u"\\se.exe.FriendlyAppName"));
    CHECK(!fake_key_exists(u"HKLM\\SYSTEM\\CurrentControlSet\\Services\\SuperEditorSvc"));
    // o PATH perdeu só a entrada do programa (e manteve o tipo REG_EXPAND_SZ)
    uint8_t *data;
    DWORD type, size;
    data = reg_get_raw(u"HKCU\\Environment", u"Path", &type, &size);
    CHECK(data && type == REG_EXPAND_SZ);
    if (data) CHECK_WEQ((const WCHAR *)data, u"C:\\Users\\ana\\bin;" UP_DIR);
    xfree(data);
    // o resto continua lá
    CHECK(fake_exists(UP_DIR u"\\nu.exe"));
    CHECK(fake_exists(u"C:\\Users\\ana\\AppData\\Roaming\\Nimbus\\Updater\\state.json"));
    CHECK(fake_exists(START_MENU u"\\Nimbus\\Nimbus Updater.lnk"));
    CHECK(fake_key_exists(u"HKCU\\Software\\Nimbus\\Updater"));
    CHECK(fake_exists(u"C:\\Windows\\System32\\Tasks\\NimbusUpdater"));
    CHECK(fake_exists(u"C:\\Windows\\System32\\Tasks\\Microsoft\\Windows\\Fake\\Task"));
    CHECK(fake_val_exists(FW_KEY, u"{BBBB-UP}"));
    CHECK(fake_val_exists(MUI, UP_DIR u"\\nu.exe.FriendlyAppName"));
    CHECK(fake_val_exists(u"HKLM\\SOFTWARE\\RegisteredApplications", u"Firefox"));
    CHECK(fake_key_exists(u"HKLM\\SOFTWARE\\Classes\\*\\shellex\\ContextMenuHandlers\\Outro"));
    CHECK(fake_key_exists(u"HKLM\\SOFTWARE\\Classes\\Directory\\shell\\cmd"));
    CHECK(fake_val_exists(u"HKCU\\Software\\Classes\\.txt\\OpenWithProgids", u"txtfile"));
    CHECK(fake_val_exists(u"HKCU\\Software\\Classes\\.sed", u""));  // desmarcado: ficou
    CHECK(fake_key_exists(u"HKCU\\Software\\OutroApp\\Recent"));
    // o backup da tarefa foi gravado, e o .reg tem o PATH antigo e as regras do firewall
    bool task_backup = false;
    PtrVec bk = fs_list(u"C:\\Users\\ana\\AppData\\Local\\EXTIRPA\\Backups");
    for (size_t i = 0; i < bk.n; i++) task_backup = task_backup || wiends(((DirEntry *)bk.items[i])->name, u"task - Nimbus_SuperEditorUpdate.xml");
    fs_list_free(&bk);
    CHECK(task_backup);
    size_t len;
    CHECK(fake_read(r.backup_path, &data, &len));
    WCHAR *text = wdupn((const WCHAR *)(data + 2), (len - 2) / 2);
    CHECK(wifind(text, u"[HKEY_CURRENT_USER\\Environment]") && wifind(text, u"\"Path\"=hex(2):"));
    CHECK(wifind(text, u"\"{AAAA-SE}\"=\"v2.30|Action=Allow"));
    CHECK(!wifind(text, u"{BBBB-UP}"));
    xfree(text);
    // o schtasks foi chamado só para a tarefa do programa
    size_t schtasks = 0;
    for (size_t i = 0; i < fake_command_count(); i++)
        if (wifind(fake_command(i), u"schtasks.exe")) {
            schtasks++;
            CHECK(wifind(fake_command(i), u"/TN \"\\Nimbus\\SuperEditorUpdate\""));
        }
    CHECK(schtasks == 1);
    del_result_free(&r);
    left_free(&l);

    // modo avançado: a referência solta no Registro de outro programa aparece, desmarcada
    base_system();
    super_editor_installed();
    v = programs_load();
    p = find_program(&v, u"Super Editor 4.2.1");
    Identity *id2 = program_identity(p);
    programs_free(&v);
    LeftList a = {0};
    scan_leftovers(&a, id2, key, SCAN_ADVANCED, NULL);
    left_finalize(&a);
    Leftover *recent = find_item(&a, LK_VALUE, u"HKCU\\Software\\OutroApp\\Recent", u"File1");
    CHECK(recent && recent->conf == CONF_LOW && !recent->checked);
    left_free(&a);
    identity_free(id2);
    identity_free(id);
    xfree(key);
}

// ---------------------------------------------------------------- duas versões do mesmo programa

#define PY_ROOT u"C:\\Users\\ana\\AppData\\Local\\Programs\\Python"

static void python_install(const WCHAR *ver, const WCHAR *tag, const WCHAR *core_ver) {
    WCHAR *key = wfmt(u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{PY-%s}", tag);
    WCHAR *name = wfmt(u"Python %s (64-bit)", ver), *dir = wfmt(PY_ROOT u"\\Python%s", tag), *exe = wfmt(u"%s\\python.exe", dir);
    fake_str(key, u"DisplayName", name);
    fake_str(key, u"DisplayVersion", ver);
    fake_str(key, u"Publisher", u"Python Software Foundation");
    fake_str(key, u"DisplayIcon", exe);
    fake_str(key, u"UninstallString", u"\"C:\\Users\\ana\\AppData\\Local\\Package Cache\\{x}\\python-setup.exe\" /uninstall");
    fake_file(exe, "MZ", 2);
    WCHAR *lib = wfmt(u"%s\\Lib\\os.py", dir);
    fake_file(lib, "x", 1);
    WCHAR *core = wfmt(u"HKCU\\Software\\Python\\PythonCore\\%s\\InstallPath", core_ver);
    fake_str(core, u"", dir);
    fake_str(core, u"ExecutablePath", exe);
    WCHAR *site = wfmt(u"C:\\Users\\ana\\AppData\\Roaming\\Python\\Python%s\\site-packages\\x.py", tag);
    fake_file(site, "x", 1);
    xfree(key), xfree(name), xfree(dir), xfree(exe), xfree(lib), xfree(core), xfree(site);
}

static void test_two_versions(void) {
    base_system();
    python_install(u"3.11.5", u"311", u"3.11");
    python_install(u"3.12.0", u"312", u"3.12");
    fake_file(PY_ROOT u"\\Launcher\\py.exe", "MZ", 2);  // de nenhum dos dois, sem registro
    PtrVec v = programs_load();
    Program *p = find_program(&v, u"Python 3.12.0 (64-bit)");
    CHECK(p != NULL);
    if (!p) return;
    Identity *id = program_identity(p);
    WCHAR *key = wdup(p->key_path);
    programs_free(&v);
    CHECK(id->dirs.n == 1 && weq(id->dirs.items[0], PY_ROOT u"\\Python312"));
    // o desinstalador do 3.12 tira a entrada e o python.exe; sobra a Lib
    reg_delete_key(key);
    DeleteFileW(PY_ROOT u"\\Python312\\python.exe");

    LeftList l = {0};
    scan_leftovers(&l, id, key, SCAN_MODERATE, NULL);
    left_finalize(&l);
    Leftover *shared = find_item(&l, LK_KEY, u"HKCU\\Software\\Python", NULL);
    CHECK(shared && shared->conf == CONF_LOW && !shared->checked);
    Leftover *core = find_item(&l, LK_KEY, u"HKCU\\Software\\Python\\PythonCore\\3.12", NULL);
    CHECK(core && core->conf == CONF_HIGH && core->checked);
    CHECK(!any_path_contains(&l, u"3.11"));
    CHECK(!any_path_contains(&l, u"Python311"));
    Leftover *inst = find_item(&l, LK_DIR, PY_ROOT u"\\Python312", NULL);
    CHECK(inst && inst->checked);
    Leftover *launcher = find_item(&l, LK_DIR, PY_ROOT u"\\Launcher", NULL);
    CHECK(!launcher || !launcher->checked);
    Leftover *site = find_item(&l, LK_DIR, u"C:\\Users\\ana\\AppData\\Roaming\\Python\\Python312", NULL);
    CHECK(site != NULL);
    if (g_fail) dump(&l);

    DelResult r;
    delete_leftovers(&l, u"Python 3.12", NULL, &r);
    CHECK(r.failed == 0);
    CHECK(!fake_exists(PY_ROOT u"\\Python312"));
    CHECK(!fake_key_exists(u"HKCU\\Software\\Python\\PythonCore\\3.12"));
    // o 3.11 inteiro continua lá
    CHECK(fake_exists(PY_ROOT u"\\Python311\\python.exe"));
    CHECK(fake_exists(PY_ROOT u"\\Python311\\Lib\\os.py"));
    CHECK(fake_exists(PY_ROOT u"\\Launcher\\py.exe"));
    CHECK(fake_key_exists(u"HKCU\\Software\\Python\\PythonCore\\3.11\\InstallPath"));
    CHECK(fake_exists(u"C:\\Users\\ana\\AppData\\Roaming\\Python\\Python311\\site-packages\\x.py"));
    del_result_free(&r);
    left_free(&l);
    identity_free(id);
    xfree(key);
}

static WCHAR *rot13w(const WCHAR *s) {
    WCHAR *r = wdup(s);
    for (WCHAR *p = r; *p; p++) {
        if (*p >= 'a' && *p <= 'z') *p = (WCHAR)('a' + (*p - 'a' + 13) % 26);
        else if (*p >= 'A' && *p <= 'Z') *p = (WCHAR)('A' + (*p - 'A' + 13) % 26);
    }
    return r;
}

#define AUTOPLAY u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\AutoplayHandlers"
#define USERASSIST u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\UserAssist\\{CEBFF5CD-ACE2-4F4F-9178-9926F41749EA}\\Count"
#define JUMPLIST u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Search\\JumplistData"
#define FILEEXTS u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts"

// Casos vistos com programas reais (teste no Windows do GitHub Actions)
static void test_real_cases(void) {
    // FileZilla: a chave do programa tem o nome da chave de desinstalação ("FileZilla Client")
    base_system();
    const WCHAR FZ[] = u"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\FileZilla Client";
    fake_str(FZ, u"DisplayName", u"FileZilla 3.71.1");
    fake_str(FZ, u"DisplayVersion", u"3.71.1");
    fake_str(FZ, u"Publisher", u"Tim Kosse");
    fake_str(FZ, u"UninstallString", u"\"C:\\Program Files\\FileZilla FTP Client\\uninstall.exe\"");
    fake_str(FZ, u"DisplayIcon", u"C:\\Program Files\\FileZilla FTP Client\\filezilla.exe");
    fake_file(u"C:\\Program Files\\FileZilla FTP Client\\filezilla.exe", "MZ", 2);
    fake_str(u"HKLM\\SOFTWARE\\WOW6432Node\\FileZilla Client", u"", u"C:\\Program Files\\FileZilla FTP Client");
    fake_file(u"C:\\Users\\ana\\AppData\\Roaming\\FileZilla\\filezilla.xml", "x", 1);
    PtrVec v = programs_load();
    Program *p = find_program(&v, u"FileZilla 3.71.1");
    CHECK(p != NULL);
    if (p) {
        Identity *id = program_identity(p);
        WCHAR *key = wdup(p->key_path);
        reg_delete_key(key);
        LeftList l = {0};
        scan_leftovers(&l, id, key, SCAN_MODERATE, NULL);
        left_finalize(&l);
        Leftover *k = find_item(&l, LK_KEY, u"HKLM\\SOFTWARE\\WOW6432Node\\FileZilla Client", NULL);
        CHECK(k && k->conf == CONF_HIGH && k->checked);
        Leftover *d = find_item(&l, LK_DIR, u"C:\\Users\\ana\\AppData\\Roaming\\FileZilla", NULL);
        CHECK(d && d->checked);
        if (g_fail) dump(&l);
        left_free(&l);
        identity_free(id);
        xfree(key);
    }
    programs_free(&v);

    // WinSCP: configuração em "Martin Prikryl\WinSCP 2" (nome + número); "WinSCP 2023" seria outra coisa
    base_system();
    const WCHAR WS[] = u"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\winscp3_is1";
    fake_str(WS, u"DisplayName", u"WinSCP 6.7");
    fake_str(WS, u"DisplayVersion", u"6.7");
    fake_str(WS, u"Publisher", u"Martin Prikryl");
    fake_str(WS, u"InstallLocation", u"C:\\Program Files (x86)\\WinSCP\\");
    fake_str(WS, u"UninstallString", u"\"C:\\Program Files (x86)\\WinSCP\\unins000.exe\"");
    fake_str(u"HKCU\\Software\\Martin Prikryl\\WinSCP 2\\Configuration", u"x", u"1");
    fake_str(u"HKCU\\Software\\Martin Prikryl\\WinSCP 2023", u"x", u"1");
    v = programs_load();
    p = find_program(&v, u"WinSCP 6.7");
    CHECK(p != NULL);
    if (p) {
        Identity *id = program_identity(p);
        WCHAR *key = wdup(p->key_path);
        reg_delete_key(key);
        LeftList l = {0};
        scan_leftovers(&l, id, key, SCAN_MODERATE, NULL);
        left_finalize(&l);
        Leftover *k = find_item(&l, LK_KEY, u"HKCU\\Software\\Martin Prikryl\\WinSCP 2", NULL);
        CHECK(k && k->conf == CONF_HIGH && k->checked);
        Leftover *other = find_item(&l, LK_KEY, u"HKCU\\Software\\Martin Prikryl\\WinSCP 2023", NULL);
        CHECK(!other || !other->checked);
        if (g_fail) dump(&l);
        left_free(&l);
        identity_free(id);
        xfree(key);
    }
    programs_free(&v);

    // VLC: a pasta de configuração tem o nome do executável, de 3 letras ("vlc")
    base_system();
    const WCHAR VL[] = u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\VLC media player";
    fake_str(VL, u"DisplayName", u"VLC media player");
    fake_str(VL, u"DisplayVersion", u"3.0.24");
    fake_str(VL, u"Publisher", u"VideoLAN");
    fake_str(VL, u"UninstallString", u"\"C:\\Program Files\\VideoLAN\\VLC\\uninstall.exe\"");
    fake_str(VL, u"DisplayIcon", u"C:\\Program Files\\VideoLAN\\VLC\\vlc.exe");
    fake_file(u"C:\\Program Files\\VideoLAN\\VLC\\vlc.exe", "MZ", 2);
    fake_file(u"C:\\Users\\ana\\AppData\\Roaming\\vlc\\vlc-qt-interface.ini", "x", 1);
    fake_file(u"C:\\Users\\ana\\AppData\\Roaming\\vlcsnap\\x.png", "x", 1);  // outro nome: não
    // reprodução automática: um handler com ícone do VLC, outro que o desinstalador já apagou
    // (sobrou só o nome no evento) e um da Microsoft no mesmo evento
    fake_str(AUTOPLAY u"\\Handlers\\VLCPlayDVDMovieOnArrival", u"DefaultIcon", u"C:\\Program Files\\VideoLAN\\VLC\\vlc.exe,0");
    fake_str(AUTOPLAY u"\\Handlers\\MSPlayDVD", u"DefaultIcon", u"%SystemRoot%\\system32\\wmploc.dll,-25");
    fake_str(AUTOPLAY u"\\EventHandlers\\PlayDVDMovieOnArrival", u"VLCPlayDVDMovieOnArrival", u"");
    fake_str(AUTOPLAY u"\\EventHandlers\\PlayDVDMovieOnArrival", u"MSPlayDVD", u"");
    fake_str(AUTOPLAY u"\\EventHandlers\\PlayBluRayOnArrival", u"VLCPlayBlurayOnArrival", u"");
    // "Abrir com" do Explorer, rastros de uso (UserAssist em ROT13, Jump Lists)
    fake_str(FILEEXTS u"\\.mp3\\OpenWithProgids", u"VLC.mp3", u"");
    fake_str(FILEEXTS u"\\.mp3\\OpenWithProgids", u"WMP11.AssocFile.MP3", u"");
    WCHAR *ua_vlc = rot13w(u"{6D809377-6AF0-444B-8957-A3773F02200E}\\VideoLAN\\VLC\\vlc.exe");
    WCHAR *ua_np = rot13w(u"{F38BF404-1D43-42F2-9305-67DE0B28FC23}\\notepad.exe");
    fake_dword(USERASSIST, ua_vlc, 1);
    fake_dword(USERASSIST, ua_np, 1);
    fake_str(JUMPLIST, u"{6D809377-6AF0-444B-8957-A3773F02200E}\\VideoLAN\\VLC\\vlc.exe", u"x");
    fake_str(JUMPLIST, u"Microsoft.Windows.Explorer", u"x");
    v = programs_load();
    p = find_program(&v, u"VLC media player");
    CHECK(p != NULL);
    if (p) {
        Identity *id = program_identity(p);
        WCHAR *key = wdup(p->key_path);
        // o desinstalador tira a entrada e a pasta; a do editor (VideoLAN) fica vazia
        reg_delete_key(key);
        DeleteFileW(u"C:\\Program Files\\VideoLAN\\VLC\\vlc.exe");
        RemoveDirectoryW(u"C:\\Program Files\\VideoLAN\\VLC");
        LeftList l = {0};
        scan_leftovers(&l, id, key, SCAN_MODERATE, NULL);
        left_finalize(&l);
        Leftover *d = find_item(&l, LK_DIR, u"C:\\Users\\ana\\AppData\\Roaming\\vlc", NULL);
        CHECK(d && d->conf == CONF_HIGH && d->checked);
        Leftover *o = find_item(&l, LK_DIR, u"C:\\Users\\ana\\AppData\\Roaming\\vlcsnap", NULL);
        CHECK(!o || !o->checked);
        Leftover *it = find_item(&l, LK_DIR_IF_EMPTY, u"C:\\Program Files\\VideoLAN", NULL);
        CHECK(it && it->checked);
        it = find_item(&l, LK_KEY, AUTOPLAY u"\\Handlers\\VLCPlayDVDMovieOnArrival", NULL);
        CHECK(it && it->checked);
        it = find_item(&l, LK_VALUE, AUTOPLAY u"\\EventHandlers\\PlayDVDMovieOnArrival", u"VLCPlayDVDMovieOnArrival");
        CHECK(it && it->checked);
        it = find_item(&l, LK_VALUE, AUTOPLAY u"\\EventHandlers\\PlayBluRayOnArrival", u"VLCPlayBlurayOnArrival");
        CHECK(it && !it->checked);  // só pelo nome: desmarcado
        if (it) it->checked = true;  // o usuário marca
        it = find_item(&l, LK_VALUE, FILEEXTS u"\\.mp3\\OpenWithProgids", u"VLC.mp3");
        CHECK(it && it->checked);
        it = find_item(&l, LK_VALUE, USERASSIST, ua_vlc);
        CHECK(it && it->checked);
        it = find_item(&l, LK_VALUE, JUMPLIST, u"{6D809377-6AF0-444B-8957-A3773F02200E}\\VideoLAN\\VLC\\vlc.exe");
        CHECK(it && it->checked);
        CHECK(!any_path_contains(&l, u"MSPlayDVD") && !find_item(&l, LK_VALUE, FILEEXTS u"\\.mp3\\OpenWithProgids", u"WMP11.AssocFile.MP3"));
        CHECK(!find_item(&l, LK_VALUE, USERASSIST, ua_np) && !find_item(&l, LK_VALUE, JUMPLIST, u"Microsoft.Windows.Explorer"));
        if (g_fail) dump(&l);
        DelResult r;
        delete_leftovers(&l, u"VLC", NULL, &r);
        CHECK(r.failed == 0);
        CHECK(!fake_exists(u"C:\\Program Files\\VideoLAN") && !fake_exists(u"C:\\Users\\ana\\AppData\\Roaming\\vlc"));
        CHECK(!fake_key_exists(AUTOPLAY u"\\Handlers\\VLCPlayDVDMovieOnArrival") && fake_key_exists(AUTOPLAY u"\\Handlers\\MSPlayDVD"));
        CHECK(!fake_val_exists(AUTOPLAY u"\\EventHandlers\\PlayDVDMovieOnArrival", u"VLCPlayDVDMovieOnArrival"));
        CHECK(fake_val_exists(AUTOPLAY u"\\EventHandlers\\PlayDVDMovieOnArrival", u"MSPlayDVD"));
        CHECK(fake_key_exists(AUTOPLAY u"\\EventHandlers\\PlayBluRayOnArrival"));  // a chave do evento fica, mesmo vazia
        CHECK(!fake_val_exists(FILEEXTS u"\\.mp3\\OpenWithProgids", u"VLC.mp3") && fake_val_exists(FILEEXTS u"\\.mp3\\OpenWithProgids", u"WMP11.AssocFile.MP3"));
        CHECK(!fake_val_exists(USERASSIST, ua_vlc) && fake_val_exists(USERASSIST, ua_np));
        CHECK(fake_val_exists(JUMPLIST, u"Microsoft.Windows.Explorer"));
        del_result_free(&r);
        left_free(&l);
        identity_free(id);
        xfree(key);
    }
    programs_free(&v);
    xfree(ua_vlc), xfree(ua_np);

    // Notepad++: o atalho do Bloco de Notas do Windows ("Notepad.lnk", sem destino) não é dele
    base_system();
    const WCHAR NP[] = u"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Notepad++";
    fake_str(NP, u"DisplayName", u"Notepad++ (64-bit x64)");
    fake_str(NP, u"DisplayVersion", u"8.9.8.1");
    fake_str(NP, u"Publisher", u"Notepad++ Team");
    fake_str(NP, u"UninstallString", u"\"C:\\Program Files\\Notepad++\\uninstall.exe\"");
    fake_str(NP, u"DisplayIcon", u"C:\\Program Files\\Notepad++\\notepad++.exe");
    fake_file(u"C:\\Program Files\\Notepad++\\notepad++.exe", "MZ", 2);
    fake_key(u"HKLM\\SOFTWARE\\WOW6432Node\\Notepad++");
    {
        uint8_t hdr[76] = {0};
        static const uint8_t clsid[16] = {0x01, 0x14, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46};
        put32(hdr, 0x4C);
        memcpy(hdr + 4, clsid, 16);
        fake_file(u"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\Notepad.lnk", hdr, sizeof(hdr));
    }
    fake_file(u"C:\\Users\\ana\\AppData\\Roaming\\Notepad++\\config.xml", "x", 1);
    // pacote esparso do menu de contexto do Windows 11, que o desinstalador do Notepad++ deixou
    fake_str(APPX_PACKAGES_KEY u"\\NotepadPlusPlus_1.0.0.0_neutral__2247w0b46hfww", u"Path", u"C:\\Program Files\\Notepad++\\contextMenu");
    fake_str(APPX_PACKAGES_KEY u"\\Microsoft.WindowsNotepad_11.2507.26.0_x64__8wekyb3d8bbwe", u"Path",
             u"C:\\Program Files\\WindowsApps\\Microsoft.WindowsNotepad_11.2507.26.0_x64__8wekyb3d8bbwe");
    v = programs_load();
    p = find_program(&v, u"Notepad++ (64-bit x64)");
    CHECK(p != NULL);
    if (p) {
        Identity *id = program_identity(p);
        WCHAR *key = wdup(p->key_path);
        reg_delete_key(key);
        LeftList l = {0};
        scan_leftovers(&l, id, key, SCAN_MODERATE, NULL);
        left_finalize(&l);
        CHECK(!any_path_contains(&l, u"Notepad.lnk"));
        CHECK(!any_path_contains(&l, u"WindowsNotepad"));
        Leftover *pk = find_item(&l, LK_APPX, u"NotepadPlusPlus_1.0.0.0_neutral__2247w0b46hfww", NULL);
        CHECK(pk && pk->checked);
        DelResult r;
        delete_leftovers(&l, u"Notepad++", NULL, &r);
        CHECK(r.failed == 0);
        CHECK(!fake_key_exists(APPX_PACKAGES_KEY u"\\NotepadPlusPlus_1.0.0.0_neutral__2247w0b46hfww"));
        CHECK(fake_key_exists(APPX_PACKAGES_KEY u"\\Microsoft.WindowsNotepad_11.2507.26.0_x64__8wekyb3d8bbwe"));
        bool ps_all = false;
        for (size_t c = 0; c < fake_command_count(); c++) ps_all = ps_all || wifind(fake_command(c), u"powershell.exe");
        CHECK(ps_all);
        del_result_free(&r);
        Leftover *k = find_item(&l, LK_KEY, u"HKLM\\SOFTWARE\\WOW6432Node\\Notepad++", NULL);
        CHECK(k && k->checked);
        Leftover *d = find_item(&l, LK_DIR, u"C:\\Users\\ana\\AppData\\Roaming\\Notepad++", NULL);
        CHECK(d && d->checked);
        if (g_fail) dump(&l);
        left_free(&l);
        identity_free(id);
        xfree(key);
    }
    programs_free(&v);
}

int main(void) {
    test_programs_load();
    test_scan_safe();
    test_scan_moderate_and_delete();
    test_scan_advanced();
    test_shared_install_dir();
    test_microsoft_program();
    test_protection_end_to_end();
    test_registry_helpers();
    test_startup();
    test_monitor();
    test_full_cleanup();
    test_two_versions();
    test_real_cases();
    printf("%d verificações (Windows simulado), %d falhas\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
