/*
 * Copyright (c) 2025 Christian Schulte <cs@schulte.it>
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

/*
 * $JDTAUS$
 * Origin: https://github.com/wcjson/wcjson v0.35
 * Modifications: Changed to include wcjson-document.h from current directory.
 */

/**
 * @file wcjson-document.c
 * @brief Document implementation.
 */

#ifdef HAVE_HOST_H
#include "host.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "wcjson-document.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#ifndef DEFAULT_NESTING_LIMIT
#define DEFAULT_NESTING_LIMIT 256
#endif

#define VALUE_IS_VALID(v)                                                      \
  ((v)->is_null || (v)->is_boolean || (v)->is_array || (v)->is_object ||       \
   (((v)->is_string || (v)->is_number || (v)->is_pair) &&                      \
    (v)->string != NULL))

#define VALUE_IS_CHILD(v) ((v)->prev_idx != 0 || (v)->next_idx != 0)

static void *doc_object_start(struct wcjson *, void *, void *);
static void doc_object_add(struct wcjson *, void *, void *, void *, void *);
static void doc_object_end(struct wcjson *, void *, void *);
static void *doc_array_start(struct wcjson *, void *, void *);
static void doc_array_add(struct wcjson *, void *, void *, void *);
static void doc_array_end(struct wcjson *, void *, void *);
static void *doc_string_value(struct wcjson *, void *, const wchar_t *,
                              const size_t, const bool);
static void *doc_number_value(struct wcjson *, void *, const wchar_t *,
                              const size_t);
static void *doc_bool_value(struct wcjson *, void *, const bool);
static void *doc_null_value(struct wcjson *, void *);

static struct wcjson_ops doc_ops = {
    .object_start = doc_object_start,
    .object_add = doc_object_add,
    .object_end = doc_object_end,
    .array_start = doc_array_start,
    .array_add = doc_array_add,
    .array_end = doc_array_end,
    .string_value = doc_string_value,
    .number_value = doc_number_value,
    .bool_value = doc_bool_value,
    .null_value = doc_null_value,
};

struct doc_impl {
  struct wcjson_document *d;
  size_t nlimit;
};

static struct wcjson_value *wcjson_document_nextv(struct wcjson_document *doc) {
  doc->v_nitems_cnt++;

  if (doc->values == NULL)
    return NULL;

  if (doc->v_next == doc->v_nitems)
    goto err_range;

  struct wcjson_value *v = &doc->values[doc->v_next];
  v->is_null = 0;
  v->is_boolean = 0;
  v->is_true = 0;
  v->is_string = 0;
  v->is_number = 0;
  v->is_object = 0;
  v->is_array = 0;
  v->is_pair = 0;
  v->string = NULL;
  v->s_len = 0;
  v->mbstring = NULL;
  v->mb_len = 0;
  v->idx = doc->v_next++;
  v->head_idx = 0;
  v->tail_idx = 0;
  v->prev_idx = 0;
  v->next_idx = 0;
  return v;
err_range:
  errno = ERANGE;
  return NULL;
}

struct wcjson_value *wcjson_value_null(struct wcjson_document *doc) {
  if (doc->values == NULL)
    goto err_inval;

  struct wcjson_value *v = wcjson_document_nextv(doc);

  if (v != NULL)
    v->is_null = 1;

  return v;
err_inval:
  errno = EINVAL;
  return NULL;
}

struct wcjson_value *wcjson_value_bool(struct wcjson_document *doc,
                                       const bool val) {
  if (doc->values == NULL)
    goto err_inval;

  struct wcjson_value *v = wcjson_document_nextv(doc);

  if (v != NULL) {
    v->is_boolean = 1;
    v->is_true = val ? 1 : 0;
  }

  return v;
err_inval:
  errno = EINVAL;
  return NULL;
}

struct wcjson_value *wcjson_value_string(struct wcjson_document *doc,
                                         const wchar_t *val, const size_t len) {
  if (doc->values == NULL)
    goto err_inval;

  struct wcjson_value *v = wcjson_document_nextv(doc);

  if (v != NULL) {
    v->is_string = 1;
    v->string = val;
    v->s_len = len;
  }

  return v;
err_inval:
  errno = EINVAL;
  return NULL;
}

