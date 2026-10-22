// SPDX-License-Identifier: MIT
// Busca de sobras (Registro, pastas, atalhos, inicialização, serviços) e a exclusão segura
// do que o usuário marcar: backup do Registro antes, arquivos para a Lixeira.
#ifndef EXTIRPA_SCAN_H
#define EXTIRPA_SCAN_H

#include "../core/leftover.h"
#include "win32.h"

typedef enum { SCAN_SAFE = 0, SCAN_MODERATE = 1, SCAN_ADVANCED = 2 } ScanMode;

// Controle de uma operação longa feita numa thread de trabalho.
typedef struct {
    volatile LONG cancel;                                  // diferente de zero: parar assim que der
    void (*progress)(void *ctx, const WCHAR *text);        // texto da etapa atual (pode ser NULL)
    void *ctx;
} Ctl;

void ctl_progress(Ctl *c, const WCHAR *text);
bool ctl_cancelled(const Ctl *c);

// uninstall_key: a entrada do programa em Uninstall (NULL na desinstalação forçada).
void scan_leftovers(LeftList *out, const Identity *id, const WCHAR *uninstall_key, ScanMode mode, Ctl *ctl);
enum { SCANF_BEFORE = 1 };  // análise antes de rodar o desinstalador: pula o que só ele resolve
void scan_leftovers_ex(LeftList *out, const Identity *id, const WCHAR *uninstall_key, ScanMode mode, unsigned flags, Ctl *ctl);
// O item ainda existe (no Registro, no disco, no Gerenciador de Serviços...)?
bool leftover_exists(const Leftover *it);
// Acrescenta a dst os itens de src que ainda existem (a análise de antes do desinstalador);
// devolve quantos eram novos.
size_t leftovers_add_existing(LeftList *dst, const LeftList *src);
// Pastas da lista que contêm a pasta de instalação de outro programa registrado descem
// para confiança baixa (desmarcadas). own_key: a entrada do próprio programa, ignorada.
void leftovers_guard_others(LeftList *l, const WCHAR *own_key);
// Calcula o tamanho das pastas e arquivos da lista.
void leftovers_measure(LeftList *l, Ctl *ctl);

typedef struct {
    int deleted, failed, kept;
    int permanent;       // apagados direto, sem passar pela Lixeira
    WCHAR *backup_path;  // .reg criado (NULL se não havia nada do Registro)
    bool backup_failed;  // o backup não pôde ser gravado: nada do Registro foi apagado
} DelResult;

// Apaga os itens marcados. label: nome do programa (para o nome do backup e o log).
void delete_leftovers(LeftList *l, const WCHAR *label, Ctl *ctl, DelResult *res);
void del_result_free(DelResult *r);

#endif
