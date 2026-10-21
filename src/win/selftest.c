// SPDX-License-Identifier: MIT
// Autoteste em um Windows de verdade (só no extirpa-selftest.exe, compilado com
// EXTIRPA_SELFTEST; o extirpa.exe da release não tem este arquivo).
//
//     extirpa-selftest.exe --selftest
//
// Grava em %LOCALAPPDATA%\EXTIRPA\selftest\ o relatório (report.txt) e capturas de tela de
// cada janela (*.bmp). O que ele faz:
// - lê os programas instalados e mostra o que o EXTIRPA entende de cada um;
// - busca sobras (sem apagar nada) de alguns programas reais, para conferir os achados;
// - instala um programa de mentira (só em HKCU e na pasta do usuário), desinstala, busca
//   as sobras, apaga, confere, restaura o backup .reg pelo reg.exe e limpa tudo;
// - testa a espera do desinstalador (job object), o rastreamento de instalação, a lista
//   de inicialização, os aplicativos do Windows e o ponto de restauração;
// - abre cada janela da interface e salva uma captura.
// O código de saída é o número de falhas.
#include "../core/fmt.h"
#include "../core/pathx.h"
#include "../core/regtext.h"
#include "backups.h"
#include "dialogs.h"
#include "fsx.h"
#include "hunter.h"
#include "i18n.h"
#include "leftwin.h"
#include "mainwin.h"
#include "monitor.h"
#include "platform.h"
#include "programs.h"
#include "progress.h"
#include "reg.h"
#include "scan.h"
#include "startup.h"
#include "ui.h"
#include "uninst.h"
#include "appx.h"
#include "../core/trace.h"

static StrBuf g_rep;
static WCHAR *g_dir;
static int g_fail, g_ok;
static const WCHAR *g_shot_name;
static bool g_shot_end = true;

static void flush(void) {
    WCHAR *p = path_join(g_dir, u"report.txt");
    size_t n;
    char *u8 = w_to_utf8(sb_str(&g_rep), g_rep.len, &n);
    file_write_all(p, u8, n);
    xfree(u8), xfree(p);
}

static void line(const WCHAR *text) {
    sb_add(&g_rep, text);
    sb_add(&g_rep, u"\r\n");
}

static void linef(WCHAR *owned) {
    line(owned);
    xfree(owned);
}

static void check(bool ok, const WCHAR *what) {
    if (ok) g_ok++;
    else g_fail++;
    linef(wfmt(u"[%s] %s", ok ? u"OK" : u"FALHA", what));
    flush();
}

static void section(const WCHAR *title) {
    linef(wfmt(u"\r\n==== %s ====", title));
    flush();
}

// ---------------------------------------------------------------- capturas de tela

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }

static bool capture(HWND w, const WCHAR *name) {
    RECT r;
    if (!GetWindowRect(w, &r)) return false;
    int cx = r.right - r.left, cy = r.bottom - r.top;
    if (cx <= 0 || cy <= 0 || cx > 8000 || cy > 8000) return false;
    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi;
    mem_set(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = cx;
    bi.bmiHeader.biHeight = cy;  // de baixo para cima, como o .bmp
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    bool ok = false;
    if (bmp && bits) {
        HGDIOBJ old = SelectObject(mem, bmp);
        ok = PrintWindow(w, mem, PW_RENDERFULLCONTENT) != 0;
        SelectObject(mem, old);
        size_t pix = (size_t)cx * (size_t)cy * 4, total = 54 + pix;
        uint8_t *file = xalloc(total);
        file[0] = 'B', file[1] = 'M';
        put32(file + 2, (uint32_t)total);
        put32(file + 10, 54);
        mem_copy(file + 14, &bi.bmiHeader, 40);
        mem_copy(file + 54, bits, pix);
        WCHAR *fname = wfmt(u"%s.bmp", name), *path = path_join(g_dir, fname);
        ok = file_write_all(path, file, total) && ok;
        xfree(fname), xfree(path), xfree(file);
        DeleteObject(bmp);
    }
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
    linef(wfmt(u"  captura %s: %s (%dx%d)", name, ok ? u"ok" : u"falhou", cx, cy));
    return ok;
}

static void modal_hook(HWND w, ModalState *ms) {
    capture(w, g_shot_name ? g_shot_name : u"modal");
    if (g_shot_end) ui_end(ms, IDCANCEL);
}

static void pump_for(DWORD ms) {
    UINT_PTR t = SetTimer(NULL, 0, ms, NULL);
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (m.message == WM_TIMER && m.hwnd == NULL && m.wParam == t) break;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    KillTimer(NULL, t);
}

// ---------------------------------------------------------------- utilidades

static void dump_left(LeftList *l, size_t limit) {
    for (size_t i = 0; i < l->items.n && i < limit; i++) {
        Leftover *it = l->items.items[i];
        static const WCHAR *const kinds[] = {u"chave", u"valor", u"chave-se-vazia", u"pasta", u"arquivo", u"pasta-se-vazia",
                                             u"serviço", u"tarefa", u"firewall", u"PATH", u"pacote"};
        _Static_assert(COUNT(kinds) == LK_APPX + 1, "um nome para cada LeftKind");
        linef(wfmt(u"    [%s conf=%d %s] %s%s%s%s%s%s", kinds[it->kind], it->conf, it->checked ? u"x" : u" ", it->path, it->value ? u" -> " : u"",
                   it->value ? it->value : u"", it->detail ? u"  (" : u"", it->detail ? it->detail : u"", it->detail ? u")" : u""));
    }
    if (l->items.n > limit) linef(wfmt(u"    ... mais %u", (unsigned)(l->items.n - limit)));
}

static void make_lnk(const WCHAR *path, const WCHAR *target) {
    static const uint8_t clsid[16] = {0x01, 0x14, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46};
    uint8_t *buf = xalloc(4096);
    put32(buf, 0x4C);
    mem_copy(buf + 4, clsid, 16);
    put32(buf + 20, 0x2 | 0x80);
    size_t li = 76, hdr = 0x24, vol = hdr, ub = vol + 0x11, n = wlen(target), us = ub + (n + 1) * 2, total = us + 2;
    put32(buf + li, (uint32_t)total);
    put32(buf + li + 4, (uint32_t)hdr);
    put32(buf + li + 8, 1);
    put32(buf + li + 12, (uint32_t)vol);
    put32(buf + li + 16, (uint32_t)(vol + 0x10));
    put32(buf + li + 24, (uint32_t)(vol + 0x10));
    put32(buf + li + 28, (uint32_t)ub);
    put32(buf + li + 32, (uint32_t)us);
    put32(buf + li + vol, 0x11);
    for (size_t i = 0; i <= n; i++) buf[li + ub + 2 * i] = (uint8_t)target[i], buf[li + ub + 2 * i + 1] = (uint8_t)(target[i] >> 8);
    file_write_all(path, buf, li + total + 4);
    xfree(buf);
}

static void set_str(const WCHAR *key, const WCHAR *name, const WCHAR *value) {
    HKEY k = NULL;
    const WCHAR *sub = key + 5;  // só HKCU\ aqui
    if (RegCreateKeyExW(HKEY_CURRENT_USER, sub, 0, NULL, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &k, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)value, (DWORD)((wlen(value) + 1) * sizeof(WCHAR)));
        RegCloseKey(k);
    }
}