struct wcjson_value *wcjson_value_number(struct wcjson_document *doc,
                                         const wchar_t *val, const size_t len) {
  if (doc->values == NULL)
    goto err_inval;

  struct wcjson_value *v = wcjson_document_nextv(doc);

  if (v != NULL) {
    v->is_number = 1;
    v->string = val;
    v->s_len = len;
  }

  return v;
err_inval:
  errno = EINVAL;
  return NULL;
}

struct wcjson_value *wcjson_value_object(struct wcjson_document *doc) {
  if (doc->values == NULL)
    goto err_inval;

  struct wcjson_value *v = wcjson_document_nextv(doc);

  if (v != NULL)
    v->is_object = 1;

  return v;
err_inval:
  errno = EINVAL;
  return NULL;
}

static struct wcjson_value *wcjson_value_pair(struct wcjson_document *doc,
                                              const wchar_t *key,
                                              const size_t key_len,
                                              const struct wcjson_value *val) {
  if (doc->values == NULL)
    goto err_inval;

  struct wcjson_value *v = wcjson_document_nextv(doc);

  if (v != NULL) {
    v->is_pair = 1;
    v->string = key;
    v->s_len = key_len;
    v->head_idx = val->idx;
    v->tail_idx = val->idx;
  }

  return v;
err_inval:
  errno = EINVAL;
  return NULL;
}

struct wcjson_value *wcjson_value_array(struct wcjson_document *doc) {
  if (doc->values == NULL)
    goto err_inval;

  struct wcjson_value *v = wcjson_document_nextv(doc);

  if (v != NULL)
    v->is_array = 1;

  return v;
err_inval:
  errno = EINVAL;
  return NULL;
}

int wcjson_array_add_head(const struct wcjson_document *doc,
                          struct wcjson_value *arr, struct wcjson_value *val) {
  if (!(arr->is_array && VALUE_IS_VALID(val)) || VALUE_IS_CHILD(val))
    goto err_inval;

  if (arr->head_idx == 0) {
    arr->head_idx = val->idx;
    arr->tail_idx = val->idx;
  } else {
    doc->values[arr->head_idx].prev_idx = val->idx;
    val->next_idx = arr->head_idx;
    arr->head_idx = val->idx;
  }

  return 0;
err_inval:
  errno = EINVAL;
  return -1;
}

int wcjson_array_add_tail(const struct wcjson_document *doc,
                          struct wcjson_value *arr, struct wcjson_value *val) {
  if (!(arr->is_array && VALUE_IS_VALID(val)) || VALUE_IS_CHILD(val))
    goto err_inval;

  if (arr->head_idx == 0) {
    arr->head_idx = val->idx;
    arr->tail_idx = val->idx;
  } else {
    doc->values[arr->tail_idx].next_idx = val->idx;
    val->prev_idx = arr->tail_idx;
    arr->tail_idx = val->idx;
  }

  return 0;
err_inval:
  errno = EINVAL;
  return -1;
}

struct wcjson_value *wcjson_array_get(const struct wcjson_document *doc,
                                      const struct wcjson_value *arr,
                                      const size_t idx) {
  if (!arr->is_array)
    goto err_inval;

  struct wcjson_value *v;
  size_t i = 0;

  wcjson_value_foreach(v, doc, arr) {
    if (i++ == idx)
      return v;
  }

  return NULL;
err_inval:
  errno = EINVAL;
  return NULL;
}

struct wcjson_value *wcjson_array_remove(const struct wcjson_document *doc,
                                         struct wcjson_value *arr,
                                         const size_t idx) {
  struct wcjson_value *val = wcjson_array_get(doc, arr, idx);

  if (val != NULL) {
    if (val->next_idx != 0)
      doc->values[val->next_idx].prev_idx = val->prev_idx;

    if (val->prev_idx != 0)
      doc->values[val->prev_idx].next_idx = val->next_idx;

    if (arr->head_idx == val->idx)
      arr->head_idx = val->next_idx;

    if (arr->tail_idx == val->idx)
      arr->tail_idx = val->prev_idx;
  }

  return val;
}

