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
} Leftover;

typedef struct {
    PtrVec items;  // Leftover*
} LeftList;

bool left_is_registry(LeftKind k);
bool left_is_file(LeftKind k);
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