static bool value_exists(const WCHAR *key, const WCHAR *name) {
    HKEY k = reg_open(key, KEY_QUERY_VALUE);
    if (!k) return false;
    DWORD t, sz = 0;
    bool ok = RegQueryValueExW(k, name, NULL, &t, NULL, &sz) == ERROR_SUCCESS;
    RegCloseKey(k);
    return ok;
}

static Program *find_program(PtrVec *v, const WCHAR *name) {
    for (size_t i = 0; i < v->n; i++)
        if (wieq(((Program *)v->items[i])->name, name)) return v->items[i];
    return NULL;
}

// ---------------------------------------------------------------- seções

static void test_environment(void) {
    section(u"Ambiente");
    linef(wfmt(u"EXTIRPA %s, administrador: %s, idioma: %s", EXTIRPA_VERSION, IsUserAnAdmin() ? u"sim" : u"não", i18n_pt() ? u"pt-BR" : u"en-US"));
    const Paths *p = &g_paths;
    const WCHAR *names[] = {u"windows", u"program_files", u"program_files_x86", u"program_data", u"appdata", u"local_appdata", u"desktop", u"programs", u"data_dir"};
    const WCHAR *vals[] = {p->windows, p->program_files, p->program_files_x86, p->program_data, p->appdata, p->local_appdata, p->desktop, p->programs, p->data_dir};
    bool all = true;
    for (size_t i = 0; i < COUNT(names); i++) {
        linef(wfmt(u"  %s = %s", names[i], vals[i] ? vals[i] : u"(NULL)"));
        all = all && vals[i] && dir_exists(vals[i]);
    }
    check(all, u"pastas do sistema encontradas e existentes");
}

static void test_programs(PtrVec *progs) {
    section(u"Programas instalados");
    *progs = programs_load();
    check(progs->n > 5, u"lista de programas instalados lida");
    for (size_t i = 0; i < progs->n; i++) {
        Program *p = progs->items[i];
        bool q;
        WCHAR *cmd = uninstall_command(p, true, &q);
        WCHAR *size = p->size ? fmt_size(p->size, &g_fmt) : wdup(u"-");
        linef(wfmt(u"  %s | %s | %s | escopo %d | %s | %d-%d-%d | %s%s", p->name, p->version ? p->version : u"-", p->publisher ? p->publisher : u"-",
                   (int)p->scope, size, p->year, p->month, p->day, cmd ? cmd : u"(sem desinstalador)", q ? u"  [silencioso]" : u""));
        xfree(cmd), xfree(size);
    }
    flush();
}

static void test_real_scans(PtrVec *progs) {
    section(u"Busca de sobras (só leitura) em programas reais instalados");
    size_t done = 0;
    for (size_t i = 0; i < progs->n && done < 12; i++) {
        Program *p = progs->items[i];
        if (!p->install_location) continue;
        Identity *id = program_identity(p);
        linef(wfmt(u"  %s (pastas: %u, palavra principal: %s)", p->name, (unsigned)id->dirs.n, id->primary ? id->primary : u"-"));
        LeftList l = {0};
        ULONGLONG t0 = GetTickCount64();
        scan_leftovers(&l, id, p->key_path, SCAN_MODERATE, NULL);
        left_finalize(&l);
        linef(wfmt(u"    %u itens em %U ms", (unsigned)l.items.n, (uint64_t)(GetTickCount64() - t0)));
        dump_left(&l, 25);
        left_free(&l);
        identity_free(id);
        done++;
        flush();
    }
    check(done > 0, u"buscas de sobras em programas reais executadas");
}