int wcjson_object_add_head(struct wcjson_document *doc,
                           struct wcjson_value *obj, const wchar_t *key,
                           const size_t key_len,
                           const struct wcjson_value *val) {
  if (!(obj->is_object && VALUE_IS_VALID(val)) || VALUE_IS_CHILD(val) ||
      doc->values == NULL)
    goto err_inval;

  struct wcjson_value *pair = wcjson_value_pair(doc, key, key_len, val);
  if (pair == NULL)
    goto err;

  if (obj->head_idx == 0) {
    obj->head_idx = pair->idx;
    obj->tail_idx = pair->idx;
  } else {
    doc->values[obj->head_idx].prev_idx = pair->idx;
    pair->next_idx = obj->head_idx;
    obj->head_idx = pair->idx;
  }

  return 0;
err:
  return -1;
err_inval:
  errno = EINVAL;
  return -1;
}

int wcjson_object_add_tail(struct wcjson_document *doc,
                           struct wcjson_value *obj, const wchar_t *key,
                           const size_t key_len,
                           const struct wcjson_value *val) {
  if (!(obj->is_object && VALUE_IS_VALID(val)) || VALUE_IS_CHILD(val) ||
      doc->values == NULL)
    goto err_inval;

  struct wcjson_value *pair = wcjson_value_pair(doc, key, key_len, val);
  if (pair == NULL)
    goto err;

  if (obj->head_idx == 0) {
    obj->head_idx = pair->idx;
    obj->tail_idx = pair->idx;
  } else {
    doc->values[obj->tail_idx].next_idx = pair->idx;
    pair->prev_idx = obj->tail_idx;
    obj->tail_idx = pair->idx;
  }

  return 0;
err:
  return -1;
err_inval:
  errno = EINVAL;
  return -1;
}

struct wcjson_value *wcjson_object_remove(const struct wcjson_document *doc,
                                          struct wcjson_value *obj,
                                          const wchar_t *key,
                                          const size_t key_len) {
  if (!obj->is_object)
    goto err_inval;

  struct wcjson_value *v;

  wcjson_value_foreach(v, doc, obj) {
    if (v->s_len == key_len && wcsncmp(v->string, key, v->s_len) == 0) {
      if (v->next_idx != 0)
        doc->values[v->next_idx].prev_idx = v->prev_idx;

      if (v->prev_idx != 0)
        doc->values[v->prev_idx].next_idx = v->next_idx;

      if (obj->head_idx == v->idx)
        obj->head_idx = v->next_idx;

      if (obj->tail_idx == v->idx)
        obj->tail_idx = v->prev_idx;

      return wcjson_value_head(doc, v);
    }
  }

  return NULL;
err_inval:
  errno = EINVAL;
  return NULL;
}

struct wcjson_value *wcjson_object_get(const struct wcjson_document *doc,
                                       const struct wcjson_value *obj,
                                       const wchar_t *key,
                                       const size_t key_len) {
  struct wcjson_value *v;

  if (!obj->is_object)
    goto err_inval;

  wcjson_value_foreach(v, doc, obj) {
    if (v->s_len == key_len && wcsncmp(v->string, key, v->s_len) == 0)
      return wcjson_value_head(doc, v);
  }

  return NULL;
err_inval:
  errno = EINVAL;
  return NULL;
}

wchar_t *wcjson_document_string(struct wcjson_document *doc, const wchar_t *s,
                                const size_t len) {
  if (doc->strings == NULL)
    goto err_inval;

  size_t dst_len = doc->s_nitems - doc->s_next;
  wchar_t *dst = &doc->strings[doc->s_next];

  if (dst_len < len + 1)
    goto err_range;

  wchar_t *ret = wcsncpy(dst, s, len);
  ret[len] = L'\0';

  size_t s_next = doc->s_next + len + 1;
  if (s_next < doc->s_next || s_next > doc->s_nitems)
    goto err_range;

  doc->s_next = s_next;
  return ret;
err_inval:
  errno = EINVAL;
  return NULL;
err_range:
  errno = ERANGE;
  return NULL;
}

char *wcjson_document_mbstring(struct wcjson_document *doc, const char *s,
                               const size_t len) {
  if (doc->mbstrings == NULL)
    goto err_inval;

  size_t dst_len = doc->mb_nitems - doc->mb_next;
  char *dst = &doc->mbstrings[doc->mb_next];

  if (dst_len < len + 1)
    goto err_range;

  char *ret = strncpy(dst, s, len);
  ret[len] = '\0';

  size_t mb_next = doc->mb_next + len + 1;
  if (mb_next < doc->mb_next || mb_next > doc->mb_nitems)
    goto err_range;

  doc->mb_next = mb_next;
  return ret;
err_inval:
  errno = EINVAL;
  return NULL;
err_range:
  errno = ERANGE;
  return NULL;
}

