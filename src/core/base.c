// SPDX-License-Identifier: MIT
// Vetor de ponteiros e ordenação estável (merge sort), sem biblioteca C.
#include "base.h"

void pv_push(PtrVec *v, void *item) {
    if (v->n == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 16;
        v->items = xrealloc(v->items, v->cap * sizeof(void *));
    }
    v->items[v->n++] = item;
}

void pv_free(PtrVec *v) {
    xfree(v->items);
    v->items = NULL;
    v->n = v->cap = 0;
}

static void merge_pass(void **src, void **dst, size_t n, size_t width, int (*cmp)(const void *, const void *, void *), void *ctx) {
    for (size_t lo = 0; lo < n; lo += 2 * width) {
        size_t mid = MIN(lo + width, n), hi = MIN(lo + 2 * width, n);
        size_t i = lo, j = mid, k = lo;
        while (i < mid && j < hi) dst[k++] = cmp(src[j], src[i], ctx) < 0 ? src[j++] : src[i++];
        while (i < mid) dst[k++] = src[i++];
        while (j < hi) dst[k++] = src[j++];
    }
}

void pv_sort(PtrVec *v, int (*cmp)(const void *a, const void *b, void *ctx), void *ctx) {
    if (v->n < 2) return;
    void **tmp = xalloc(v->n * sizeof(void *));
    void **a = v->items, **b = tmp;
    for (size_t w = 1; w < v->n; w *= 2) {
        merge_pass(a, b, v->n, w, cmp, ctx);
        void **t = a;
        a = b;
        b = t;
    }
    if (a != v->items) mem_copy(v->items, a, v->n * sizeof(void *));
    xfree(tmp);
}
