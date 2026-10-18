// SPDX-License-Identifier: MIT
// Strings UTF-16 (as mesmas da API do Windows) e conversão para UTF-8.
#ifndef EXTIRPA_WSTR_H
#define EXTIRPA_WSTR_H

#include "base.h"

size_t wlen(const WCHAR *s);
WCHAR *wdup(const WCHAR *s);  // NULL vira ""
WCHAR *wdupn(const WCHAR *s, size_t n);
WCHAR *wcat(const WCHAR *a, const WCHAR *b);
WCHAR *wcat3(const WCHAR *a, const WCHAR *b, const WCHAR *c);
WCHAR *w_from_ascii(const char *s);

// Minúscula para comparar sem diferenciar maiúsculas: ASCII, Latin-1, Latin Extended-A,
// grego e cirílico básicos. É o suficiente para nomes de programas, pastas e chaves.
WCHAR wfold(WCHAR c);
int wcmp(const WCHAR *a, const WCHAR *b);
int wicmp(const WCHAR *a, const WCHAR *b);
bool weq(const WCHAR *a, const WCHAR *b);
bool wieq(const WCHAR *a, const WCHAR *b);
bool wieqn(const WCHAR *a, const WCHAR *b, size_t n);
bool wistarts(const WCHAR *s, const WCHAR *prefix);
bool wiends(const WCHAR *s, const WCHAR *suffix);
const WCHAR *wifind(const WCHAR *hay, const WCHAR *needle);  // sem diferenciar maiúsculas
const WCHAR *wfindc(const WCHAR *s, WCHAR c);
const WCHAR *wrfindc(const WCHAR *s, WCHAR c);
bool w_is_space(WCHAR c);
bool w_is_digit(WCHAR c);
bool w_is_alnum(WCHAR c);  // letras (qualquer alfabeto) e dígitos
WCHAR *wtrim_dup(const WCHAR *s);
bool w_to_u64(const WCHAR *s, uint64_t *out);  // só dígitos decimais, sem sinal

// Construtor de strings. Zerado ({0}) já está pronto para usar.
typedef struct {
    WCHAR *p;
    size_t len, cap;
} StrBuf;

void sb_addn(StrBuf *b, const WCHAR *s, size_t n);
void sb_add(StrBuf *b, const WCHAR *s);
void sb_addc(StrBuf *b, WCHAR c);
void sb_add_ascii(StrBuf *b, const char *s);
void sb_add_u64(StrBuf *b, uint64_t v);
void sb_add_u64_pad(StrBuf *b, uint64_t v, int width);  // completa com zeros à esquerda
void sb_add_hex(StrBuf *b, uint64_t v, int digits);     // minúsculas, exatamente `digits` dígitos
const WCHAR *sb_str(StrBuf *b);                          // sempre terminada em zero
WCHAR *sb_take(StrBuf *b);                               // entrega a string e zera o construtor
void sb_free(StrBuf *b);

// UTF-8 <-> UTF-16. Sequências inválidas viram U+FFFD. O resultado termina em zero.
char *w_to_utf8(const WCHAR *s, size_t n, size_t *out_len);
WCHAR *utf8_to_w(const char *s, size_t n, size_t *out_len);

#endif