static void *doc_object_start(struct wcjson *ctx, void *doc, void *parent) {
  struct doc_impl *d = doc;
  struct wcjson_value *v = wcjson_document_nextv(d->d);

  if (v != NULL)
    v->is_object = 1;

  if (d->nlimit-- == 0)
    goto err_range;

  return v;
err_range:
  ctx->status = WCJSON_ABORT_ERROR;
  ctx->errnum = ERANGE;
  return v;
}

static void doc_object_add(struct wcjson *ctx, void *doc, void *obj, void *key,
                           void *value) {
  struct doc_impl *d = doc;

  if (obj != NULL && key != NULL && value != NULL) {
    struct wcjson_value *o = obj;
    struct wcjson_value *pair = key;
    struct wcjson_value *v = value;

    pair->is_string = 0;
    pair->is_pair = 1;
    pair->head_idx = v->idx;
    pair->tail_idx = v->idx;

    if (o->head_idx == 0) {
      o->head_idx = pair->idx;
      o->tail_idx = pair->idx;
    } else {
      d->d->values[o->tail_idx].next_idx = pair->idx;
      pair->prev_idx = o->tail_idx;
      o->tail_idx = pair->idx;
    }
  }
}

static void doc_object_end(struct wcjson *ctx, void *doc, void *obj) {
  struct doc_impl *d = doc;
  d->nlimit++;
}

static void *doc_array_start(struct wcjson *ctx, void *doc, void *parent) {
  struct doc_impl *d = doc;
  struct wcjson_value *v = wcjson_document_nextv(d->d);

  if (v != NULL)
    v->is_array = 1;

  if (d->nlimit-- == 0)
    goto err_range;

  return v;
err_range:
  ctx->status = WCJSON_ABORT_ERROR;
  ctx->errnum = ERANGE;
  return v;
}

static void doc_array_add(struct wcjson *ctx, void *doc, void *arr,
                          void *value) {
  struct doc_impl *d = doc;

  if (arr != NULL && value != NULL) {
    struct wcjson_value *a = arr;
    struct wcjson_value *v = value;

    if (a->head_idx == 0) {
      a->head_idx = v->idx;
      a->tail_idx = v->idx;
    } else {
      d->d->values[a->tail_idx].next_idx = v->idx;
      v->prev_idx = a->tail_idx;
      a->tail_idx = v->idx;
    }
  }
}

static void doc_array_end(struct wcjson *ctx, void *doc, void *arr) {
  struct doc_impl *d = doc;
  d->nlimit++;
}

static void *doc_string_value(struct wcjson *ctx, void *doc, const wchar_t *str,
                              const size_t len, const bool escaped) {
  struct doc_impl *d = doc;
  struct wcjson_value *v = wcjson_document_nextv(d->d);

  if (v != NULL) {
    v->is_string = 1;
    v->string = str;
    v->s_len = len;
  }

  d->d->s_nitems_cnt += len + 1;
  return v;
}

static void *doc_number_value(struct wcjson *ctx, void *doc, const wchar_t *num,
                              const size_t len) {
  struct doc_impl *d = doc;
  struct wcjson_value *v = wcjson_document_nextv(d->d);

  if (v != NULL) {
    v->is_number = 1;
    v->string = num;
    v->s_len = len;
  }

  d->d->s_nitems_cnt += len + 1;
  return v;
}

static void *doc_bool_value(struct wcjson *ctx, void *doc, const bool value) {
  struct doc_impl *d = doc;
  struct wcjson_value *v = wcjson_document_nextv(d->d);

  if (v != NULL) {
    v->is_boolean = 1;
    v->is_true = value ? 1 : 0;
  }

  return v;
}

static void *doc_null_value(struct wcjson *ctx, void *doc) {
  struct doc_impl *d = doc;
  struct wcjson_value *v = wcjson_document_nextv(d->d);

  if (v != NULL)
    v->is_null = 1;

  return v;
}