#define W_NAME u"Extirpa Selftest Widget"
#define W_CORP u"Extirpa Selftest Corp"
#define W_KEY u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ExtirpaSelftestWidget"
#define W_REG u"HKCU\\Software\\" W_CORP u"\\" W_NAME
#define W_REG_OTHER u"HKCU\\Software\\" W_CORP u"\\Other Product"
#define RUN u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define APPROVED u"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run"

static LeftList g_sample;  // lista para a captura da janela de sobras

static void test_synthetic_app(void) {
    section(u"Programa de mentira: instalar, desinstalar, buscar, apagar, restaurar");
    WCHAR *programs_dir = path_join(g_paths.local_appdata, u"Programs");
    WCHAR *install = path_join(programs_dir, W_NAME);
    WCHAR *bin = path_join(install, u"bin"), *exe = path_join(install, u"widget.exe"), *binexe = path_join(bin, u"helper.dll");
    WCHAR *corp_data = path_join(g_paths.appdata, W_CORP), *data = path_join(corp_data, W_NAME), *other_data = path_join(corp_data, u"Other Product");
    WCHAR *cfg = path_join(data, u"config.ini"), *other_cfg = path_join(other_data, u"x.ini");
    WCHAR *lnk = wfmt(u"%s\\%s.lnk", g_paths.programs, W_NAME);
    dir_create_all(bin), dir_create_all(data), dir_create_all(other_data);
    file_write_all(exe, "MZ", 2), file_write_all(binexe, "MZ", 2), file_write_all(cfg, "a=1", 3), file_write_all(other_cfg, "b=2", 3);
    make_lnk(lnk, exe);
    WCHAR *uninst = wfmt(u"\"%s\\cmd.exe\" /c rd /s /q \"%s\"", g_paths.system32, bin);
    set_str(W_KEY, u"DisplayName", W_NAME);
    set_str(W_KEY, u"Publisher", W_CORP);
    set_str(W_KEY, u"DisplayVersion", u"1.2.3");
    set_str(W_KEY, u"InstallLocation", install);
    set_str(W_KEY, u"UninstallString", uninst);
    set_str(W_REG u"\\Settings", u"Theme", u"dark");
    set_str(W_REG_OTHER, u"x", u"1");
    WCHAR *run_cmd = wfmt(u"\"%s\" --tray", exe);
    set_str(RUN, W_NAME, run_cmd);
    uint8_t on[12] = {2};
    reg_set_binary(APPROVED, W_NAME, on, sizeof(on));

    PtrVec progs = programs_load();
    Program *p = find_program(&progs, W_NAME);
    check(p != NULL, u"programa de mentira aparece na lista");
    if (!p) {
        programs_free(&progs);
        goto cleanup;
    }
    Identity *id = program_identity(p);
    check(id->dirs.n == 1 && path_eq(id->dirs.items[0], install), u"pasta de instalação identificada");
    WCHAR *cmd = uninstall_command(p, false, NULL);
    linef(wfmt(u"  desinstalador: %s", cmd));
    RunResult rr;
    ULONGLONG t0 = GetTickCount64();
    run_and_wait(cmd, NULL, &rr);
    linef(wfmt(u"  rodou: %s, código %u, %U ms", rr.started ? u"sim" : u"não", (unsigned)rr.exit_code, (uint64_t)(GetTickCount64() - t0)));
    check(rr.started && rr.exit_code == 0 && !dir_exists(bin), u"desinstalador executado e esperado (job object)");
    xfree(cmd);
    reg_delete_key(W_KEY);  // como um desinstalador de verdade faria
    WCHAR *key = wdup(p->key_path);
    programs_free(&progs);

    LeftList l = {0};
    scan_leftovers(&l, id, key, SCAN_MODERATE, NULL);
    left_finalize(&l);
    leftovers_measure(&l, NULL);
    dump_left(&l, 50);
    bool only_ours = true;
    for (size_t i = 0; i < l.items.n; i++) {
        Leftover *it = l.items.items[i];
        bool ours = wifind(it->path, u"Extirpa Selftest") || (it->value && wifind(it->value, u"Extirpa Selftest"));
        if (!ours) {
            only_ours = false;
            linef(wfmt(u"  !! item que não é do programa de mentira: %s", it->path));
        }
    }
    check(only_ours, u"a busca não achou nada de outros programas (sem falso positivo)");
    check(l.items.n == 8, u"8 itens esperados: pasta, chave, chave do editor, pasta de dados, pasta do editor, Run, StartupApproved, atalho");
    // cópia para a captura da janela de sobras
    for (size_t i = 0; i < l.items.n; i++) {
        Leftover *it = l.items.items[i];
        Leftover *c = left_add(&g_sample, it->kind, it->path, it->value, it->conf, it->reason, it->detail);
        if (c) c->size = it->size;
    }
    left_finalize(&g_sample);

    DelResult r;
    delete_leftovers(&l, W_NAME, NULL, &r);
    linef(wfmt(u"  apagados %d, falharam %d, mantidos %d, backup %s", r.deleted, r.failed, r.kept, r.backup_path ? r.backup_path : u"-"));
    check(r.failed == 0 && r.backup_path && file_exists(r.backup_path), u"exclusão sem falhas e com backup .reg");
    check(!dir_exists(install) && !dir_exists(data) && !file_exists(lnk), u"pastas e atalho foram para a Lixeira");
    check(!reg_exists(W_REG) && !value_exists(RUN, W_NAME) && !value_exists(APPROVED, W_NAME), u"chaves e valores do programa saíram do Registro");
    check(reg_exists(W_REG_OTHER) && file_exists(other_cfg), u"o outro produto do mesmo editor ficou intacto");
    check(r.kept == 2, u"pasta e chave do editor mantidas (não ficaram vazias)");

    if (r.backup_path) {
        WCHAR *err = backup_restore(r.backup_path);
        linef(wfmt(u"  reg import: %s", err ? err : u"ok"));
        check(!err && value_exists(W_REG u"\\Settings", u"Theme") && value_exists(RUN, W_NAME), u"backup .reg restaurado pelo reg.exe");
        xfree(err);
    }
    del_result_free(&r);
    left_free(&l);
    identity_free(id);
    xfree(key);

cleanup:
    reg_delete_key(u"HKCU\\Software\\" W_CORP);
    reg_delete_key(W_KEY);
    reg_delete_value(RUN, W_NAME);
    reg_delete_value(APPROVED, W_NAME);
    if (dir_exists(corp_data)) fs_recycle(corp_data);
    if (dir_exists(install)) fs_recycle(install);
    if (file_exists(lnk)) fs_recycle(lnk);
    check(!reg_exists(u"HKCU\\Software\\" W_CORP) && !dir_exists(corp_data), u"limpeza do teste");
    xfree(programs_dir), xfree(install), xfree(bin), xfree(exe), xfree(binexe), xfree(corp_data), xfree(data), xfree(other_data);
    xfree(cfg), xfree(other_cfg), xfree(lnk), xfree(uninst), xfree(run_cmd);
}

