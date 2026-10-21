// SPDX-License-Identifier: MIT
// Linhas de comando de desinstalação (UninstallString e QuietUninstallString).
#ifndef EXTIRPA_CMDLINE_H
#define EXTIRPA_CMDLINE_H

#include "wstr.h"

typedef struct {
    WCHAR *exe;   // caminho do executável, sem aspas
    WCHAR *args;  // o resto, sem espaços nas pontas
} Cmd;

// Separa executável e argumentos. Aceita o executável entre aspas ou sem aspas e com
// espaços no caminho ("C:\Program Files\Foo\uninst.exe /S", erro comum dos instaladores).
bool cmd_split(const WCHAR *cmdline, Cmd *out);
void cmd_free(Cmd *c);
WCHAR *cmd_join(const WCHAR *exe, const WCHAR *args);  // coloca o executável entre aspas

bool guid_valid(const WCHAR *s);  // "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}"
// Forma "compactada" que o Windows Installer usa no Registro ({923A99B5-32DE-...} ->
// "5B99A329ED23..."): 32 caracteres em maiúsculas + zero. false se o GUID for inválido.
bool guid_pack(const WCHAR *guid, WCHAR out[33]);
// Se o comando for do Windows Installer (msiexec /I ou /X {produto}), copia o código do
// produto (38 caracteres + zero) e devolve true.
bool cmd_msi_product(const Cmd *c, WCHAR product[39]);

typedef enum {
    UNINST_UNKNOWN,
    UNINST_MSI,   // Windows Installer
    UNINST_INNO,  // Inno Setup (unins000.exe)
} UninstKind;

UninstKind cmd_kind(const Cmd *c);

// Argumentos do msiexec para remover um produto. quiet: sem perguntas, só a barra de
// progresso. O executável (msiexec.exe da pasta do sistema) fica por conta de quem chama.
WCHAR *msi_uninstall_args(const WCHAR *product, bool quiet);
// Linha de comando silenciosa para a desinstalação rápida, ou NULL se não houver como
// saber os parâmetros silenciosos desse desinstalador.
WCHAR *cmd_quiet_variant(const WCHAR *uninstall, const WCHAR *quiet_uninstall);

#endif