static int doc_unesc(struct wcjson *ctx, struct doc_impl *d,
                     struct wcjson_value *v) {
  if (v->is_string || v->is_pair) {
    size_t dst_len = d->d->s_nitems - d->d->s_next;
    wchar_t *dst = &d->d->strings[d->d->s_next];

    if (wcjsonstowc(v->string, v->s_len, dst, &dst_len) < 0)
      goto err_decode;

    dst[dst_len] = L'\0';

    size_t s_next = d->d->s_next + dst_len + 1;
    if (s_next < d->d->s_next || s_next > d->d->s_nitems)
      goto err_range;

    v->string = dst;
    v->s_len = dst_len;

    d->d->s_next = s_next;
    d->d->e_nitems_cnt =
        MAX(dst_len * WCJSON_ESCAPE_MAX + 1, d->d->e_nitems_cnt);

    size_t mblen = wcstombs(NULL, v->string, v->s_len);
    if (mblen == (size_t)-1)
      goto err;

    d->d->mb_nitems_cnt += mblen + 1;

    if (v->is_pair && doc_unesc(ctx, d, wcjson_value_head(d->d, v)) < 0)
      goto err;

  } else if (v->is_number) {
    size_t dst_len = d->d->s_nitems - d->d->s_next;
    wchar_t *dst = &d->d->strings[d->d->s_next];

    if (dst_len < v->s_len + 1)
      goto err_range;

    wmemcpy(dst, v->string, v->s_len);
    dst[v->s_len] = L'\0';

    size_t s_next = d->d->s_next + v->s_len + 1;
    if (s_next < d->d->s_next || s_next > d->d->s_nitems)
      goto err_range;

    v->string = dst;
    d->d->s_next = s_next;

    size_t mblen = wcstombs(NULL, v->string, v->s_len);
    if (mblen == (size_t)-1)
      goto err;

    d->d->mb_nitems_cnt += mblen + 1;
  } else if (v->is_array) {
    for (struct wcjson_value *n = wcjson_value_tail(d->d, v); n != NULL;
         n = wcjson_value_prev(d->d, n))
      if (doc_unesc(ctx, d, n) < 0)
        goto err;
  } else if (v->is_object) {
    for (struct wcjson_value *n = wcjson_value_tail(d->d, v); n != NULL;
         n = wcjson_value_prev(d->d, n))
      if (doc_unesc(ctx, d, n) < 0)
        goto err;
  }

  return 0;
err:
  return -1;
err_range:
  ctx->status = WCJSON_ABORT_ERROR;
  ctx->errnum = ERANGE;
  return -1;
err_decode:
  ctx->status = WCJSON_ABORT_ERROR;
  ctx->errnum = errno;
  return -1;
}

static int doc_mbstrings(struct wcjson *ctx, struct doc_impl *d,
                         struct wcjson_value *v) {
  if (v->is_string || v->is_pair) {
    size_t dst_len = d->d->mb_nitems - d->d->mb_next;
    char *dst = &d->d->mbstrings[d->d->mb_next];
    size_t mb_len = wcstombs(dst, v->string, dst_len);

    if (mb_len == (size_t)-1)
      goto err;

    if (mb_len == dst_len)
      goto err_range;

    size_t mb_next = d->d->mb_next + mb_len + 1;

    if (mb_next < d->d->mb_next || mb_next > d->d->mb_nitems)
      goto err_range;

    v->mbstring = dst;
    v->mb_len = mb_len;

    d->d->mb_next = mb_next;

    if (v->is_pair && doc_mbstrings(ctx, d, wcjson_value_head(d->d, v)) < 0)
      goto err;

  } else if (v->is_number) {
    size_t dst_len = d->d->mb_nitems - d->d->mb_next;
    char *dst = &d->d->mbstrings[d->d->mb_next];
    size_t mb_len = wcstombs(dst, v->string, dst_len);

    if (mb_len == (size_t)-1)
      goto err;

    if (mb_len == dst_len)
      goto err_range;

    size_t mb_next = d->d->mb_next + mb_len + 1;

    if (mb_next < d->d->mb_next || mb_next > d->d->mb_nitems)
      goto err_range;

    v->mbstring = dst;
    v->mb_len = mb_len;

    d->d->mb_next = mb_next;
  } else if (v->is_array) {
    for (struct wcjson_value *n = wcjson_value_tail(d->d, v); n != NULL;
         n = wcjson_value_prev(d->d, n))
      if (doc_mbstrings(ctx, d, n) < 0)
        goto err;
  } else if (v->is_object) {
    for (struct wcjson_value *n = wcjson_value_tail(d->d, v); n != NULL;
         n = wcjson_value_prev(d->d, n))
      if (doc_mbstrings(ctx, d, n) < 0)
        goto err;
  }

  return 0;
err:
  return -1;
err_range:
  ctx->status = WCJSON_ABORT_ERROR;
  ctx->errnum = ERANGE;
  return -1;
}