static void test_job_wait(void) {
    section(u"Espera do desinstalador e dos processos que ele abre");
    // o cmd sai na hora, mas deixa um "neto" (ping) rodando por ~2 s
    WCHAR *cmd = wfmt(u"\"%s\\cmd.exe\" /c start \"\" /b \"%s\\PING.EXE\" -n 3 127.0.0.1", g_paths.system32, g_paths.system32);
    RunResult r;
    ULONGLONG t0 = GetTickCount64();
    run_and_wait(cmd, NULL, &r);
    ULONGLONG dt = GetTickCount64() - t0;
    linef(wfmt(u"  %U ms", (uint64_t)dt));
    check(r.started && dt >= 1500, u"espera também os processos filhos");
    xfree(cmd);
    WCHAR *bad = wdup(u"\"C:\\nao\\existe\\uninstall.exe\" /S");
    run_and_wait(bad, NULL, &r);
    check(!r.started && r.error != 0, u"desinstalador inexistente: erro sem travar");
    xfree(bad);
}

static void test_trace(void) {
    section(u"Rastreamento de instalação (fotos do sistema)");
    Snapshot s = {0};
    ULONGLONG t0 = GetTickCount64();
    monitor_snapshot(&s, false, NULL);
    linef(wfmt(u"  primeira foto: %U ms, %U entradas", (uint64_t)(GetTickCount64() - t0), (uint64_t)s.before.n));
    set_str(u"HKCU\\Software\\ExtirpaTraceTest\\Sub", u"v", u"1");
    WCHAR *dir = path_join(g_paths.local_appdata, u"ExtirpaTraceTest"), *f = path_join(dir, u"f.txt");
    dir_create_all(dir);
    file_write_all(f, "x", 1);
    s.out = trace_new();
    t0 = GetTickCount64();
    monitor_snapshot(&s, true, NULL);
    linef(wfmt(u"  segunda foto: %U ms, %u itens novos", (uint64_t)(GetTickCount64() - t0), (unsigned)s.out->items.n));
    bool key = false, d = false;
    for (size_t i = 0; i < s.out->items.n; i++) {
        TraceItem *it = s.out->items.items[i];
        if (i < 40) linef(wfmt(u"    %c %s%s%s", (WCHAR)it->kind, it->path, it->value ? u" -> " : u"", it->value ? it->value : u""));
        if (it->kind == TR_KEY && path_eq(it->path, u"HKCU\\Software\\ExtirpaTraceTest")) key = true;
        if (it->kind == TR_DIR && path_eq(it->path, dir)) d = true;
    }
    check(key && d, u"chave e pasta novas apareceram no rastreamento");
    reg_delete_key(u"HKCU\\Software\\ExtirpaTraceTest");
    fs_recycle(dir);
    hs_free(&s.before);
    trace_free(s.out);
    xfree(dir), xfree(f);
}

