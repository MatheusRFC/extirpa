// SPDX-License-Identifier: MIT
// Modo caçador: a janela principal some e fica só uma mira no canto da tela. Arraste a
// mira até a janela de um programa e solte para desinstalar, abrir a pasta, encerrar ou
// tirar da inicialização.
#ifndef EXTIRPA_HUNTER_H
#define EXTIRPA_HUNTER_H

#include "win32.h"

// Volta quando o usuário sai do modo caçador. Devolve true se algo foi desinstalado.
bool hunter_run(HWND main);

#endif
