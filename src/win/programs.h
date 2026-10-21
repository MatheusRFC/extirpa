// SPDX-License-Identifier: MIT
// Programas instalados, lidos das três chaves "Uninstall" do Registro (as mesmas que o
// Windows usa em Aplicativos instalados).
#ifndef EXTIRPA_PROGRAMS_H
#define EXTIRPA_PROGRAMS_H

#include "../core/match.h"
#include "win32.h"

typedef enum { SCOPE_MACHINE64, SCOPE_MACHINE32, SCOPE_USER } Scope;

typedef struct {
    WCHAR *key_path;  // "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\<chave>"
    WCHAR *name, *publisher, *version;
    WCHAR *install_location, *uninstall, *quiet_uninstall, *display_icon;
    uint64_t size;  // bytes (EstimatedSize), 0 = desconhecido
    int year, month, day;  // data de instalação (0 = desconhecida)
    Scope scope;
    bool msi;          // registrado pelo Windows Installer
    bool no_remove;    // NoRemove=1: o próprio programa pede para não ser desinstalado
    int icon;          // índice na lista de ícones; -1 = ainda não carregado
    PtrVec msi_files;  // WCHAR*: arquivos-chave dos componentes do Windows Installer só deste produto
    bool msi_loaded;   // programs_load_msi já passou por ele
} Program;

PtrVec programs_load(void);  // Program*, ordenados por nome
void program_free(Program *p);
void programs_free(PtrVec *v);

// Arquivos de cada programa MSI da lista, lidos dos componentes do Windows Installer
// (UserData\<SID>\Components). É o que diz onde um MSI instalou os arquivos quando ele não
// grava InstallLocation. Uma passada só pelo Registro para todos os programas da lista.
void programs_load_msi(PtrVec *programs);
// Identidade para a busca de sobras: nome, editor e as pastas do programa.
Identity *program_identity(const Program *p);
// A identidade do programa que vai ser desinstalado: a de cima e, se ela não souber nenhuma
// pasta, as pistas do App Paths e dos atalhos do menu Iniciar com o nome exato do programa.
// Precisa ser montada antes de desinstalar.
Identity *program_identity_target(Program *p);
// Acrescenta o nome curto 8.3 de cada pasta da identidade (precisa que a pasta exista).
void identity_add_short_names(Identity *id);
// O programa a que pertence um executável (pela pasta de instalação), ou NULL.
Program *programs_find_by_exe(const PtrVec *v, const WCHAR *exe_path);
// A entrada do programa ainda existe no Registro?
bool program_still_registered(const Program *p);
// Caminho e índice do ícone (DisplayIcon, ou o próprio desinstalador).
WCHAR *program_icon_source(const Program *p, int *index);

#endif
