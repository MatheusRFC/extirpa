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
    c = uninstall_command(ff, true, &q);  // sem variante silenciosa conhecida: o normal
    CHECK_WEQ(c, u"\"C:\\Program Files\\Mozilla Firefox\\uninstall\\helper.exe\"");
    CHECK(!q);
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
    CHECK(!any_path_contains(&l, u"FirefoxHTML"));  // tipos de arquivo: só no avançado
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
    CHECK(progid && progid->conf == CONF_MEDIUM && !progid->checked);
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
    Leftover *d = find_item(&l, LK_DIR, u"C:\\Program Files\\Acme", NULL);
    CHECK(d && d->conf == CONF_LOW && !d->checked);  // apagar levaria o outro programa junto
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
    programs_free(&v);
    LeftList l = {0};
    scan_leftovers(&l, id, NULL, SCAN_ADVANCED, NULL);
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
    printf("%d verificações (Windows simulado), %d falhas\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