static void test_lists(void) {
    section(u"Inicialização");
    PtrVec st = startup_list();
    for (size_t i = 0; i < st.n; i++) {
        StartupEntry *e = st.items[i];
        linef(wfmt(u"  %s | %s | %s | %s", e->name, e->enabled ? u"ativado" : u"desativado", startup_source_text(e->source), e->command));
    }
    check(true, u"lista de inicialização lida");
    startup_list_free(&st);

    section(u"Aplicativos do Windows (PowerShell)");
    WCHAR *err = NULL;
    ULONGLONG t0 = GetTickCount64();
    PtrVec apps = appx_list(&err);
    linef(wfmt(u"  %u pacotes em %U ms%s%s", (unsigned)apps.n, (uint64_t)(GetTickCount64() - t0), err ? u", erro: " : u"", err ? err : u""));
    for (size_t i = 0; i < apps.n && i < 20; i++) {
        AppxPackage *a = apps.items[i];
        linef(wfmt(u"  %s | %s | %s | %s", a->name, a->version, a->publisher, a->removable ? u"removível" : u"sistema"));
    }
    check(!err, u"aplicativos do Windows listados");
    appx_list_free(&apps);
    xfree(err);

    section(u"Ponto de restauração");
    INT64 seq;
    DWORD e;
    bool ok = restore_point_begin(u"EXTIRPA autoteste", &seq, &e);
    if (ok) restore_point_end(seq, true);  // cancela: não deixa ponto para trás
    WCHAR *et = ok ? wdup(u"criado e cancelado") : win_error_text(e);
    linef(wfmt(u"  %s", et));
    xfree(et);
    check(true, u"chamada ao ponto de restauração não travou");
}

typedef struct {
    int dummy;
} SleepJob;

static void sleepy_work(Progress *pg, void *arg) {
    (void)arg;
    pg_step(pg, T(S_STEP_SCANNING));
    pg_detail(pg, u"C:\\Program Files\\Exemplo\\pasta\\muito\\funda\\arquivo.dll");
    Sleep(2500);
}

static void test_ui(PtrVec *progs) {
    section(u"Interface (capturas de tela)");
    HWND main = mainwin_create();
    check(main != NULL, u"janela principal criada");
    if (!main) return;
    pump_for(1500);
    capture(main, u"01-programas");
    static const WCHAR *const names[] = {u"", u"02-apps", u"03-rastreados", u"04-inicializacao", u"05-backups"};
    for (int t = 1; t <= 4; t++) {
        mainwin_selftest_tab(t);
        if (t == 1)
            for (int i = 0; i < 60 && !mainwin_selftest_apps_ready(); i++) pump_for(500);
        pump_for(800);
        capture(main, names[t]);
    }
    mainwin_selftest_tab(0);
    pump_for(500);

    g_modal_hook = modal_hook;
    Program *sel[2] = {progs->n ? progs->items[0] : NULL, progs->n > 1 ? progs->items[1] : NULL};
    if (sel[0]) {
        UninstallOpts o;
        g_shot_name = u"10-confirmar-um";
        dlg_confirm_uninstall(main, sel, 1, false, &o);
        if (sel[1]) {
            g_shot_name = u"11-confirmar-varios";
            dlg_confirm_uninstall(main, sel, 2, true, &o);
        }
    }
    WCHAR *n = wdup(u"Programa Antigo"), *f = wdup(u"");
    ScanMode m;
    g_shot_name = u"12-forcada";
    dlg_forced(main, &n, &f, &m);
    xfree(n), xfree(f);
    g_shot_name = u"13-nome";
    WCHAR *r = dlg_input(main, T(S_TRACE_TITLE), u"A instalação criou 42 itens. Nome para este rastreamento:", u"Programa Exemplo 1.0");
    xfree(r);
    g_shot_name = u"14-sobras";
    leftovers_show(main, W_NAME, NULL, &g_sample);
    g_shot_name = u"15-progresso";
    g_shot_end = false;
    SleepJob job;
    progress_run(main, T(S_UNINSTALL), sleepy_work, &job);
    g_shot_end = true;
    g_shot_name = u"16-cacador";
    hunter_run(main);
    g_modal_hook = NULL;
    DestroyWindow(main);
    check(true, u"todas as janelas abriram e fecharam");
}

// ---------------------------------------------------------------- teste com programas reais
//
//   extirpa-selftest.exe --snapshot-save <arquivo>
//       foto do sistema (antes de instalar os programas de teste)
//   extirpa-selftest.exe --realtest <arquivo> <nome1> [<nome2> ...]
//       para cada programa (parte do nome em "Aplicativos instalados"): desinstalação rápida
//       pelo EXTIRPA, busca de sobras no modo moderado e exclusão dos itens marcados, como o
//       usuário faria clicando em "Excluir marcados". Depois tira a segunda foto e lista tudo o
//       que a instalação criou e ainda existe: o que o EXTIRPA deixou para trás.

static WCHAR *env_path(const WCHAR *key) {
    HKEY k = reg_open(key, KEY_READ);
    if (!k) return wdup(u"");
    WCHAR *p = reg_get_str(k, u"Path");
    RegCloseKey(k);
    return p ? p : wdup(u"");
}