static int doc_fprint(FILE *f, bool asc, const struct wcjson_document *d,
                      const struct wcjson_value *v) {
  if (v->is_null) {
    if (fputws(L"null", f) == -1)
      goto err;

  } else if (v->is_boolean) {
    if (fputws(v->is_true ? L"true" : L"false", f) == -1)
      goto err;

  } else if (v->is_string || v->is_pair) {
    size_t d_len = d->e_nitems;

    if (asc) {
      if (wctoascjsons(v->string, v->s_len, d->esc, &d_len) < 0)
        goto err;

    } else {
      if (wctowcjsons(v->string, v->s_len, d->esc, &d_len) < 0)
        goto err;
    }

    if (d->e_nitems - d_len == 0)
      goto err_range;

    d->esc[d_len] = L'\0';

    if (putwc(L'"', f) == WEOF)
      goto err;

    if (fputws(d->esc, f) == -1)
      goto err;

    if (putwc(L'"', f) == WEOF)
      goto err;

    if (v->is_pair) {
      if (putwc(L':', f) == WEOF)
        goto err;

      if (doc_fprint(f, asc, d, wcjson_value_head(d, v)) < 0)
        goto err;
    }
  } else if (v->is_number) {
    if (fputws(v->string, f) == -1)
      goto err;

  } else if (v->is_array) {
    if (putwc(L'[', f) == WEOF)
      goto err;

    struct wcjson_value *n = wcjson_value_head(d, v);
    while (n != NULL) {
      if (doc_fprint(f, asc, d, n) < 0)
        goto err;

      n = wcjson_value_next(d, n);

      if (n != NULL && putwc(L',', f) == WEOF)
        goto err;
    }

    if (putwc(L']', f) == WEOF)
      goto err;

  } else if (v->is_object) {
    if (putwc(L'{', f) == WEOF)
      goto err;

    struct wcjson_value *n = wcjson_value_head(d, v);
    while (n != NULL) {
      if (doc_fprint(f, asc, d, n) < 0)
        goto err;

      n = wcjson_value_next(d, n);

      if (n != NULL && putwc(L',', f) == WEOF)
        goto err;
    }

    if (putwc(L'}', f) == WEOF)
      goto err;

  } else
    goto err_inval;

  return 0;
err:
  return -1;
err_inval:
  errno = EINVAL;
  return -1;
err_range:
  errno = ERANGE;
  return -1;
}

