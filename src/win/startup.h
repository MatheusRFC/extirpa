// SPDX-License-Identifier: MIT
// Gerenciador de inicialização: o que abre junto com o Windows (chaves Run/RunOnce e
// pastas Inicializar), com ativar, desativar e excluir.
//
// Ativar/desativar usa o mesmo lugar que o Gerenciador de Tarefas do Windows
// (...\Explorer\StartupApproved). Esse formato não é documentado pela Microsoft: o
// primeiro byte par (02) significa ativado e ímpar (03), desativado.
#ifndef EXTIRPA_STARTUP_H
#define EXTIRPA_STARTUP_H

#include "../core/wstr.h"
#include "win32.h"

typedef enum { SRC_HKCU_RUN, SRC_HKCU_RUNONCE, SRC_HKLM_RUN, SRC_HKLM_RUNONCE, SRC_HKLM32_RUN, SRC_HKLM32_RUNONCE, SRC_USER_FOLDER, SRC_COMMON_FOLDER } StartupSource;

typedef struct {
    WCHAR *name;     // nome do valor ou do arquivo
    WCHAR *command;  // comando (ou o alvo do atalho)
    WCHAR *location; // chave do Registro ou caminho do arquivo
    StartupSource source;
    bool enabled;
    bool can_toggle;  // RunOnce não tem ativar/desativar
} StartupEntry;

PtrVec startup_list(void);
void startup_list_free(PtrVec *v);
const WCHAR *startup_source_text(StartupSource s);
DWORD startup_set_enabled(const StartupEntry *e, bool enabled);
// Exclui (backup .reg antes, ou o atalho para a Lixeira). backup_path recebe o .reg criado.
DWORD startup_delete(const StartupEntry *e, WCHAR **backup_path);
// Executável a que o comando se refere (para "abrir local" e o modo caçador).
WCHAR *startup_exe(const StartupEntry *e);

#endif