static int snapshot_save(const WCHAR *file) {
    Snapshot s;
    mem_set(&s, 0, sizeof(s));
    ULONGLONG t0 = GetTickCount64();
    monitor_snapshot(&s, false, NULL);
    StrBuf b = {0};
    WCHAR *pm = env_path(u"HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment"), *pu = env_path(u"HKCU\\Environment");
    // arquivo: n (64 bits), os hashes, e os dois PATH em UTF-16 terminados em zero
    size_t bytes = 8 + s.before.n * 8 + (wlen(pm) + 1 + wlen(pu) + 1) * 2;
    uint8_t *d = xalloc(bytes), *o = d;
    uint64_t n = s.before.n;
    mem_copy(o, &n, 8), o += 8;
    for (size_t i = 0; i < s.before.cap; i++)
        if (s.before.slots[i]) mem_copy(o, &s.before.slots[i], 8), o += 8;
    mem_copy(o, pm, (wlen(pm) + 1) * 2), o += (wlen(pm) + 1) * 2;
    mem_copy(o, pu, (wlen(pu) + 1) * 2);
    bool ok = file_write_all(file, d, bytes);
    xfree(d), xfree(pm), xfree(pu);
    hs_free(&s.before);
    sb_free(&b);
    WCHAR *rep = path_join(g_dir, u"snapshot.txt");
    WCHAR *t = wfmt(u"foto: %U entradas, %U ms, gravada: %s\r\n", n, (uint64_t)(GetTickCount64() - t0), ok ? u"sim" : u"NÃO");
    size_t len;
    char *u8 = w_to_utf8(t, wlen(t), &len);
    file_write_all(rep, u8, len);
    xfree(u8), xfree(t), xfree(rep);
    return ok ? 0 : 1;
}

typedef struct {
    WCHAR *name;
    Identity *id;
} Tested;

static bool belongs(const Tested *t, const WCHAR *path) {
    if (match_path_ref(t->id, path)) return true;
    // algum pedaço do caminho tem o nome do programa
    for (const WCHAR *p = path; *p;) {
        const WCHAR *e = p;
        while (*e && *e != '\\') e++;
        WCHAR *part = wdupn(p, (size_t)(e - p));
        int c = match_name(t->id, part);
        xfree(part);
        if (c >= CONF_MEDIUM) return true;
        p = *e ? e + 1 : e;
    }
    return false;
}

static void path_diff(const WCHAR *before, const WCHAR *after, const WCHAR *label) {
    for (const WCHAR *p = after; *p;) {
        const WCHAR *e = wfindc(p, ';');
        if (!e) e = p + wlen(p);
        WCHAR *entry = wdupn(p, (size_t)(e - p));
        if (entry[0]) {
            bool was = false;
            for (const WCHAR *q = before; *q && !was;) {
                const WCHAR *f = wfindc(q, ';');
                if (!f) f = q + wlen(q);
                was = (size_t)(f - q) == wlen(entry) && wieqn(q, entry, wlen(entry));
                q = *f ? f + 1 : f;
            }
            if (!was) linef(wfmt(u"  PATH (%s) ganhou: %s", label, entry));
        }
        xfree(entry);
        p = *e ? e + 1 : e;
    }
}

static DWORD WINAPI watchdog(LPVOID ev) {
    Sleep(8 * 60 * 1000);  // 8 min por desinstalador: depois disso segue sem esperar
    SetEvent(ev);
    return 0;
}

