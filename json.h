/* $JDTAUS$ */

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

#ifndef JSON_H
#define JSON_H

#ifdef HAVE_HOST_H
#include "host.h"
#endif

#include "math.h"
#include "string.h"
#include "wcjson-document.h"

void json_init(void);
void json_destroy(void);

struct wcjson_value *
wcjson_value_mbstring(struct wcjson_document *restrict const,
                      const char *restrict const, const size_t);

struct wcjson_value *
wcjson_value_mbnumber(struct wcjson_document *restrict const,
                      const char *restrict const, const size_t);

int wcjson_document_build(struct wcjson *restrict,
                          struct wcjson_document *restrict);

const char *json_mbserror(const struct wcjson *restrict const);

int json_mbsprint(char *restrict const, size_t *restrict const,
                  const struct wcjson_document *restrict const,
                  const struct wcjson_value *restrict const);

int json_mbparse(struct wcjson_document *restrict, const char *restrict const,
                 const size_t);

struct String *json_obj_get_string(const struct wcjson_document *restrict const,
                                   const struct wcjson_value *restrict const,
                                   const wchar_t *restrict const, const size_t);

struct String *
json_obj_get_optional_string(const struct wcjson_document *restrict const,
                             const struct wcjson_value *restrict const,
                             const wchar_t *restrict const, const size_t);

struct Numeric *
json_obj_get_string_number(const struct wcjson_document *restrict const,
                           const struct wcjson_value *restrict const,
                           const wchar_t *restrict const, const size_t);

struct Numeric *json_obj_get_optional_string_number(
    const struct wcjson_document *restrict const,
    const struct wcjson_value *restrict const, const wchar_t *restrict const,
    const size_t);

struct Numeric *
json_obj_get_string_iso8601(const struct wcjson_document *restrict const,
                            const struct wcjson_value *restrict const,
                            const wchar_t *restrict const, const size_t);

struct Numeric *json_obj_get_optional_string_iso8601(
    const struct wcjson_document *restrict const,
    const struct wcjson_value *restrict const, const wchar_t *restrict const,
    const size_t);

bool json_obj_get_bool(const struct wcjson_document *restrict const,
                       const struct wcjson_value *restrict const,
                       const wchar_t *restrict const, const size_t);

const struct wcjson_value *
json_obj_get_optional_bool(const struct wcjson_document *restrict const,
                           const struct wcjson_value *restrict const,
                           const wchar_t *restrict const, const size_t);

#endif
