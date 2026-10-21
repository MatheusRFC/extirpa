// SPDX-License-Identifier: MIT
// Serviços básicos do Windows: memória, pastas do sistema, arquivos, log e processos.
#ifndef EXTIRPA_PLATFORM_H
#define EXTIRPA_PLATFORM_H

#include "../core/fmt.h"
#include "../core/wstr.h"
#include "win32.h"

#define EXTIRPA_VERSION u"1.0.0"

// Pastas do sistema e do usuário (todas normalizadas, sem '\' no fim; NULL se não existir)
typedef struct {
    WCHAR *windows, *system32, *exe;
    WCHAR *program_files, *program_files_x86, *common_files, *common_files_x86, *program_data;
    WCHAR *profile, *profiles_root, *public_dir, *appdata, *local_appdata, *locallow, *temp;
    WCHAR *desktop, *public_desktop, *documents, *public_documents;
    WCHAR *start_menu, *common_start_menu, *programs, *common_programs, *startup, *common_startup;
    WCHAR *data_dir, *backups_dir, *traces_dir, *log_path, *settings_path;
} Paths;

extern Paths g_paths;
extern FmtStyle g_fmt;

void plat_init(void);  // pastas, lista de proteção, pasta de dados

// arquivos
bool file_exists(const WCHAR *path);  // arquivo ou pasta
bool dir_exists(const WCHAR *path);
bool file_read_all(const WCHAR *path, uint8_t **data, size_t *len);
bool file_write_all(const WCHAR *path, const void *data, size_t len);
bool file_write_utf16(const WCHAR *path, const WCHAR *text);  // com BOM (formato dos .reg)
bool dir_create_all(const WCHAR *path);
uint64_t file_size(const WCHAR *path);

// texto
WCHAR *expand_env(const WCHAR *s);
WCHAR *win_error_text(DWORD err);  // mensagem do sistema para um código de erro
WCHAR *now_text(bool for_file_name);  // "2026-10-18 14:05:33" ou "20261018-140533"
WCHAR *filetime_text(const FILETIME *ft);  // data e hora local no estilo da interface

// log em %LOCALAPPDATA%\EXTIRPA\extirpa.log (UTF-8)
void log_text(const WCHAR *text);
void log_fmt(const WCHAR *pattern, ...);

// processos
// Roda um programa sem janela, espera terminar (ou o tempo esgotar) e devolve a saída
// (stdout+stderr) em UTF-8. Devolve o código de saída, ou -1 se não conseguiu executar.
int run_capture(const WCHAR *cmdline, char **out, size_t *out_len, DWORD timeout_ms);
typedef struct {
    DWORD pid;
    WCHAR *exe;
} ProcInfo;
// Processos em execução cujo executável está dentro de alguma das pastas/arquivos dados.
PtrVec processes_under(const PtrVec *paths);
void processes_free(PtrVec *v);
bool process_kill(DWORD pid);
void shell_open(const WCHAR *target);          // abre pasta, arquivo ou endereço
void shell_select(const WCHAR *path);          // abre o Explorer com o item selecionado
WCHAR *system_exe(const WCHAR *name);          // "C:\Windows\System32\<name>"

// configurações simples (chave=valor em settings.ini)
int setting_get_int(const WCHAR *key, int def);
void setting_set_int(const WCHAR *key, int value);

#endif
