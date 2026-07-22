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
 * $JDTAUS$
 * Origin: https://github.com/wcjson/wcjson v0.40
 * Modifications: None.
 */

#ifndef WCJSON_WCJSON_H
#define WCJSON_WCJSON_H

#ifdef HAVE_HOST_H
#include "host.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

#ifdef HAVE_WCJSON_HOST_H
#include <wcjson-host.h>
#endif

#ifndef WCJSON_EXPORT
#define WCJSON_EXPORT
#endif

#ifndef WCJSON_NO_EXPORT
#define WCJSON_NO_EXPORT
#endif

#ifndef WCJSON_DEPRECATED
#define WCJSON_DEPRECATED
#endif

#ifndef WCJSON_DEPRECATED_EXPORT
#define WCJSON_DEPRECATED_EXPORT
#endif

#ifndef WCJSON_DEPRECATED_NO_EXPORT
#define WCJSON_DEPRECATED_NO_EXPORT
#endif

#define WCJSON_ESCAPE_MAX 12

enum wcjson_status {
	WCJSON_OK,
	WCJSON_ABORT_ERROR,
	WCJSON_ABORT_INVALID,
	WCJSON_ABORT_END_OF_INPUT,
};

struct wcjson {
	enum wcjson_status status;
	int errnum;
};

#define WCJSON_INITIALIZER						\
  {									\
      .status = WCJSON_OK,						\
      .errnum = 0,							\
  }

struct wcjson_ops {
	void *(*object_start)(struct wcjson *ctx, void *doc, void *parent);
	void (*object_add)(struct wcjson *ctx, void *doc, void *obj, void *key,
	    void *value);
	void (*object_end)(struct wcjson *ctx, void *doc, void *obj);
	void *(*array_start)(struct wcjson *ctx, void *doc, void *parent);
	void (*array_add)(struct wcjson *ctx, void *doc, void *arr, void *value);
	void (*array_end)(struct wcjson *ctx, void *doc, void *arr);
	void *(*string_value)(struct wcjson *ctx, void *doc, const wchar_t *str,
	    const size_t len, const bool escaped);
	void *(*number_value)(struct wcjson *ctx, void *doc, const wchar_t *num,
	    const size_t len);
	void *(*bool_value)(struct wcjson *ctx, void *doc, const bool value);
	void *(*null_value)(struct wcjson *ctx, void *doc);
};

WCJSON_EXPORT int wcjson(struct wcjson *ctx, const struct wcjson_ops *ops,
    void *doc, const wchar_t *txt, const size_t len);

WCJSON_EXPORT int wctowcjsons(const wchar_t *s, size_t s_len, wchar_t *d,
    size_t *d_lenp);

WCJSON_EXPORT int wctoascjsons(const wchar_t *s, size_t s_len, wchar_t *d,
    size_t *d_lenp);

WCJSON_EXPORT int wcjsonstowc(const wchar_t *s, size_t s_len, wchar_t *d,
    size_t *d_lenp);

#ifdef __cplusplus
}
#endif
#endif