static int realtest(int argc, WCHAR **argv) {
    const WCHAR *snapfile = argv[2];
    uint8_t *d;
    size_t len;
    if (!file_read_all(snapfile, &d, &len) || len < 8) {
        line(u"foto não encontrada");
        flush();
        return 100;
    }
    Snapshot s;
    mem_set(&s, 0, sizeof(s));
    uint64_t n;
    mem_copy(&n, d, 8);
    const uint8_t *o = d + 8;
    for (uint64_t i = 0; i < n && o + 8 <= d + len; i++, o += 8) {
        uint64_t h;
        mem_copy(&h, o, 8);
        hs_add(&s.before, h);
    }
    WCHAR *pm_before = wdup((const WCHAR *)o), *pu_before = wdup((const WCHAR *)o + wlen((const WCHAR *)o) + 1);
    xfree(d);

    PtrVec tested = {0};
    int problems = 0;
    for (int a = 3; a < argc; a++) {
        section(argv[a]);
        PtrVec progs = programs_load();
        Program *p = NULL;
        for (size_t i = 0; i < progs.n && !p; i++)  // primeiro o nome exato, depois parte do nome
            if (wieq(((Program *)progs.items[i])->name, argv[a])) p = progs.items[i];
        for (size_t i = 0; i < progs.n && !p; i++)
            if (wifind(((Program *)progs.items[i])->name, argv[a])) p = progs.items[i];
        if (!p) {
            linef(wfmt(u"  NÃO ENCONTRADO na lista de programas: %s", argv[a]));
            problems++;
            programs_free(&progs);
            continue;
        }
        Identity *id = program_identity(p);
        Tested *t = xalloc(sizeof(Tested));
        t->name = wdup(p->name);
        t->id = id;
        pv_push(&tested, t);
        WCHAR *key = wdup(p->key_path);
        linef(wfmt(u"  programa: %s %s (%s)", p->name, p->version ? p->version : u"", p->publisher ? p->publisher : u"-"));
        linef(wfmt(u"  entrada: %s", key));
        for (size_t i = 0; i < id->dirs.n; i++) linef(wfmt(u"  pasta: %s", id->dirs.items[i]));
        for (size_t i = 0; i < id->alt_dirs.n; i++) linef(wfmt(u"  pasta (8.3): %s", id->alt_dirs.items[i]));
        bool quiet;
        WCHAR *cmd = uninstall_command(p, true, &quiet);
        programs_free(&progs);
        linef(wfmt(u"  desinstalador: %s%s", cmd ? cmd : u"(nenhum)", quiet ? u"  [silencioso]" : u"  [NÃO SILENCIOSO]"));
        flush();
        if (cmd && quiet) {
            HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
            HANDLE th = CreateThread(NULL, 0, watchdog, ev, 0, NULL);
            RunResult rr;
            ULONGLONG t0 = GetTickCount64();
            run_and_wait(cmd, ev, &rr);
            linef(wfmt(u"  rodou: %s, código %u, %U ms%s", rr.started ? u"sim" : u"não", (unsigned)rr.exit_code, (uint64_t)(GetTickCount64() - t0),
                       rr.skipped ? u" (TEMPO ESGOTADO)" : u""));
            if (th) CloseHandle(th);
            CloseHandle(ev);
            Sleep(3000);
        } else {
            problems++;
        }
        xfree(cmd);
        bool still = reg_exists(key);
        linef(wfmt(u"  entrada ainda existe depois do desinstalador: %s", still ? u"SIM" : u"não"));
        LeftList l = {0};
        ULONGLONG t0 = GetTickCount64();
        scan_leftovers(&l, id, key, SCAN_MODERATE, NULL);
        left_finalize(&l);
        linef(wfmt(u"  sobras: %u itens em %U ms", (unsigned)l.items.n, (uint64_t)(GetTickCount64() - t0)));
        dump_left(&l, 200);
        DelResult r;
        delete_leftovers(&l, t->name, NULL, &r);
        linef(wfmt(u"  exclusão: %d apagados, %d falharam, %d mantidos, %d sem Lixeira", r.deleted, r.failed, r.kept, r.permanent));
        for (size_t i = 0; i < l.items.n; i++) {
            Leftover *it = l.items.items[i];
            if (it->status == LS_FAILED) {
                WCHAR *e = win_error_text(it->error);
                linef(wfmt(u"    falhou: %s %s: %s", it->path, it->value ? it->value : u"", e));
                xfree(e);
                problems++;
                if (it->kind == LK_DIR && dir_exists(it->path)) {
                    // o que ficou na pasta e quem está usando
                    PtrVec one = {0};
                    pv_push(&one, it->path);
                    PtrVec procs = processes_under(&one);
                    for (size_t j = 0; j < procs.n; j++)
                        linef(wfmt(u"      processo: %s (PID %u)", ((ProcInfo *)procs.items[j])->exe, (unsigned)((ProcInfo *)procs.items[j])->pid));
                    processes_free(&procs);
                    pv_free(&one);
                    PtrVec files = fs_list(it->path);
                    for (size_t j = 0; j < files.n && j < 20; j++) {
                        DirEntry *f = files.items[j];
                        linef(wfmt(u"      contém: %s%s", f->name, f->is_dir ? u"\\" : u""));
                    }
                    fs_list_free(&files);
                    // o que a janela de sobras oferece nesse caso
                    linef(wfmt(u"      agendado para apagar na reinicialização: %s", fs_delete_on_reboot(it->path) ? u"sim" : u"NÃO"));
                }
            }
        }
        del_result_free(&r);
        left_free(&l);
        xfree(key);
        flush();
    }

    section(u"O que a instalação criou e ainda existe (segunda foto)");
    size_t choco = 0;
    s.out = trace_new();
    monitor_snapshot(&s, true, NULL);
    size_t mine = 0, other = 0;
    for (size_t i = 0; i < s.out->items.n; i++) {
        TraceItem *it = s.out->items.items[i];
        const Tested *owner = NULL;
        if (wifind(it->path, u"chocolatey")) {  // do gerenciador de pacotes que instalou os programas do teste
            choco++;
            continue;
        }
        WCHAR *full = it->value ? wcat3(it->path, u" -> ", it->value) : wdup(it->path);
        for (size_t k = 0; k < tested.n && !owner; k++)
            if (belongs(tested.items[k], full)) owner = tested.items[k];
        if (owner) {
            linef(wfmt(u"  FICOU [%s] %c %s", owner->name, (WCHAR)it->kind, full));
            mine++;
        } else if (other < 400) {
            linef(wfmt(u"  outro  %c %s", (WCHAR)it->kind, full));
            other++;
        }
        // tipos de arquivo e protocolos que ficaram: mostra o conteúdo, para saber de quem são
        if (it->kind == TR_KEY && wifind(it->path, u"\\Classes\\") && !wifind(it->path, u"Local Settings") && !wifind(it->path, u"PackagedCom")) {
            HKEY k = reg_open(it->path, KEY_READ);
            if (k) {
                PtrVec vals = reg_values(k);
                for (size_t j = 0; j < vals.n && j < 8; j++) {
                    RegValue *v = vals.items[j];
                    WCHAR *t = (v->type == REG_SZ || v->type == REG_EXPAND_SZ) ? wdupn((const WCHAR *)v->data, v->size / 2) : wdup(u"(binário)");
                    linef(wfmt(u"         %s = %s", v->name[0] ? v->name : u"(padrão)", t));
                    xfree(t);
                }
                reg_values_free(&vals);
                PtrVec subs = reg_subkeys(k);
                for (size_t j = 0; j < subs.n && j < 8; j++) linef(wfmt(u"         [%s]", subs.items[j]));
                strings_free(&subs);
                HKEY c = reg_open_sub(k, u"shell\\open\\command");
                if (c) {
                    WCHAR *cmd = reg_get_str(c, u"");
                    linef(wfmt(u"         shell\\open\\command = %s", cmd ? cmd : u"-"));
                    xfree(cmd);
                    RegCloseKey(c);
                }
                RegCloseKey(k);
            }
        }
        xfree(full);
    }
    WCHAR *pm = env_path(u"HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment"), *pu = env_path(u"HKCU\\Environment");
    path_diff(pm_before, pm, u"sistema");
    path_diff(pu_before, pu, u"usuário");
    linef(wfmt(u"\r\nresultado: %u itens dos programas testados ficaram, %u itens novos de outros (ruído do Windows), %u do Chocolatey, %d problemas",
               (unsigned)mine, (unsigned)(s.out->items.n - mine - choco), (unsigned)choco, problems));
    flush();
    xfree(pm), xfree(pu), xfree(pm_before), xfree(pu_before);
    hs_free(&s.before);
    trace_free(s.out);
    for (size_t i = 0; i < tested.n; i++) {
        Tested *t = tested.items[i];
        xfree(t->name), identity_free(t->id), xfree(t);
    }
    pv_free(&tested);
    return (int)mine + problems;
}

