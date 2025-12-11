/* $SchulteIT: array.c 15189 2025-10-27 05:41:45Z schulte $ */
/* $JDTAUS$ */

/*
 * Copyright (c) 2018 - 2025 Christian Schulte <cs@schulte.it>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "array.h"
#include "heap.h"
#include "thread.h"

#include <string.h>

struct Array {
  void **items;
  size_t size;
  size_t capacity;
  mtx_t *restrict mtx;
};

struct Array *Array_new(const size_t c) {
  struct Array *restrict const a = heap_malloc(sizeof(struct Array));
  a->size = 0;
  a->capacity = ((c | !c) + 1) & ~1U;
  a->items = heap_calloc(a->capacity, sizeof(void *));
  a->mtx = heap_malloc(sizeof(mtx_t));
  mutex_init(a->mtx);
  return a;
}

inline void Array_delete(struct Array *restrict const a,
                         void (*cb)(void *restrict const)) {
  if (cb)
    for (size_t i = a->size; i > 0; i--)
      cb(a->items[i - 1]);

  mutex_destroy(a->mtx);
  heap_free(a->mtx);
  heap_free(a->items);
  heap_free(a);
}

inline void Array_lock(struct Array *restrict const a) { mutex_lock(a->mtx); }
inline void Array_unlock(struct Array *restrict const a) {
  mutex_unlock(a->mtx);
}

inline struct Array *Array_copy(const struct Array *restrict const a,
                                void *(*cb)(void *restrict const)) {
  struct Array *restrict const copy = Array_new(a->size);

  if (cb)
    for (size_t i = a->size; i > 0; i--)
      copy->items[i - 1] = cb(a->items[i - 1]);
  else
    for (size_t i = a->size; i > 0; i--)
      copy->items[i - 1] = a->items[i - 1];

  copy->size = a->size;
  return copy;
}

inline void Array_clear(struct Array *restrict const a,
                        void (*cb)(void *restrict const)) {
  if (cb)
    for (size_t i = a->size; i > 0; i--) {
      cb(a->items[i - 1]);
      a->items[i - 1] = NULL;
    }
  else
    for (size_t i = a->size; i > 0; i--)
      a->items[i - 1] = NULL;

  a->size = 0;
}

inline void Array_shrink(struct Array *restrict const a) {
  if (a->size < a->capacity) {
    a->capacity = ((a->size | !a->size) + 1) & ~1U;
    a->items = heap_realloc(a->items, sizeof(void *) * a->capacity);
    heap_trim(0);
  }
}

inline void Array_grow(struct Array *restrict const a) {
  if (a->size >= a->capacity) {
    a->capacity <<= 1;
    a->items = heap_realloc(a->items, sizeof(void *) * a->capacity);
    heap_trim(0);
  }
}

inline void Array_add_tail(struct Array *restrict const a,
                           void *restrict const i) {
  Array_grow(a);
  a->items[a->size] = i;
  a->size++;
}

inline void *Array_tail(const struct Array *restrict const a) {
  return a->size > 0 ? a->items[a->size - 1] : NULL;
}

inline void Array_add_head(struct Array *restrict const a,
                           void *restrict const i) {
  Array_grow(a);

  if (a->size > 0)
    memmove(&a->items[1], &a->items[0], sizeof(void *) * a->size);

  a->items[0] = i;
  a->size++;
}

inline void *Array_head(const struct Array *restrict const a) {
  return a->size > 0 ? a->items[0] : NULL;
}

inline void *Array_remove_tail(struct Array *restrict const a) {
  return Array_remove_idx(a, a->size - 1);
}

inline void *Array_remove_head(struct Array *restrict const a) {
  return Array_remove_idx(a, 0);
}

inline void *Array_remove_idx(struct Array *restrict const a, const size_t i) {
  void *restrict const item = a->items[i];

  if (i < a->size - 1)
    memmove(&a->items[i], &a->items[i + 1], sizeof(void *) * (a->size - 1 - i));

  a->items[--a->size] = NULL;
  return item;
}

inline const size_t Array_size(const struct Array *restrict const a) {
  return a->size;
}

inline void **Array_items(const struct Array *restrict const a) {
  return a->items;
}
