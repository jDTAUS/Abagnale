/* $SchulteIT: object.c 15189 2025-10-27 05:41:45Z schulte $ */
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

#include "object.h"
#include "heap.h"
#include "proc.h"
#include "thread.h"

struct Object {
  size_t r_cnt;
  mtx_t *restrict mtx;
};

inline struct Object *Object_new(void) {
  struct Object *restrict const o = heap_malloc(sizeof(struct Object));
  o->r_cnt = 1;
  o->mtx = heap_malloc(sizeof(mtx_t));
  mutex_init(o->mtx);
  return o;
}

inline bool Object_delete(void *restrict const o) {
  if (o == NULL)
    return false;

  struct Object *restrict const obj = o;
  bool del = false;

  mutex_lock(obj->mtx);
  if (obj->r_cnt == 0) {
    werr("%s: %d: %s: o->r_cnt == 0\n", __FILE__, __LINE__, __func__);
    fatal();
  }

  obj->r_cnt--;
  if (obj->r_cnt == 0)
    del = true;

  mutex_unlock(obj->mtx);

  if (del) {
    mutex_destroy(obj->mtx);
    heap_free(obj->mtx);
    heap_free(obj);
  }

  return del;
}

inline struct Object *Object_copy(struct Object *restrict const o) {
  if (o == NULL)
    return NULL;

  mutex_lock(o->mtx);
  size_t saved_cnt = o->r_cnt;
  o->r_cnt++;
  if (o->r_cnt < saved_cnt) {
    werr("%s: %d: %s: Too many references\n", __FILE__, __LINE__, __func__);
    fatal();
  }
  mutex_unlock(o->mtx);
  return o;
}