// --scan <nome>: só a busca de sobras (moderado e avançado), sem desinstalar nem apagar nada.
// Mostra o que o EXTIRPA acha com o programa ainda instalado (o que a desinstalação forçada vê).
static int scan_only(const WCHAR *wanted) {
    PtrVec progs = programs_load();
    Program *p = NULL;
    for (size_t i = 0; i < progs.n && !p; i++)
        if (wieq(((Program *)progs.items[i])->name, wanted)) p = progs.items[i];
    for (size_t i = 0; i < progs.n && !p; i++)
        if (wifind(((Program *)progs.items[i])->name, wanted)) p = progs.items[i];
    if (!p) {
        linef(wfmt(u"NÃO ENCONTRADO: %s", wanted));
        flush();
        programs_free(&progs);
        return 1;
    }
    ULONGLONG t0 = GetTickCount64();
    Identity *id = program_identity(p);
    linef(wfmt(u"programa: %s (%s), entrada %s, identidade em %U ms", p->name, p->publisher ? p->publisher : u"-", p->key_path,
               (uint64_t)(GetTickCount64() - t0)));
    for (size_t i = 0; i < id->dirs.n; i++) linef(wfmt(u"  pasta: %s", id->dirs.items[i]));
    for (size_t i = 0; i < id->exact.n; i++) linef(wfmt(u"  nome exato: %s", id->exact.items[i]));
    static const ScanMode modes[] = {SCAN_MODERATE, SCAN_ADVANCED};
    for (size_t m = 0; m < COUNT(modes); m++) {
        section(modes[m] == SCAN_MODERATE ? u"moderado" : u"avançado");
        LeftList l = {0};
        t0 = GetTickCount64();
        scan_leftovers(&l, id, p->key_path, modes[m], NULL);
        left_finalize(&l);
        linef(wfmt(u"  %u itens em %U ms", (unsigned)l.items.n, (uint64_t)(GetTickCount64() - t0)));
        dump_left(&l, 400);
        left_free(&l);
        flush();
    }
    identity_free(id);
    programs_free(&progs);
    return 0;
}

int selftest_run(void) {
    g_dir = path_join(g_paths.data_dir, u"selftest");
    dir_create_all(g_dir);
    int argc = 0;
    WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 3 && wieq(argv[1], u"--snapshot-save")) return snapshot_save(argv[2]);
    if (argv && argc >= 3 && wieq(argv[1], u"--scan")) {
        WCHAR *old = g_dir;
        g_dir = path_join(old, u"scan");
        xfree(old);
        dir_create_all(g_dir);
        return scan_only(argv[2]);
    }
    if (argv && argc >= 4 && wieq(argv[1], u"--realtest")) {
        // relatório separado do autoteste geral
        WCHAR *old = g_dir;
        g_dir = path_join(old, u"real");
        xfree(old);
        dir_create_all(g_dir);
        return realtest(argc, argv);
    }
    PtrVec progs = {0};
    test_environment();
    test_programs(&progs);
    test_real_scans(&progs);
    test_synthetic_app();
    test_job_wait();
    test_trace();
    test_lists();
    test_ui(&progs);
    section(u"Resultado");
    linef(wfmt(u"%d OK, %d FALHAS", g_ok, g_fail));
    flush();
    programs_free(&progs);
    left_free(&g_sample);
    return g_fail;
}