static int doc_sprint(wchar_t *s, size_t *lenp, bool asc,
                      const struct wcjson_document *d,
                      const struct wcjson_value *v) {
#ifdef __copy
#error "__copy macro already defined - rename"
#else
#define __copy(_s, _l)                                                         \
  do {                                                                         \
    if (s_len < (_l))                                                          \
      goto err_range;                                                          \
    wmemcpy(s, (_s), (_l));                                                    \
    s_len -= (_l);                                                             \
    s += (_l);                                                                 \
  } while (0)
#endif

  size_t s_len = *lenp;

  if (v->is_null) {
    __copy(L"null", 4);
  } else if (v->is_boolean) {
    if (v->is_true) {
      __copy(L"true", 4);
    } else {
      __copy(L"false", 5);
    }
  } else if (v->is_string || v->is_pair) {
    size_t d_len = d->e_nitems;

    if (asc) {
      if (wctoascjsons(v->string, v->s_len, d->esc, &d_len) < 0)
        goto err;

    } else {
      if (wctowcjsons(v->string, v->s_len, d->esc, &d_len) < 0)
        goto err;
    }

    if (d->e_nitems - d_len == 0)
      goto err_range;

    d->esc[d_len] = L'\0';

    __copy(L"\"", 1);
    __copy(d->esc, d_len);
    __copy(L"\"", 1);

    if (v->is_pair) {
      __copy(L":", 1);

      size_t t_len = s_len;
      if (doc_sprint(s, &t_len, asc, d, wcjson_value_head(d, v)) < 0)
        goto err;

      s_len -= t_len;
    }
  } else if (v->is_number) {
    __copy(v->string, v->s_len);
  } else if (v->is_array) {
    __copy(L"[", 1);

    struct wcjson_value *n = wcjson_value_head(d, v);
    while (n != NULL) {
      size_t t_len = s_len;
      if (doc_sprint(s, &t_len, asc, d, n) < 0)
        goto err;

      s_len -= t_len;
      s += t_len;

      n = wcjson_value_next(d, n);

      if (n != NULL)
        __copy(L",", 1);
    }

    __copy(L"]", 1);
  } else if (v->is_object) {
    __copy(L"{", 1);

    struct wcjson_value *n = wcjson_value_head(d, v);
    while (n != NULL) {
      size_t t_len = s_len;
      if (doc_sprint(s, &t_len, asc, d, n) < 0)
        goto err;

      s_len -= t_len;
      s += t_len;

      n = wcjson_value_next(d, n);

      if (n != NULL)
        __copy(L",", 1);
    }

    __copy(L"}", 1);
  } else
    goto err_inval;

  *lenp -= s_len;
  return 0;
err:
  return -1;
err_inval:
  errno = EINVAL;
  return -1;
err_range:
  errno = ERANGE;
  return -1;
#undef __copy
}

int wcjsondocvalues(struct wcjson *ctx, struct wcjson_document *doc,
                    const wchar_t *txt, const size_t len) {
  int saved_errno = errno;
  errno = 0;

  struct doc_impl d = {
      .d = doc,
      .nlimit = DEFAULT_NESTING_LIMIT,
  };

  d.d->v_nitems_cnt = 0;
  d.d->s_nitems_cnt = 0;

  int r = wcjson(ctx, &doc_ops, &d, txt, len);

  if (r == 0 && errno != 0) {
    ctx->status = WCJSON_ABORT_ERROR;
    ctx->errnum = errno;
    r = -1;
  }

  errno = saved_errno;
  return r;
}

int wcjsondocstrings(struct wcjson *ctx, struct wcjson_document *doc) {
  struct doc_impl d = {
      .d = doc,
      .nlimit = DEFAULT_NESTING_LIMIT,
  };

  d.d->mb_nitems_cnt = 0;
  d.d->e_nitems_cnt = 0;

  return doc_unesc(ctx, &d, d.d->values);
}

int wcjsondocmbstrings(struct wcjson *ctx, struct wcjson_document *doc) {
  struct doc_impl d = {
      .d = doc,
      .nlimit = DEFAULT_NESTING_LIMIT,
  };
  return doc_mbstrings(ctx, &d, d.d->values);
}

int wcjsondocfprint(FILE *f, const struct wcjson_document *doc,
                    const struct wcjson_value *value) {
  return doc_fprint(f, false, doc, value);
}

int wcjsondocfprintasc(FILE *f, const struct wcjson_document *doc,
                       const struct wcjson_value *value) {
  return doc_fprint(f, true, doc, value);
}

int wcjsondocsprint(wchar_t *s, size_t *lenp, const struct wcjson_document *doc,
                    const struct wcjson_value *value) {
  size_t s_len = *lenp;
  size_t t_len = s_len;
  int r = doc_sprint(s, &t_len, false, doc, value);

  if (r < 0)
    return -1;

  s_len -= t_len;

  if (s_len < 1)
    goto err_range;

  s[t_len] = L'\0';
  *lenp -= s_len;
  return 0;
err_range:
  errno = ERANGE;
  return -1;
}

int wcjsondocsprintasc(wchar_t *s, size_t *lenp,
                       const struct wcjson_document *doc,
                       const struct wcjson_value *value) {
  size_t s_len = *lenp;
  size_t t_len = s_len;
  int r = doc_sprint(s, &t_len, true, doc, value);

  if (r < 0)
    return -1;

  s_len -= t_len;

  if (s_len < 1)
    goto err_range;

  s[t_len] = L'\0';
  *lenp -= s_len;
  return 0;
err_range:
  errno = ERANGE;
  return -1;
}

#ifdef __cplusplus
}
#endif
