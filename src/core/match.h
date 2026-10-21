// SPDX-License-Identifier: MIT
// Identidade de um programa e a comparação de nomes que decide o que é sobra dele.
//
// Tudo é comparado na forma "normalizada": só letras e dígitos, em minúsculas. Assim
// "Mozilla Firefox", "mozilla-firefox" e "MozillaFirefox" são o mesmo nome.
#ifndef EXTIRPA_MATCH_H
#define EXTIRPA_MATCH_H

#include "wstr.h"

// Confiança de uma sobra. O nível decide a marcação inicial na lista e em quais modos
// de varredura ela aparece.
enum { CONF_NONE = 0, CONF_LOW = 1, CONF_MEDIUM = 2, CONF_HIGH = 3 };

typedef struct {
    WCHAR *display;         // nome como o Windows mostra
    WCHAR *clean;           // nome sem versão, arquitetura e idioma ("Mozilla Firefox")
    WCHAR *name_norm;       // "mozillafirefox"
    WCHAR *publisher;       // editor sem sufixos jurídicos ("Mozilla")
    WCHAR *publisher_norm;  // "mozilla" (NULL se genérico demais para servir de pasta)
    WCHAR *publisher_short; // sem as palavras de empresa no fim: "Nimbus Software" -> "nimbus" (ou NULL)
    WCHAR *pub_name_norm;   // editor + nome, sem repetir ("mozillafirefox")
    WCHAR *primary;         // a palavra mais característica ("firefox"), ou NULL
    PtrVec tokens;          // palavras características (normalizadas, >= 4 letras)
    PtrVec exact;           // nomes que contam como iguais ao programa (normalizados)
    PtrVec dirs;            // pastas do programa (normalizadas), nunca pastas protegidas
    PtrVec alt_dirs;        // as mesmas pastas em nome curto 8.3 (C:\PROGRA~1\...), só para referências
    WCHAR *vtag;            // versão curta ("312" para 3.12.0), para separar versões do mesmo programa; ou NULL
} Identity;

WCHAR *norm_alnum(const WCHAR *s);
WCHAR *clean_display_name(const WCHAR *display);
WCHAR *clean_publisher(const WCHAR *publisher);
bool is_generic_word(const WCHAR *norm);

Identity *identity_new(const WCHAR *display_name, const WCHAR *publisher);
// Pasta do programa (ignora pastas protegidas e repetidas). leaf_is_name: o nome da pasta
// passa a contar como nome exato do programa (pasta criada pelo instalador direto em
// Arquivos de Programas, ou escolhida pelo usuário na desinstalação forçada). Sem isso, o
// nome só conta se já parecer com o do programa: uma instalação em "C:\Jogos" não faz toda
// pasta "Jogos" virar sobra.
void identity_add_dir(Identity *id, const WCHAR *dir, bool leaf_is_name);
void identity_add_alt_dir(Identity *id, const WCHAR *dir);   // nome curto 8.3 de uma pasta do programa
void identity_set_version(Identity *id, const WCHAR *version);
void identity_add_exe(Identity *id, const WCHAR *exe_path);   // o nome do executável vira nome exato
void identity_add_exact(Identity *id, const WCHAR *name);     // qualquer outro nome exato
void identity_free(Identity *id);

int match_name(const Identity *id, const WCHAR *candidate);       // CONF_*
// Nome exato do programa mais um número de versão ("WinSCP 2"). Só vale dentro da pasta ou
// chave do editor, onde o contexto já é do programa.
bool match_name_versioned(const Identity *id, const WCHAR *candidate);
bool match_publisher(const Identity *id, const WCHAR *candidate);  // pasta/chave do editor
// O texto (comando, caminho, valor do Registro) aponta para dentro de uma pasta do programa?
bool match_path_ref(const Identity *id, const WCHAR *text);
// Comprimento da pasta mais longa (mais específica) da lista citada no texto; 0 se nenhuma.
// Serve para decidir de quem é uma referência quando um programa está dentro da pasta de
// outro: a pasta mais específica ganha.
size_t path_ref_len(const PtrVec *dirs, const WCHAR *text);

#endif
