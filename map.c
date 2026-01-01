/* $SchulteIT: map.c 15189 2025-10-27 05:41:45Z schulte $ */
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

#include "map.h"
#include "heap.h"
#include "thread.h"

struct Entry {
  struct String *restrict key;
  void *restrict value;
  struct Entry *restrict next;
};

struct Map {
  size_t capacity;
  struct Entry **restrict buckets;
  mtx_t mtx;
};

struct MapIterator {
  size_t i;
  struct Entry *restrict e;
  const struct Map *restrict m;
};

inline struct Map *Map_new(const size_t capacity) {
  struct Map *restrict const m = heap_malloc(sizeof(struct Map));
  m->capacity = capacity;
  m->buckets = heap_calloc(capacity, sizeof(struct Entry *));
  mutex_init(&m->mtx);
  return m;
}

inline void Map_delete(struct Map *restrict const m,
                       void (*cb)(void *restrict const)) {
  for (size_t i = m->capacity; i > 0; i--) {
    struct Entry *restrict bucket = m->buckets[i - 1];

    while (bucket != NULL) {
      if (cb)
        cb(bucket->value);

      String_delete(bucket->key);

      struct Entry *tmp = bucket;
      bucket = bucket->next;
      heap_free(tmp);
    }
  }

  mutex_destroy(&m->mtx);
  heap_free(m->buckets);
  heap_free(m);
}

inline void Map_lock(struct Map *restrict const m) { mutex_lock(&m->mtx); }
inline void Map_unlock(struct Map *restrict const m) { mutex_unlock(&m->mtx); }

inline void *Map_put(struct Map *restrict const m,
                     struct String *restrict const key,
                     void *restrict const e) {
  const size_t i = String_hash(key) % m->capacity;
  struct Entry *restrict bucket = m->buckets[i];
  void *restrict needle = NULL;

  while (bucket != NULL && !String_equals(bucket->key, key))
    bucket = bucket->next;

  if (bucket != NULL) {
    needle = bucket->value;
    bucket->value = e;
  } else {
    bucket = heap_malloc(sizeof(struct Entry));
    bucket->key = String_copy(key);
    bucket->value = e;
    bucket->next = m->buckets[i];
    m->buckets[i] = bucket;
  }

  return needle;
}

inline void *Map_get(const struct Map *restrict const m,
                     const struct String *restrict const key) {
  struct Entry *restrict bucket = m->buckets[String_hash(key) % m->capacity];

  while (bucket != NULL && !String_equals(bucket->key, key))
    bucket = bucket->next;

  return bucket != NULL ? bucket->value : NULL;
}

inline struct MapIterator *MapIterator_new(const struct Map *restrict const m) {
  struct MapIterator *restrict const it =
      heap_malloc(sizeof(struct MapIterator));

  it->i = m->capacity;
  it->e = NULL;
  it->m = m;
  return it;
}

inline void MapIterator_delete(struct MapIterator *restrict const it) {
  heap_free(it);
}

inline bool MapIterator_next(struct MapIterator *restrict const it) {
  if (it->e != NULL)
    it->e = it->e->next;

  for (; it->i > 0; it->i--) {
    it->e = it->m->buckets[it->i - 1];
    if (it->e != NULL)
      break;
  }

  return it->e != NULL;
}

inline void *MapIterator_remove(struct MapIterator *restrict const it) {
  void *restrict value = NULL;

  if (it->e != NULL) {
    it->m->buckets[it->i - 1] = it->e->next;
    value = it->e->value;
    String_delete(it->e->key);
    heap_free(it->e);
    it->e = NULL;
  }

  return value;
}

inline const struct String *
MapIterator_key(const struct MapIterator *restrict const it) {
  return it->e != NULL ? it->e->key : NULL;
}

inline const void *
MapIterator_value(const struct MapIterator *restrict const it) {
  return it->e != NULL ? it->e->value : NULL;
}
