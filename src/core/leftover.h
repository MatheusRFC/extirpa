// SPDX-License-Identifier: MIT
// Lista de sobras encontradas (o que a varredura ou o rastreamento sugerem apagar).
#ifndef EXTIRPA_LEFTOVER_H
#define EXTIRPA_LEFTOVER_H

#include "match.h"
#include "trace.h"

typedef enum {
    LK_KEY,           // chave do Registro (com tudo dentro)
    LK_VALUE,         // um valor dentro de uma chave
    LK_KEY_IF_EMPTY,  // chave do editor: só sai se ficar vazia depois das outras exclusões
    LK_DIR,           // pasta (com tudo dentro)
    LK_FILE,          // arquivo
    LK_DIR_IF_EMPTY,  // pasta do editor: só sai se ficar vazia
    LK_SERVICE,       // serviço do Windows (removido pelo Gerenciador de Serviços, não pelo Registro)
    LK_TASK,          // tarefa agendada (path = nome completo, "\Pasta\Tarefa"; removida pelo schtasks)
    LK_FWRULE,        // regra do Firewall do Windows (path = ID da regra; removida pelo PowerShell)
    LK_PATHENTRY,     // entrada da variável PATH (path = chave do ambiente, value = a entrada)
    LK_APPX,          // pacote do Windows (path = PackageFullName; removido pelo Remove-AppxPackage).
                      // Ex.: o pacote "esparso" que põe o programa no menu de contexto do Windows 11
} LeftKind;

// Por que o item foi considerado sobra (o texto de cada motivo fica na interface).
typedef enum {
    LR_UNINSTALL_KEY,  // a entrada do programa em "Aplicativos instalados" ficou para trás
    LR_INSTALL_DIR,    // a pasta de instalação
    LR_NAME,           // nome igual ou parecido com o do programa
    LR_PUBLISHER,      // pasta/chave do editor, vazia depois da limpeza
    LR_PATH_REF,       // aponta para a pasta do programa
    LR_RUN,            // inicialização automática
    LR_APP_PATHS,      // registro "App Paths" do executável
    LR_SHARED_DLL,     // contador de DLL compartilhada
    LR_SERVICE,        // serviço instalado pelo programa
    LR_SHORTCUT,       // atalho que aponta para o programa
    LR_COM,            // componente COM do programa
    LR_FILE_ASSOC,     // tipo de arquivo/aplicativo registrado pelo programa
    LR_TRACE,          // criado durante a instalação rastreada
    LR_USER,           // escolhido na desinstalação forçada
    LR_TASK,           // tarefa agendada que roda o programa
    LR_FIREWALL,       // regra do firewall para o programa
    LR_PATH_VAR,       // pasta do programa na variável PATH
    LR_USAGE,          // rastro de uso guardado pelo Windows (MuiCache, compatibilidade)
    LR_SHELL_EXT,      // extensão do Explorer (menu de contexto, ícones) do programa
    LR_REFERENCE,      // referência a um item do programa que também está sendo removido
    LR_PACKAGE,        // pacote do Windows registrado pelo programa
    LR_MSI,            // registro ou cache do Windows Installer deste programa
    LR_EXE_TRACE,      // guardado pelo Windows com o nome do executável (Prefetch, relatórios de erro...)
} LeftReason;

typedef struct {
    LeftKind kind;
    WCHAR *path;   // chave, caminho ou nome do serviço
    WCHAR *value;  // nome do valor (LK_VALUE; "" = valor padrão)
    int conf;      // CONF_LOW..CONF_HIGH
    LeftReason reason;
    WCHAR *detail;  // informação extra para o usuário (o comando que apontava para o programa...)
    bool checked;
    uint64_t size;  // pastas e arquivos (0 = desconhecido)
    int status;     // LS_* (preenchido na exclusão)
    uint32_t error; // código de erro do Windows quando status == LS_FAILED
} Leftover;

enum { LS_PENDING = 0, LS_DELETED = 1, LS_FAILED = 2, LS_KEPT = 3 /* "se ficar vazia" e não ficou */ };

typedef struct {
    PtrVec items;  // Leftover*
} LeftList;

// Chaves de ambiente onde fica a variável PATH (as únicas aceitas em LK_PATHENTRY)
#define ENV_KEY_MACHINE u"HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment"
#define ENV_KEY_USER u"HKCU\\Environment"
// Pacotes do Windows (AppX/MSIX) registrados na máquina: uma subchave por PackageFullName
#define APPX_PACKAGES_KEY u"HKLM\\SOFTWARE\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\PackageRepository\\Packages"
#define FIREWALL_RULES_KEY u"HKLM\\SYSTEM\\CurrentControlSet\\Services\\SharedAccess\\Parameters\\FirewallPolicy\\FirewallRules"

bool left_is_registry(LeftKind k);
bool left_has_value(LeftKind k);  // LK_VALUE e LK_PATHENTRY usam o campo value
bool left_is_file(LeftKind k);
// Nome de pacote aceito em LK_APPX: só letras, dígitos, '.', '_', '-' e '~', e nunca um pacote
// da Microsoft ou do Windows
bool left_appx_name_ok(const WCHAR *full_name);
// Acrescenta respeitando a lista de proteção; repetido mantém a maior confiança.
// Devolve o item (novo ou existente) ou NULL se bloqueado.
Leftover *left_add(LeftList *l, LeftKind kind, const WCHAR *path, const WCHAR *value, int conf, LeftReason reason, const WCHAR *detail);
// Tira itens já cobertos por um ancestral de confiança igual ou maior, marca os de alta
// confiança e ordena (Registro primeiro, depois arquivos; por caminho).
void left_finalize(LeftList *l);
void left_free(LeftList *l);
// Itens de um rastreamento de instalação como sobras. Chaves sensíveis (dados do Windows
// Installer, configurações do Windows...) entram desmarcadas.
void left_add_trace(LeftList *l, const Trace *t);

#endif
