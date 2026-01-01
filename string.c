/* $SchulteIT: string.c 15189 2025-10-27 05:41:45Z schulte $ */
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

#include "string.h"
#include "heap.h"
#include "proc.h"
#include "thread.h"

#include <stdint.h>
#include <string.h>

struct String {
  char *restrict s;
  size_t len;
  size_t hc;
  size_t r_cnt;
  mtx_t mtx;
};

inline struct String *String_cnew(const char *restrict s) {
  size_t hc = 5381;
  const char *s_p;

  for (s_p = s; *s_p; s_p++)
    hc = ((hc << 5) + hc) + (unsigned char)*s_p;

  struct String *restrict const str = heap_malloc(sizeof(struct String));
  str->len = s_p - s;
  str->hc = hc;

  // str->len + 1 <= SIZE_MAX
  // => str->len <= SIZE_MAX - 1
  if (str->len > SIZE_MAX - 1) {
    werr("%s: %d: %s\n", __FILE__, __LINE__, __func__);
    fatal();
  }

  str->s = heap_calloc(str->len + 1, sizeof(char));
  memcpy(str->s, s, str->len);
  mutex_init(&str->mtx);
  str->r_cnt = 1;
  return str;
}

inline struct String *String_cnnew(const char *restrict s, size_t maxlen) {
  size_t hc = 5381;
  const char *s_p;

  for (s_p = s; *s_p && maxlen != 0; s_p++, maxlen--)
    hc = ((hc << 5) + hc) + (unsigned char)*s_p;

  struct String *restrict const str = heap_malloc(sizeof(struct String));
  str->len = s_p - s;
  str->hc = hc;

  // str->len + 1 <= SIZE_MAX
  // => str->len <= SIZE_MAX - 1
  if (str->len > SIZE_MAX - 1) {
    werr("%s: %d: %s\n", __FILE__, __LINE__, __func__);
    fatal();
  }

  str->s = heap_calloc(str->len + 1, sizeof(char));
  memcpy(str->s, s, str->len);
  mutex_init(&str->mtx);
  str->r_cnt = 1;
  return str;
}

inline struct String *String_new(const struct String *restrict s,
                                 const size_t i, const size_t c) {
  const char *s_p;
  // i + c + 1 <= SIZE_MAX
  // => i <= SIZE_MAX - c - 1
  // => c <= SIZE_MAX - i - 1
  if (i > SIZE_MAX - c - 1 || c > SIZE_MAX - i - 1 || i + c > s->len) {
    werr("%s: %d :%s: %s: %zu: %zu\n", __FILE__, __LINE__, __func__, s->s, i,
         c);
    fatal();
  }

  struct String *restrict const str = heap_malloc(sizeof(struct String));
  str->len = c;
  str->hc = 5381;
  str->s = heap_calloc(str->len + 1, sizeof(char));
  memcpy(str->s, s->s + i, str->len);

  for (s_p = str->s; *s_p; s_p++)
    str->hc = ((str->hc << 5) + str->hc) + (unsigned char)*s_p;

  mutex_init(&str->mtx);
  str->r_cnt = 1;
  return str;
}

inline const char *String_chars(const struct String *restrict const s) {
  return s->s;
}

inline const size_t String_length(const struct String *restrict const s) {
  return s->len;
}

inline const size_t String_hash(const struct String *restrict const s) {
  return s->hc;
}

inline void String_delete(void *restrict const s) {
  if (s == NULL)
    return;

  struct String *restrict const str = s;

  mutex_lock(&str->mtx);

  if (str->r_cnt-- == 0) {
    werr("%s: %d: %s\n", __FILE__, __LINE__, __func__);
    fatal();
  }

  if (str->r_cnt > 0) {
    mutex_unlock(&str->mtx);
    return;
  }

  mutex_unlock(&str->mtx);
  mutex_destroy(&str->mtx);
  heap_free(str->s);
  heap_free(s);
}

inline struct String *String_copy(struct String *restrict const str) {
  if (str == NULL)
    return NULL;

  mutex_lock(&str->mtx);

  // str->r_cnt + 1 <= SIZE_MAX
  // => str->r_cnt <= SIZE_MAX - 1
  if (str->r_cnt > SIZE_MAX - 1) {
    werr("%s: %d: %s\n", __FILE__, __LINE__, __func__);
    fatal();
  }

  str->r_cnt++;

  mutex_unlock(&str->mtx);
  return str;
}

inline bool String_equals(const struct String *restrict const s1,
                          const struct String *restrict const s2) {
  return s1->hc == s2->hc && strcmp(s1->s, s2->s) == 0;
}
