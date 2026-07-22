/* $JDTAUS: json.c 9634 2026-07-22 02:43:37Z schulte $ */

/*
 * Copyright (c) 2026 Christian Schulte <cs@schulte.it>
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

#include "array.h"
#include "charset.h"
#include "heap.h"
#include "json.h"
#include "proc.h"
#include "thread.h"
#include "time.h"

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#define JSON_ERR_MAX (size_t)8192

#define nitems(a) (sizeof((a)) / sizeof((a)[0]))

struct json_tls {
  struct wcjson_document_mbstowcs_vars {
    struct wc_heap_obj *restrict wc;
  } wcjson_document_mbstowcs;
  struct json_mbparse_vars {
    struct wc_heap_obj *restrict wc;
  } json_mbparse;
  struct json_mbsprint_vars {
    struct wc_heap_obj *restrict wc;
  } json_mbsprint;
};

struct json_heap_document {
  struct Array *restrict heap_values;
  struct wcjson_document *wc_doc;
};

struct json_heap_value {
  size_t idx;
};

struct wc_heap_obj {
  wchar_t *restrict base;
  size_t len;
};

static tss_t json_tls_key;

static void *json_heap_op_object_start(struct wcjson *, void *, void *);
static void json_heap_op_object_add(struct wcjson *, void *, void *, void *,
                                    void *);
static void json_heap_op_object_end(struct wcjson *, void *, void *);
static void *json_heap_op_array_start(struct wcjson *, void *, void *);
static void json_heap_op_array_add(struct wcjson *, void *, void *, void *);
static void json_heap_op_array_end(struct wcjson *, void *, void *);
static void *json_heap_op_string_value(struct wcjson *, void *, const wchar_t *,
                                       const size_t, const bool);
static void *json_heap_op_number_value(struct wcjson *, void *, const wchar_t *,
                                       const size_t);
static void *json_heap_op_bool_value(struct wcjson *, void *, const bool);
static void *json_heap_op_null_value(struct wcjson *, void *);

const struct wcjson_ops *const json_heap_ops = &(const struct wcjson_ops){
    .object_start = json_heap_op_object_start,
    .object_add = json_heap_op_object_add,
    .object_end = json_heap_op_object_end,
    .array_start = json_heap_op_array_start,
    .array_add = json_heap_op_array_add,
    .array_end = json_heap_op_array_end,
    .string_value = json_heap_op_string_value,
    .number_value = json_heap_op_number_value,
    .bool_value = json_heap_op_bool_value,
    .null_value = json_heap_op_null_value,
};

static struct json_tls *const json_tls(void) {
  struct json_tls *restrict tls = tls_get(json_tls_key);
  if (tls == NULL) {
    tls = heap_malloc(sizeof(struct json_tls));
    tls->wcjson_document_mbstowcs.wc =
        heap_calloc(1, sizeof(struct wc_heap_obj));
    tls->json_mbparse.wc = heap_calloc(1, sizeof(struct wc_heap_obj));
    tls->json_mbsprint.wc = heap_calloc(1, sizeof(struct wc_heap_obj));
    tls_set(json_tls_key, tls);
  }

  return tls;
}

static void json_tls_dtor(void *e) {
  struct json_tls *restrict const tls = e;
  heap_free(tls->wcjson_document_mbstowcs.wc->base);
  heap_free(tls->wcjson_document_mbstowcs.wc);
  heap_free(tls->json_mbparse.wc->base);
  heap_free(tls->json_mbparse.wc);
  heap_free(tls->json_mbsprint.wc->base);
  heap_free(tls->json_mbsprint.wc);
  heap_free(tls);
  tls_set(json_tls_key, NULL);
}

void json_init(void) { tls_create(&json_tls_key, json_tls_dtor); }
void json_destroy(void) { tls_delete(json_tls_key); }

static void
wcjson_document_grow_strings(struct wcjson_document *restrict const doc) {
  for (size_t i = 0; i < doc->v_next; i++) {
    struct wcjson_value *restrict wc_value = &doc->values[i];

    if ((wc_value->is_string || wc_value->is_pair || wc_value->is_number) &&
        wc_value->string >= doc->strings &&
        wc_value->string < doc->strings + doc->s_next) {
      // XXX UB
      wc_value->string =
          (const wchar_t *)(size_t)(uintptr_t)(wc_value->string - doc->strings);

      wc_value->is_true = 1;
    }
  }

  doc->strings = heap_realloc(doc->strings, doc->s_nitems * sizeof(wchar_t));

  for (size_t i = 0; i < doc->v_next; i++) {
    struct wcjson_value *restrict wc_value = &doc->values[i];

    if ((wc_value->is_string || wc_value->is_pair || wc_value->is_number) &&
        wc_value->is_true) {
      wc_value->string = &doc->strings[(size_t)(uintptr_t)wc_value->string];
      wc_value->is_true = 0;
    }
  }
}

static void
wcjson_document_grow_mbstrings(struct wcjson_document *restrict const doc) {
  for (size_t i = 0; i < doc->v_next; i++) {
    struct wcjson_value *restrict wc_value = &doc->values[i];

    if ((wc_value->is_string || wc_value->is_pair || wc_value->is_number) &&
        wc_value->mbstring >= doc->mbstrings &&
        wc_value->mbstring < doc->mbstrings + doc->mb_next) {
      // XXX UB
      wc_value->mbstring =
          (const char *)(size_t)(uintptr_t)(wc_value->mbstring -
                                            doc->mbstrings);

      wc_value->is_true = 1;
    }
  }

  doc->mbstrings = heap_realloc(doc->mbstrings, doc->mb_nitems * sizeof(char));

  for (size_t i = 0; i < doc->v_next; i++) {
    struct wcjson_value *restrict wc_value = &doc->values[i];

    if ((wc_value->is_string || wc_value->is_pair || wc_value->is_number) &&
        wc_value->is_true) {
      wc_value->mbstring =
          &doc->mbstrings[(size_t)(uintptr_t)wc_value->mbstring];

      wc_value->is_true = 0;
    }
  }
}

static int
wcjson_document_mbstowcs(struct wcjson_document *restrict const wc_doc,
                         wchar_t *restrict *restrict wcp, size_t *wc_lenp,
                         const char *restrict const s, const size_t s_len) {
  const struct json_tls *restrict const tls = json_tls();
  struct wc_heap_obj *restrict const wc = tls->wcjson_document_mbstowcs.wc;
  size_t wc_len = s_len + 1;

  if (wc_len > wc->len) {
    wc->base = heap_realloc(wc->base, wc_len * sizeof(wchar_t));
    wc->len = wc_len;
  }

  wc_len = mbstowcs(wc->base, s, wc_len);

  if (wc_len == (size_t)-1)
    goto err;

  if (wc_len >= s_len + 1) {
    errno = ERANGE;
    goto err;
  }

  size_t s_remaining = wc_doc->s_nitems - wc_doc->s_next;

  if (wc_len == SIZE_MAX)
    panic();

  if (wc_len + 1 > s_remaining) {
    const size_t s_nitems = wc_doc->s_nitems + wc_len - s_remaining + 1;

    if (s_nitems < wc_doc->s_nitems)
      panic();

    wc_doc->s_nitems = s_nitems;
    wcjson_document_grow_strings(wc_doc);
  }

  *wcp = wcjson_document_string(wc_doc, wc->base, wc_len);

  if (*wcp == NULL)
    goto err;

  *wc_lenp = wc_len;
  return 0;
err:
  return -1;
}

struct wcjson_value *
wcjson_value_mbstring(struct wcjson_document *restrict const wc_doc,
                      const char *restrict const s, const size_t s_len) {
  size_t wc_len;
  wchar_t *restrict wc_str;

  if (wcjson_document_mbstowcs(wc_doc, &wc_str, &wc_len, s, s_len) < 0)
    return NULL;

  return wcjson_value_string(wc_doc, wc_str, wc_len);
}

struct wcjson_value *
wcjson_value_mbnumber(struct wcjson_document *restrict const wc_doc,
                      const char *restrict const s, const size_t s_len) {
  size_t wc_len;
  wchar_t *restrict wc_str;

  if (wcjson_document_mbstowcs(wc_doc, &wc_str, &wc_len, s, s_len) < 0)
    return NULL;

  return wcjson_value_number(wc_doc, wc_str, wc_len);
}

int wcjson_document_build(struct wcjson *restrict wc_json,
                          struct wcjson_document *restrict wc_doc) {
  const size_t s_remaining = wc_doc->s_nitems - wc_doc->s_next;

  if (wc_doc->s_nitems_cnt > s_remaining) {
    const size_t s_nitems =
        wc_doc->s_nitems + wc_doc->s_nitems_cnt - s_remaining;

    if (s_nitems < wc_doc->s_nitems)
      panic();

    wc_doc->s_nitems = s_nitems;
    wcjson_document_grow_strings(wc_doc);
  }

  if (wcjsondocstrings(wc_json, wc_doc) < 0)
    return -1;

  const size_t mb_remaining = wc_doc->mb_nitems - wc_doc->mb_next;

  if (wc_doc->mb_nitems_cnt > mb_remaining) {
    const size_t mb_nitems =
        wc_doc->mb_nitems + wc_doc->mb_nitems_cnt - mb_remaining;

    if (mb_nitems < wc_doc->mb_nitems)
      panic();

    wc_doc->mb_nitems = mb_nitems;
    wcjson_document_grow_mbstrings(wc_doc);
  }

  if (wc_doc->e_nitems_cnt > wc_doc->e_nitems) {
    wc_doc->e_nitems = wc_doc->e_nitems_cnt;
    wc_doc->esc = heap_realloc(wc_doc->esc, wc_doc->e_nitems * sizeof(wchar_t));
  }

  if (wcjsondocmbstrings(wc_json, wc_doc) < 0)
    return -1;

  return 0;
}

const char *json_mbserror(const struct wcjson *restrict const wc_json) {
  switch (wc_json->status) {
  case WCJSON_OK:
    return strerror(0);
  case WCJSON_ABORT_ERROR:
    return strerror(wc_json->errnum);
  case WCJSON_ABORT_INVALID:
    return "Invalid JSON text";
  case WCJSON_ABORT_END_OF_INPUT:
    return "Incomplete JSON text";
  default:
    panic();
  }
}

int json_mbparse(struct wcjson_document *restrict wc_doc,
                 const char *restrict const s, const size_t s_len) {
  int r = -1;
  const struct json_tls *restrict const tls = json_tls();
  struct wc_heap_obj *restrict const wc = tls->json_mbparse.wc;
  const int saved_errno = errno;
  size_t wc_len = s_len + 1;
  struct wcjson wc_json = WCJSON_INITIALIZER;

  if (wc_len > wc->len) {
    wc->base = heap_realloc(wc->base, wc_len * sizeof(wchar_t));
    wc->len = wc_len;
  }

  errno = 0;
  if (mbsntowcsn(wc->base, &wc_len, s, s_len) < 0)
    goto err;

  struct json_heap_document *restrict heap_doc =
      heap_malloc(sizeof(struct json_heap_document));

  heap_doc->heap_values = Array_new(64);
  heap_doc->wc_doc = wc_doc;

  wc_doc->s_nitems_cnt = 0;
  wc_doc->mb_nitems_cnt = 0;
  wc_doc->e_nitems_cnt = 0;

  if (wcjson(&wc_json, json_heap_ops, heap_doc, wc->base, wc_len) < 0)
    goto err;

  Array_delete(heap_doc->heap_values, heap_free);
  heap_free(heap_doc);

  const size_t s_remaining = wc_doc->s_nitems - wc_doc->s_next;

  if (wc_doc->s_nitems_cnt > s_remaining) {
    const size_t s_nitems =
        wc_doc->s_nitems + wc_doc->s_nitems_cnt - s_remaining;

    if (s_nitems < wc_doc->s_nitems)
      panic();

    wc_doc->s_nitems = s_nitems;
    wcjson_document_grow_strings(wc_doc);
  }

  if (wcjsondocstrings(&wc_json, wc_doc) < 0)
    goto err;

  const size_t mb_remaining = wc_doc->mb_nitems - wc_doc->mb_next;

  if (wc_doc->mb_nitems_cnt > mb_remaining) {
    const size_t mb_nitems =
        wc_doc->mb_nitems + wc_doc->mb_nitems_cnt - mb_remaining;

    if (mb_nitems < wc_doc->mb_nitems)
      panic();

    wc_doc->mb_nitems = mb_nitems;
    wcjson_document_grow_mbstrings(wc_doc);
  }

  if (wc_doc->e_nitems_cnt > wc_doc->e_nitems) {
    wc_doc->e_nitems = wc_doc->e_nitems_cnt;
    wc_doc->esc = heap_realloc(wc_doc->esc, wc_doc->e_nitems * sizeof(wchar_t));
  }

  if (wcjsondocmbstrings(&wc_json, wc_doc) < 0)
    goto err;

  r = 0;
  errno = 0;
err:
  if (wc_json.status != WCJSON_OK)
    werr("%s: %s: %.*s\n", __func__, json_mbserror(&wc_json), (int)s_len, s);

  if (errno)
    werr("%s: %s: %.*s\n", __func__, strerror(errno), (int)s_len, s);

  errno = saved_errno;
  return r;
}

int json_mbsprint(char *restrict const dst, size_t *restrict const dst_lenp,
                  const struct wcjson_document *restrict const wc_doc,
                  const struct wcjson_value *restrict const wc_val) {
  int r = -1;
  const struct json_tls *restrict const tls = json_tls();
  struct wc_heap_obj *restrict const wc = tls->json_mbsprint.wc;
  const int saved_errno = errno;
  size_t wc_len = *dst_lenp + 1;

  if (wc_len > wc->len) {
    wc->base = heap_realloc(wc->base, wc_len * sizeof(wchar_t));
    wc->len = wc_len;
  }

  errno = 0;
  if (wcjsondocsprint(wc->base, &wc_len, wc_doc, wc_val) < 0)
    goto err;

  size_t mb_len = wcstombs(dst, wc->base, *dst_lenp);

  if (mb_len == (size_t)-1)
    goto err;

  if (mb_len >= *dst_lenp) {
    errno = ERANGE;
    goto err;
  }

  *dst_lenp = mb_len;
  errno = 0;
  r = 0;
err:
  if (errno)
    werr("%s: %s\n", __func__, strerror(errno));

  errno = saved_errno;
  return r;
}

static void json_werr(const struct wcjson_document *restrict const wc_doc,
                      const struct wcjson_value *wc_val,
                      const char *restrict const fmt, ...) {
  int r;
  va_list ap;
  char ser[JSON_ERR_MAX + 1] = {0};
  char err[JSON_ERR_MAX + 1] = {0};
  size_t ser_nitems = nitems(err);

  if (json_mbsprint(ser, &ser_nitems, wc_doc, wc_val)) {
    r = snprintf(ser, ser_nitems, "%s", strerror(errno));
    if (r < 0 || (size_t)r >= ser_nitems)
      panic();
  }

  va_start(ap, fmt);
  r = vsnprintf(err, sizeof(err), fmt, ap);
  va_end(ap);

  if (r < 0 || (size_t)r >= sizeof(err))
    panic();

  werr("json: %s: %s\n", err, ser);
}

struct String *
json_obj_get_string(const struct wcjson_document *restrict const wc_doc,
                    const struct wcjson_value *restrict const wc_obj,
                    const wchar_t *restrict const key, const size_t key_len) {
  struct wcjson_value *restrict const v =
      wcjson_object_get(wc_doc, wc_obj, key, key_len);

  if (v == NULL || !(v->is_string || v->s_len)) {
    json_werr(wc_doc, wc_obj, "No '%ls' string item", key);
    errno = EILSEQ;
    return NULL;
  }

  return String_cnew(v->mbstring);
}

struct String *json_obj_get_optional_string(
    const struct wcjson_document *restrict const wc_doc,
    const struct wcjson_value *restrict const wc_obj,
    const wchar_t *restrict const key, const size_t key_len) {
  struct wcjson_value *restrict const v =
      wcjson_object_get(wc_doc, wc_obj, key, key_len);

  if (v != NULL && !(v->is_string || v->is_null)) {
    json_werr(wc_doc, wc_obj, "No '%ls' string item", key);
    errno = EILSEQ;
    return NULL;
  }

  return v == NULL || v->is_null ? NULL : String_cnew(v->mbstring);
}

struct Numeric *
json_obj_get_string_number(const struct wcjson_document *restrict const wc_doc,
                           const struct wcjson_value *restrict const wc_obj,
                           const wchar_t *restrict const key,
                           const size_t key_len) {
  struct wcjson_value *restrict const v =
      wcjson_object_get(wc_doc, wc_obj, key, key_len);

  if (v == NULL || !(v->is_string || v->s_len)) {
    json_werr(wc_doc, wc_obj, "No '%ls' numeric item", key);
    errno = EILSEQ;
    return NULL;
  }

  struct Numeric *restrict const n = Numeric_from_char(v->mbstring);

  if (n == NULL) {
    json_werr(wc_doc, wc_obj, "Invalid '%ls' numeric item", key);
    errno = EILSEQ;
    return NULL;
  }

  return n;
}

struct Numeric *json_obj_get_optional_string_number(
    const struct wcjson_document *restrict const wc_doc,
    const struct wcjson_value *restrict const wc_obj,
    const wchar_t *restrict const key, const size_t key_len) {
  struct wcjson_value *restrict const v =
      wcjson_object_get(wc_doc, wc_obj, key, key_len);

  if (v != NULL && !(v->is_string || v->is_null)) {
    json_werr(wc_doc, wc_obj, "No '%ls' numeric item", key);
    errno = EILSEQ;
    return NULL;
  }

  if (v == NULL || v->is_null)
    return NULL;

  struct Numeric *restrict const n = Numeric_from_char(v->mbstring);

  if (n == NULL) {
    json_werr(wc_doc, wc_obj, "Invalid '%ls' numeric item", key);
    errno = EILSEQ;
    return NULL;
  }

  return n;
}

struct Numeric *
json_obj_get_string_iso8601(const struct wcjson_document *restrict const wc_doc,
                            const struct wcjson_value *restrict const wc_obj,
                            const wchar_t *restrict const key,
                            const size_t key_len) {
  struct wcjson_value *restrict const v =
      wcjson_object_get(wc_doc, wc_obj, key, key_len);

  if (v == NULL || !(v->is_string || v->s_len)) {
    json_werr(wc_doc, wc_obj, "No '%ls' ISO8601 item", key);
    errno = EILSEQ;
    return NULL;
  }

  struct Numeric *restrict const n = Numeric_new();

  if (!nanos_from_iso8601(v->mbstring, v->mb_len, n)) {
    json_werr(wc_doc, wc_obj, "Invalid '%ls' ISO8601 item", key);
    Numeric_delete(n);
    errno = EILSEQ;
    return NULL;
  }

  return n;
}

struct Numeric *json_obj_get_optional_string_iso8601(
    const struct wcjson_document *restrict const wc_doc,
    const struct wcjson_value *restrict const wc_obj,
    const wchar_t *restrict const key, const size_t key_len) {
  struct wcjson_value *restrict const v =
      wcjson_object_get(wc_doc, wc_obj, key, key_len);

  if (v != NULL && !(v->is_string || v->is_null)) {
    json_werr(wc_doc, wc_obj, "No '%ls' ISO8601 item", key);
    errno = EILSEQ;
    return NULL;
  }

  if (v == NULL || v->is_null)
    return NULL;

  struct Numeric *restrict const n = Numeric_new();

  if (!nanos_from_iso8601(v->mbstring, v->mb_len, n)) {
    json_werr(wc_doc, wc_obj, "Invalid '%ls' ISO8601 item", key);
    Numeric_delete(n);
    errno = EILSEQ;
    return NULL;
  }

  return n;
}

bool json_obj_get_bool(const struct wcjson_document *restrict const wc_doc,
                       const struct wcjson_value *restrict const wc_obj,
                       const wchar_t *restrict const key,
                       const size_t key_len) {
  struct wcjson_value *restrict const v =
      wcjson_object_get(wc_doc, wc_obj, key, key_len);

  if (v == NULL || !v->is_boolean) {
    json_werr(wc_doc, wc_obj, "No '%ls' bool item", key);
    errno = EILSEQ;
    return NULL;
  }

  return v->is_true;
}

const struct wcjson_value *
json_obj_get_optional_bool(const struct wcjson_document *restrict const wc_doc,
                           const struct wcjson_value *restrict const wc_obj,
                           const wchar_t *restrict const key,
                           const size_t key_len) {
  struct wcjson_value *restrict const v =
      wcjson_object_get(wc_doc, wc_obj, key, key_len);

  if (v != NULL && !v->is_boolean) {
    json_werr(wc_doc, wc_obj, "No '%ls' bool item", key);
    errno = EILSEQ;
    return NULL;
  }

  return v;
}

inline static void
json_heap_ops_grow_document(struct wcjson_document *restrict wc_doc) {
  if (wc_doc->v_next == wc_doc->v_nitems) {
    if (wc_doc->v_nitems == SIZE_MAX)
      panic();

    wc_doc->v_nitems++;
    wc_doc->values = heap_realloc(
        wc_doc->values, wc_doc->v_nitems * sizeof(struct wcjson_value));
  }
}

static void *json_heap_op_object_start(struct wcjson *wc_json, void *doc,
                                       void *parent) {
  struct json_heap_document *restrict heap_doc = doc;
  struct json_heap_value *restrict heap_obj = NULL;
  struct wcjson_value *restrict wc_parent = NULL;

  json_heap_ops_grow_document(heap_doc->wc_doc);

  if (parent != NULL)
    wc_parent =
        &heap_doc->wc_doc->values[((struct json_heap_value *)parent)->idx];

  const struct wcjson_value *restrict const wc_obj =
      wcjson_document_ops->object_start(wc_json, heap_doc->wc_doc, wc_parent);

  if (wc_obj != NULL) {
    heap_obj = heap_malloc(sizeof(struct json_heap_value));
    heap_obj->idx = wc_obj->idx;
    Array_add_tail(heap_doc->heap_values, heap_obj);
  }

  return heap_obj;
}

static void json_heap_op_object_add(struct wcjson *wc_json, void *doc,
                                    void *obj, void *key, void *value) {
  struct json_heap_document *restrict const heap_doc = doc;
  struct wcjson_value *restrict wc_obj = NULL;
  struct wcjson_value *restrict wc_key = NULL;
  struct wcjson_value *restrict wc_value = NULL;

  if (obj != NULL)
    wc_obj = &heap_doc->wc_doc->values[((struct json_heap_value *)obj)->idx];

  if (key != NULL)
    wc_key = &heap_doc->wc_doc->values[((struct json_heap_value *)key)->idx];

  if (value != NULL)
    wc_value =
        &heap_doc->wc_doc->values[((struct json_heap_value *)value)->idx];

  wcjson_document_ops->object_add(wc_json, heap_doc->wc_doc, wc_obj, wc_key,
                                  wc_value);
}

static void json_heap_op_object_end(struct wcjson *wc_json, void *doc,
                                    void *obj) {
  struct json_heap_document *restrict heap_doc = doc;
  struct wcjson_value *restrict wc_obj = NULL;

  if (obj != NULL)
    wc_obj = &heap_doc->wc_doc->values[((struct json_heap_value *)obj)->idx];

  wcjson_document_ops->object_end(wc_json, heap_doc->wc_doc, wc_obj);
}

static void *json_heap_op_array_start(struct wcjson *wc_json, void *doc,
                                      void *parent) {
  struct json_heap_document *restrict heap_doc = doc;
  struct json_heap_value *restrict heap_array = NULL;
  struct wcjson_value *restrict wc_parent = NULL;

  json_heap_ops_grow_document(heap_doc->wc_doc);

  if (parent != NULL)
    wc_parent =
        &heap_doc->wc_doc->values[((struct json_heap_value *)parent)->idx];

  const struct wcjson_value *restrict const wc_array =
      wcjson_document_ops->array_start(wc_json, heap_doc->wc_doc, wc_parent);

  if (wc_array != NULL) {
    heap_array = heap_malloc(sizeof(struct json_heap_value));
    heap_array->idx = wc_array->idx;
    Array_add_tail(heap_doc->heap_values, heap_array);
  }

  return heap_array;
}

static void json_heap_op_array_add(struct wcjson *wc_json, void *doc, void *arr,
                                   void *value) {
  struct json_heap_document *restrict const heap_doc = doc;

  struct wcjson_value *restrict wc_array = NULL;
  struct wcjson_value *restrict wc_value = NULL;

  if (arr != NULL)
    wc_array = &heap_doc->wc_doc->values[((struct json_heap_value *)arr)->idx];

  if (value != NULL)
    wc_value =
        &heap_doc->wc_doc->values[((struct json_heap_value *)value)->idx];

  wcjson_document_ops->array_add(wc_json, heap_doc->wc_doc, wc_array, wc_value);
}

static void json_heap_op_array_end(struct wcjson *wc_json, void *doc,
                                   void *arr) {
  struct json_heap_document *restrict const heap_doc = doc;
  struct wcjson_value *restrict wc_array = NULL;

  if (arr != NULL)
    wc_array = &heap_doc->wc_doc->values[((struct json_heap_value *)arr)->idx];

  wcjson_document_ops->array_end(wc_json, heap_doc->wc_doc, wc_array);
}

static void *json_heap_op_string_value(struct wcjson *wc_json, void *doc,
                                       const wchar_t *str, const size_t len,
                                       const bool escaped) {
  struct json_heap_document *restrict heap_doc = doc;
  struct json_heap_value *restrict heap_string = NULL;

  json_heap_ops_grow_document(heap_doc->wc_doc);

  struct wcjson_value *restrict const wc_string =
      wcjson_document_ops->string_value(wc_json, heap_doc->wc_doc, str, len,
                                        escaped);

  if (wc_string != NULL) {
    heap_string = heap_malloc(sizeof(struct json_heap_value));
    heap_string->idx = wc_string->idx;
    Array_add_tail(heap_doc->heap_values, heap_string);
  }

  return heap_string;
}

static void *json_heap_op_number_value(struct wcjson *wc_json, void *doc,
                                       const wchar_t *num, const size_t len) {
  struct json_heap_document *restrict heap_doc = doc;
  struct json_heap_value *restrict heap_number = NULL;

  json_heap_ops_grow_document(heap_doc->wc_doc);

  struct wcjson_value *restrict const wc_number =
      wcjson_document_ops->number_value(wc_json, heap_doc->wc_doc, num, len);

  if (wc_number != NULL) {
    heap_number = heap_malloc(sizeof(struct json_heap_value));
    heap_number->idx = wc_number->idx;
    Array_add_tail(heap_doc->heap_values, heap_number);
  }

  return heap_number;
}

static void *json_heap_op_bool_value(struct wcjson *wc_json, void *doc,
                                     const bool value) {
  struct json_heap_document *restrict heap_doc = doc;
  struct json_heap_value *restrict heap_bool = NULL;

  json_heap_ops_grow_document(heap_doc->wc_doc);

  struct wcjson_value *restrict const wc_bool =
      wcjson_document_ops->bool_value(wc_json, heap_doc->wc_doc, value);

  if (wc_bool != NULL) {
    heap_bool = heap_malloc(sizeof(struct json_heap_value));
    heap_bool->idx = wc_bool->idx;
    Array_add_tail(heap_doc->heap_values, heap_bool);
  }

  return heap_bool;
}

static void *json_heap_op_null_value(struct wcjson *wc_json, void *doc) {
  struct json_heap_document *restrict heap_doc = doc;
  struct json_heap_value *restrict heap_null = NULL;

  json_heap_ops_grow_document(heap_doc->wc_doc);

  struct wcjson_value *restrict const wc_null =
      wcjson_document_ops->null_value(wc_json, heap_doc->wc_doc);

  if (wc_null != NULL) {
    heap_null = heap_malloc(sizeof(struct json_heap_value));
    heap_null->idx = wc_null->idx;
    Array_add_tail(heap_doc->heap_values, heap_null);
  }

  return heap_null;
}
