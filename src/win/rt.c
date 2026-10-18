// SPDX-License-Identifier: MIT
// O mínimo de "biblioteca C" que o compilador espera encontrar quando não há CRT:
// memset/memcpy/memmove/memcmp (o clang gera chamadas a elas para cópias e zeragens
// grandes) e o símbolo _fltused (exigido no Windows quando o programa usa ponto flutuante).
#include <stddef.h>

int _fltused = 0x9875;

// As funções são escritas com "rep movsb/stosb" para o compilador não transformar o laço
// de volta numa chamada a si mesma.
void *memset(void *dst, int c, size_t n) {
    void *d = dst;
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory");
    return dst;
}

void *memcpy(void *restrict dst, const void *restrict src, size_t n) {
    void *d = dst;
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(src), "+c"(n) : : "memory");
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d == s || !n) return dst;
    if (d < s || d >= s + n) {
        __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    } else {
        // sobreposição com destino depois da origem: copia de trás para frente
        d += n - 1;
        s += n - 1;
        __asm__ volatile("std\n\trep movsb\n\tcld" : "+D"(d), "+S"(s), "+c"(n) : : "memory");
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
        __asm__ volatile("" ::: "memory");  // impede a troca do laço por uma chamada a memcmp
    }
    return 0;
}
