/* $SchulteIT: array.c 15189 2025-10-27 05:41:45Z schulte $ */
/* $JDTAUS: array.c 9639 2026-07-22 06:07:19Z schulte $ */

/*
 * Copyright (c) 2018 - 2026 Christian Schulte <cs@schulte.it>
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

#ifdef HAVE_HOST_H
#include "host.h"
#endif

#ifdef MULTI_THREADED
#include "thread.h"
#endif

#include "array.h"
#include "heap.h"

#include <string.h>

struct Array {
  void **items;
  size_t size;
  size_t offset;
  size_t capacity;
#ifdef MULTI_THREADED
  mtx_t mtx;
#endif
};

void Array_grow(struct Array *restrict const);
void Array_shrink(struct Array *restrict const);

struct Array *Array_new(const size_t c) {
  struct Array *restrict const a = heap_malloc(sizeof(struct Array));
  a->size = 0;
  a->offset = 0;
  a->capacity = ((c | !c) + 1) & ~1U;
  a->items = heap_calloc(a->capacity, sizeof(void *));
#ifdef MULTI_THREADED
  mutex_init(&a->mtx);
#endif
  return a;
}

inline void Array_delete(struct Array *restrict const a,
                         void (*i_delete)(void *restrict const)) {
  if (i_delete)
    for (size_t i = a->size; i-- > 0;)
      i_delete(a->items[a->offset + i]);

#ifdef MULTI_THREADED
  mutex_destroy(&a->mtx);
#endif
  heap_free(a->items);
  heap_free(a);
}

inline struct Array *Array_copy(const struct Array *restrict const a,
                                void *(*i_copy)(void *restrict const)) {
  struct Array *restrict const copy = Array_new(a->size);

  if (i_copy)
    for (size_t i = a->size; i-- > 0;)
      copy->items[i] = i_copy(a->items[a->offset + i]);
  else
    for (size_t i = a->size; i-- > 0;)
      copy->items[i] = a->items[a->offset + i];

  copy->size = a->size;
  return copy;
}

inline void Array_cut(struct Array *restrict const a, const size_t i,
                      const size_t cnt,
                      void (*i_delete)(void *restrict const)) {
  if (i_delete) {
    size_t j = i;
    while (j != 0)
      i_delete(a->items[a->offset + i - j--]);
    j = a->size - (i + cnt);
    while (j != 0)
      i_delete(a->items[a->offset + a->size - j--]);
  }

  a->offset += i;
  a->size = cnt;
  Array_shrink(a);
}

inline void Array_clear(struct Array *restrict const a,
                        void (*i_delete)(void *restrict const)) {
  if (i_delete)
    for (size_t i = a->size; i-- > 0;) {
      i_delete(a->items[a->offset + i]);
      a->items[a->offset + i] = NULL;
    }
  else
    for (size_t i = a->size; i-- > 0;)
      a->items[a->offset + i] = NULL;

  a->size = 0;
  Array_shrink(a);
}

inline void Array_compact(struct Array *restrict const a) {
  if (a->offset > 0) {
    memmove(&a->items[0], &a->items[a->offset], sizeof(void *) * a->size);
    a->offset = 0;
  }
  a->capacity = ((a->size | !a->size) + 1) & ~1U;
  a->items = heap_reallocarray(a->items, a->capacity, sizeof(void *));
  heap_trim(0);
}

inline void Array_shrink(struct Array *restrict const a) {
  if (a->size < ((a->capacity - a->offset) >> 2)) {
    if (a->offset > 0) {
      memmove(&a->items[0], &a->items[a->offset], sizeof(void *) * a->size);
      a->offset = 0;
    }
    a->capacity = (((a->capacity >> 1) | !(a->capacity >> 1)) + 1) & ~1U;
    a->items = heap_reallocarray(a->items, a->capacity, sizeof(void *));
    heap_trim(0);
  }
}

inline void Array_grow(struct Array *restrict const a) {
  if (a->size >= a->capacity - a->offset) {
    if (a->offset > 0) {
      memmove(&a->items[0], &a->items[a->offset], sizeof(void *) * a->size);
      a->offset = 0;
    } else {
      a->capacity <<= 1;
      a->items = heap_reallocarray(a->items, a->capacity, sizeof(void *));
      heap_trim(0);
    }
  }
}

inline void Array_add_tail(struct Array *restrict const a,
                           void *restrict const i) {
  Array_grow(a);
  a->items[a->offset + a->size++] = i;
}

inline void *Array_tail(const struct Array *restrict const a) {
  return a->size > 0 ? a->items[a->offset + a->size - 1] : NULL;
}

inline void Array_add_head(struct Array *restrict const a,
                           void *restrict const i) {
  Array_grow(a);

  if (a->offset == 0) {
    if (a->size > 0)
      memmove(&a->items[1], &a->items[0], sizeof(void *) * a->size);

    a->offset++;
  }

  a->items[--a->offset] = i;
  a->size++;
}

inline void *Array_head(const struct Array *restrict const a) {
  return a->size > 0 ? a->items[a->offset] : NULL;
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
    memmove(&a->items[a->offset + i], &a->items[a->offset + i + 1],
            sizeof(void *) * (a->size - 1 - i));

  a->items[a->offset + --a->size] = NULL;
  return item;
}

inline const size_t Array_size(const struct Array *restrict const a) {
  return a->size;
}

inline void *const *Array_items(const struct Array *restrict const a) {
  return &a->items[a->offset];
}

#ifdef MULTI_THREADED
inline mtx_t *Array_mutex(struct Array *restrict const a) { return &a->mtx; }
inline void Array_lock(struct Array *restrict const a) { mutex_lock(&a->mtx); }
inline bool Array_trylock(struct Array *restrict const a) {
  return mutex_trylock(&a->mtx);
}
inline void Array_unlock(struct Array *restrict const a) {
  mutex_unlock(&a->mtx);
}
#endif
