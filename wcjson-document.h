/*
 * Copyright (c) 2025-2026 Christian Schulte <cs@schulte.it>
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
 * $JDTAUS: wcjson-document.h 9634 2026-07-22 02:43:37Z schulte $
 * Origin: https://github.com/wcjson/wcjson v0.40
 * Modifications: Changed to include wcjson.h from current directory.
 */

#ifndef WCJSON_DOCUMENT_H
#define WCJSON_DOCUMENT_H

#ifdef HAVE_HOST_H
#include "host.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

#include "wcjson.h"

extern const struct wcjson_ops *const wcjson_document_ops;

struct wcjson_value {
	unsigned is_null:1;
	unsigned is_boolean:1;
	unsigned is_true:1;
	unsigned is_string:1;
	unsigned is_number:1;
	unsigned is_object:1;
	unsigned is_array:1;
	unsigned is_pair:1;
	const wchar_t *string;
	size_t s_len;
	const char *mbstring;
	size_t mb_len;
	size_t idx;
	size_t head_idx;
	size_t tail_idx;
	size_t prev_idx;
	size_t next_idx;
};

struct wcjson_document {
	struct wcjson_value *values;
	size_t v_nitems;
	size_t v_nitems_cnt;
	size_t v_next;
	wchar_t *strings;
	size_t s_nitems;
	size_t s_nitems_cnt;
	size_t s_next;
	char *mbstrings;
	size_t mb_nitems;
	size_t mb_nitems_cnt;
	size_t mb_next;
	wchar_t *esc;
	size_t e_nitems;
	size_t e_nitems_cnt;
};

#define WCJSON_DOCUMENT_INITIALIZER					\
  {									\
      .values = NULL,							\
      .v_nitems = 0,							\
      .v_nitems_cnt = 0,						\
      .v_next = 0,							\
      .strings = NULL,							\
      .s_nitems = 0,							\
      .s_nitems_cnt = 0,						\
      .s_next = 0,							\
      .mbstrings = NULL,						\
      .mb_nitems = 0,							\
      .mb_nitems_cnt = 0,						\
      .mb_next = 0,							\
      .esc = NULL,							\
      .e_nitems = 0,							\
      .e_nitems_cnt = 0,						\
  }

WCJSON_EXPORT struct wcjson_value *wcjson_value_null(
    struct wcjson_document *doc);

WCJSON_EXPORT struct wcjson_value *wcjson_value_bool(
    struct wcjson_document *doc, const bool val);

WCJSON_EXPORT struct wcjson_value *wcjson_value_string(
    struct wcjson_document *doc, const wchar_t *val,
    const size_t len);

WCJSON_EXPORT struct wcjson_value *wcjson_value_number(
    struct wcjson_document *doc, const wchar_t *val,
    const size_t len);

WCJSON_EXPORT struct wcjson_value *wcjson_value_object(
    struct wcjson_document *doc);

WCJSON_EXPORT struct wcjson_value *wcjson_value_array(
    struct wcjson_document *doc);

#define wcjson_value_head(d, v)                                                \
  ((v)->head_idx == 0 ? NULL : &(d)->values[(v)->head_idx])

#define wcjson_value_next(d, v)                                                \
  ((v)->next_idx == 0 ? NULL : &(d)->values[(v)->next_idx])

#define wcjson_value_tail(d, v)                                                \
  ((v)->tail_idx == 0 ? NULL : &(d)->values[(v)->tail_idx])

#define wcjson_value_prev(d, v)                                                \
  ((v)->prev_idx == 0 ? NULL : &(d)->values[(v)->prev_idx])

#define wcjson_value_foreach(lval, d, v)				\
  for ((lval) = wcjson_value_head((d), (v)); (lval) != NULL;		\
       (lval) = wcjson_value_next((d), (lval)))

WCJSON_EXPORT int wcjson_array_add_head(const struct wcjson_document *doc,
    struct wcjson_value *arr,
    struct wcjson_value *val);

WCJSON_EXPORT int wcjson_array_add_tail(const struct wcjson_document *doc,
    struct wcjson_value *arr,
    struct wcjson_value *val);

WCJSON_EXPORT struct wcjson_value *wcjson_array_get(
    const struct wcjson_document *doc,
    const struct wcjson_value *arr, const size_t idx);

WCJSON_EXPORT struct wcjson_value *wcjson_array_remove(
    const struct wcjson_document *doc, struct wcjson_value *arr,
    const size_t idx);

WCJSON_EXPORT int wcjson_object_add_head(struct wcjson_document *doc,
    struct wcjson_value *obj,
    const wchar_t *key, const size_t len,
    const struct wcjson_value *val);

WCJSON_EXPORT int wcjson_object_add_tail(struct wcjson_document *doc,
    struct wcjson_value *obj,
    const wchar_t *key, const size_t len,
    const struct wcjson_value *val);

WCJSON_EXPORT struct wcjson_value *wcjson_object_remove(
    const struct wcjson_document *doc,
    struct wcjson_value *obj, const wchar_t *key,
    const size_t len);

WCJSON_EXPORT struct wcjson_value *wcjson_object_get(
    const struct wcjson_document *doc,
    const struct wcjson_value *obj, const wchar_t *key,
    const size_t len);

WCJSON_EXPORT wchar_t *wcjson_document_string(struct wcjson_document *doc,
    const wchar_t *s,
    const size_t len);

WCJSON_EXPORT char *wcjson_document_mbstring(struct wcjson_document *doc,
    const char *s, const size_t len);

WCJSON_EXPORT int wcjsondocvalues(struct wcjson *ctx,
    struct wcjson_document *doc,
    const wchar_t *txt, const size_t len);

WCJSON_EXPORT int wcjsondocstrings(struct wcjson *ctx,
    struct wcjson_document *doc);

WCJSON_EXPORT int wcjsondocmbstrings(struct wcjson *ctx,
    struct wcjson_document *doc);

WCJSON_EXPORT int wcjsondocfprint(FILE *f, const struct wcjson_document *doc,
    const struct wcjson_value *value);

WCJSON_EXPORT int wcjsondocfprintasc(FILE *f, const struct wcjson_document *doc,
    const struct wcjson_value *value);

WCJSON_EXPORT int wcjsondocsprint(wchar_t *s, size_t *lenp,
    const struct wcjson_document *doc,
    const struct wcjson_value *value);

WCJSON_EXPORT int wcjsondocsprintasc(wchar_t *s, size_t *lenp,
    const struct wcjson_document *doc,
    const struct wcjson_value *value);

#ifdef __cplusplus
}
#endif
#endif
