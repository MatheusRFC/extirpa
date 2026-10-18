// SPDX-License-Identifier: MIT
// Peças comuns da interface: fontes por DPI, criação de controles, lista (ListView),
// janelas modais e mensagens.
#ifndef EXTIRPA_UI_H
#define EXTIRPA_UI_H

#include "../core/wstr.h"
#include "win32.h"

// Mensagens internas
#define WM_APP_STEP (WM_APP + 1)    // lParam: WCHAR* (quem recebe libera)
#define WM_APP_DETAIL (WM_APP + 2)  // lParam: WCHAR*
#define WM_APP_DONE (WM_APP + 3)    // a thread de trabalho terminou
#define WM_APP_ASK (WM_APP + 4)     // wParam: flags do MessageBox, lParam: texto; devolve a resposta
#define WM_APP_BUTTON (WM_APP + 5)  // wParam: 1 mostra / 0 esconde o botão extra, lParam: WCHAR* rótulo
#define WM_APP_LOADED (WM_APP + 6)  // dados carregados em segundo plano (lParam: ponteiro do dono)

extern HINSTANCE g_inst;
extern HICON g_icon_big, g_icon_small;
extern UINT g_dpi;

void ui_init(void);
void ui_set_dpi(UINT dpi);  // refaz as fontes
int S(int v);               // escala para o DPI atual (96 = 100%)
HFONT ui_font(void);
HFONT ui_font_bold(void);
HFONT ui_font_title(void);
// Estado da janela (o ponteiro passado em ui_window), guardado em GWLP_USERDATA.
void *ui_state(HWND h, UINT m, LPARAM lp);

ATOM ui_register(const WCHAR *cls, WNDPROC proc);
HWND ui_child(HWND parent, const WCHAR *cls, const WCHAR *text, DWORD style, DWORD exstyle, int id);
HWND ui_button(HWND parent, const WCHAR *text, int id);
HWND ui_check(HWND parent, const WCHAR *text, int id, bool checked);
HWND ui_radio(HWND parent, const WCHAR *text, int id, bool first, bool checked);
HWND ui_label(HWND parent, const WCHAR *text, int id);
HWND ui_edit(HWND parent, const WCHAR *text, int id, DWORD extra_style);
bool ui_checked(HWND h);
void ui_set_checked(HWND h, bool on);
WCHAR *ui_text(HWND h);
void ui_place(HWND h, int x, int y, int w, int hh);
int ui_button_width(const WCHAR *text);  // largura aproximada, já em pixels do DPI atual

// ListView
HWND lv_create(HWND parent, int id, DWORD style, DWORD exstyle);
void lv_column(HWND lv, int i, const WCHAR *text, int width, bool right);
int lv_insert(HWND lv, int i, const WCHAR *text, LPARAM param);
void lv_text(HWND lv, int i, int sub, const WCHAR *text);
LPARAM lv_param(HWND lv, int i);
int lv_count(HWND lv);
int lv_selected(HWND lv, int after);  // próximo selecionado depois de `after` (-1 = do início); -1 = nenhum
int lv_selected_count(HWND lv);
void lv_select(HWND lv, int i, bool only);
bool lv_checked(HWND lv, int i);
void lv_set_checked(HWND lv, int i, bool on);
void lv_sort_arrow(HWND lv, int col, int dir);  // dir: 1 crescente, -1 decrescente, 0 nenhum
void lv_clear(HWND lv);

// Mensagem com o título do programa
int msg(HWND owner, const WCHAR *text, UINT flags);

// Janelas modais feitas à mão: o estado de cada uma começa com ModalState.
typedef struct {
    int result;
    bool done;
} ModalState;
int ui_modal(HWND w, HWND owner, ModalState *ms);  // mostra, espera fechar, destrói
void ui_end(ModalState *ms, int result);
void ui_center(HWND w, HWND owner, int cx, int cy);  // tamanho em pixels do DPI atual
HWND ui_window(const WCHAR *cls, const WCHAR *title, HWND owner, void *state, DWORD style);

#endif
