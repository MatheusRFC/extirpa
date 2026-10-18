// SPDX-License-Identifier: MIT
// EXTIRPA: ponto de entrada. Sem CRT: o Windows chama WinMainCRTStartup direto.
#include "i18n.h"
#include "mainwin.h"
#include "platform.h"
#include "ui.h"

static int run(void) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    plat_init();
    i18n_init();
    ui_init();

    // Uma cópia só: duas desinstalações ao mesmo tempo se atrapalhariam.
    HANDLE mutex = CreateMutexW(NULL, TRUE, u"Local\\EXTIRPA-single-instance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(u"ExtirpaMain", NULL);
        if (other) {
            if (IsIconic(other)) ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        return 0;
    }
    log_fmt(u"EXTIRPA %s iniciado (PID %u, administrador: %s)", EXTIRPA_VERSION, (unsigned)GetCurrentProcessId(), IsUserAnAdmin() ? u"sim" : u"não");
    if (!IsUserAnAdmin()) msg(NULL, T(S_NOT_ADMIN), MB_OK | MB_ICONWARNING);

    HWND main = mainwin_create();
    if (!main) {
        msg(NULL, T(S_ERR_WINDOW), MB_OK | MB_ICONERROR);
        return 1;
    }
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (mainwin_pretranslate(&m)) continue;
        if (!IsDialogMessageW(main, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    CoUninitialize();
    log_text(u"EXTIRPA encerrado");
    return 0;
}

void WinMainCRTStartup(void) { ExitProcess((UINT)run()); }
