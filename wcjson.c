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
 * Modifications: Changed to include wcjson.h from current directory.
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
#include <stdint.h>

#include "wcjson.h"

/* Binary to hex literal conversions */
#define B111 0x07
#define B1111 0x0f
#define B11111 0x1f
#define B111111 0x3f
#define B10000000 0x80
#define B11000000 0xc0
#define B11100000 0xe0
#define B11110000 0xf0
#define B11111000 0xf8
#define B1111111111 0x3ff
#define B11111000000 0x7c0
#define B111111000000 0xfc0
#define B1111000000000000 0xf000
#define B111111000000000000 0x3f000
#define B111000000000000000000 0x1c0000

static const wchar_t *const hex_digits = L"0123456789abcdef";

enum token {
	T_OBJ_START,
	T_OBJ_END,
	T_ARR_START,
	T_ARR_END,
	T_COMMA,
	T_COLON,
	T_QUOTE,
	T_TRUE,
	T_FALSE,
	T_NULL,
	T_NUMBER,
	T_UNKNOWN,
};

struct scan_state {
	size_t pos;
	size_t len;
	const wchar_t *txt;
	bool escaped;
};

static void *parse_array(struct scan_state *, struct wcjson *,
    const struct wcjson_ops *, void *, void *);
static void *parse_object(struct scan_state *, struct wcjson *,
    const struct wcjson_ops *, void *, void *);

static enum token
scan(struct scan_state *ss)
{
	switch (ss->txt[ss->pos]) {
	case L'{':
		return T_OBJ_START;
	case L'}':
		return T_OBJ_END;
	case L'[':
		return T_ARR_START;
	case L']':
		return T_ARR_END;
	case L',':
		return T_COMMA;
	case L':':
		return T_COLON;
	case L'"':
		return T_QUOTE;
	case L't':
		return T_TRUE;
	case L'f':
		return T_FALSE;
	case L'n':
		return T_NULL;
	case L'-':
		return T_NUMBER;
	default:
		return (ss->txt[ss->pos] >= L'0' && ss->txt[ss->pos] <= L'9') ?
		    T_NUMBER : T_UNKNOWN;
	}
}

static inline enum wcjson_status
scan_ws(struct scan_state *ss)
{
	for (; ss->pos < ss->len; ss->pos++)
		switch (ss->txt[ss->pos]) {
		case L'\t':
		case L'\n':
		case L'\r':
		case L' ':
			break;
		default:
			return WCJSON_OK;
		}

	return WCJSON_OK;
}

static inline enum wcjson_status
scan_literal(struct scan_state *ss, const wchar_t *lit, const size_t lit_len)
{
	size_t i = 0;

	for (; ss->pos < ss->len && i < lit_len; ss->pos++, i++)
		if (ss->txt[ss->pos] != lit[i])
			return WCJSON_ABORT_INVALID;

	if (i < lit_len)
		return WCJSON_ABORT_END_OF_INPUT;

	return WCJSON_OK;
}

static void *
parse_null(struct scan_state *ss, struct wcjson *ctx,
    const struct wcjson_ops *ops, void *doc)
{
	ctx->status = scan_literal(ss, L"null", 4);
	return ctx->status == WCJSON_OK && ops != NULL ? ops->null_value(ctx,
	    doc) : NULL;
}

static void *
parse_true(struct scan_state *ss, struct wcjson *ctx,
    const struct wcjson_ops *ops, void *doc)
{
	ctx->status = scan_literal(ss, L"true", 4);
	return ctx->status == WCJSON_OK && ops != NULL ?
	    ops->bool_value(ctx, doc, true) :
	    NULL;
}

static void *
parse_false(struct scan_state *ss, struct wcjson *ctx,
    const struct wcjson_ops *ops, void *doc)
{
	ctx->status = scan_literal(ss, L"false", 5);
	return ctx->status == WCJSON_OK && ops != NULL ?
	    ops->bool_value(ctx, doc, false) :
	    NULL;
}

static inline enum wcjson_status
scan_int(struct scan_state *ss)
{
	bool digits = false;
	bool zero = false;
	const size_t start = ss->pos;

	for (; ss->pos < ss->len; ss->pos++) {
		switch (ss->txt[ss->pos]) {
		case L'0':
			if (zero)
				return WCJSON_ABORT_INVALID;

			zero = ss->pos == start;
			digits = true;
			break;
		default:
			if (ss->txt[ss->pos] >= L'1' &&
			    ss->txt[ss->pos] <= L'9') {
				if (zero)
					return WCJSON_ABORT_INVALID;

				zero = false;
				digits = true;
			} else
				goto out;
			break;
		}
	}

out:
	return digits ? WCJSON_OK : WCJSON_ABORT_INVALID;
}

static inline enum wcjson_status
scan_frac(struct scan_state *ss)
{
	if (ss->txt[ss->pos] != L'.')
		return WCJSON_OK;

	if (++ss->pos == ss->len)
		return WCJSON_ABORT_END_OF_INPUT;

	bool digits = false;

	for (; ss->pos < ss->len; ss->pos++)
		if (ss->txt[ss->pos] >= L'0' && ss->txt[ss->pos] <= L'9')
			digits = true;
		else
			goto out;

out:
	return digits ? WCJSON_OK : WCJSON_ABORT_INVALID;
}

static inline enum wcjson_status
scan_exp(struct scan_state *ss)
{
	if (ss->txt[ss->pos] != L'e' && ss->txt[ss->pos] != L'E')
		return WCJSON_OK;

	if (++ss->pos == ss->len)
		return WCJSON_ABORT_END_OF_INPUT;

	bool op = false;
	bool digits = false;

	for (; ss->pos < ss->len; ss->pos++)
		switch (ss->txt[ss->pos]) {
		case L'-':
		case L'+':
			if (op)
				return WCJSON_ABORT_INVALID;

			op = true;
			break;
		default:
			if (ss->txt[ss->pos] >= L'0' &&
			    ss->txt[ss->pos] <= L'9')
				digits = true;
			else
				goto out;
			break;
		}

out:
	if (op)
		return digits ? WCJSON_OK : WCJSON_ABORT_END_OF_INPUT;
	else
		return digits ? WCJSON_OK : WCJSON_ABORT_INVALID;
}

static void *
parse_number(struct scan_state *ss, struct wcjson *ctx,
    const struct wcjson_ops *ops, void *doc)
{
	const size_t start = ss->pos;

	if (ss->txt[ss->pos] == L'-' && ++ss->pos == ss->len) {
		ctx->status = WCJSON_ABORT_END_OF_INPUT;
		return NULL;
	}

	ctx->status = scan_int(ss);

	if (ctx->status != WCJSON_OK)
		return NULL;

	if (ss->pos < ss->len) {
		ctx->status = scan_frac(ss);

		if (ctx->status != WCJSON_OK)
			return NULL;
	}

	if (ss->pos < ss->len) {
		ctx->status = scan_exp(ss);

		if (ctx->status != WCJSON_OK)
			return NULL;
	}

	return ops != NULL ?
	    ops->number_value(ctx, doc, &ss->txt[start], ss->pos - start) :
	    NULL;
}

static inline enum wcjson_status
scan_unescaped(struct scan_state *ss)
{
	for (; ss->pos < ss->len; ss->pos++)
		if (!((ss->txt[ss->pos] >= (wchar_t)0x20 &&
		    ss->txt[ss->pos] <= (wchar_t)0x21) ||
		    (ss->txt[ss->pos] >= (wchar_t)0x23 &&
		     ss->txt[ss->pos] <= (wchar_t)0x5b) ||
		    (ss->txt[ss->pos] >= (wchar_t)0x5d &&
#if defined(WCHAR_T_UTF32)
		     ss->txt[ss->pos] <= (wchar_t)0x10ffff)))
#elif defined(WCHAR_T_UTF16)
#if SIZEOF_WCHAR_T == 2
		    true)))
#else
           ss->txt[ss->pos] <= (wchar_t)0xffff)))
#endif
#elif defined(WCHAR_T_UTF8)
#if SIZEOF_WCHAR_T == 1
		    true)))
#else
		    ss->txt[ss->pos] <= (wchar_t)0xff)))
#endif
#else
#error "Wide character literal encoding not defined"
#endif
			break;

	return WCJSON_OK;
#undef __limit
}

static inline enum wcjson_status
scan_hex4(uint16_t *r, struct scan_state *ss)
{
	*r = 0;

	for (int e = 3; ss->pos < ss->len && e >= 0; ss->pos++, e--)
		if (ss->txt[ss->pos] >= L'0' && ss->txt[ss->pos] <= L'9')
			*r += (uint16_t)((ss->txt[ss->pos] - L'0') *
			    (1 << (e << 2)));
		else if (ss->txt[ss->pos] >= L'a' && ss->txt[ss->pos] <= L'f')
			*r += (uint16_t)((ss->txt[ss->pos] - L'a' + 10) *
			    (1 << (e << 2)));
		else if (ss->txt[ss->pos] >= L'A' && ss->txt[ss->pos] <= L'F')
			*r += (uint16_t)((ss->txt[ss->pos] - L'A' + 10) *
			    (1 << (e << 2)));
		else
			return WCJSON_ABORT_INVALID;

	return ss->pos < ss->len ? WCJSON_OK : WCJSON_ABORT_END_OF_INPUT;
}

static inline enum wcjson_status
scan_escaped(struct scan_state *ss)
{
	uint16_t unescaped;
	enum wcjson_status status;

	if (++ss->pos == ss->len)
		return WCJSON_ABORT_END_OF_INPUT;

	switch (ss->txt[ss->pos]) {
	case L'"':
	case L'\\':
	case L'/':
	case L'b':
	case L'f':
	case L'n':
	case L'r':
	case L't':
		if (++ss->pos == ss->len)
			return WCJSON_ABORT_END_OF_INPUT;

		ss->escaped = true;
		return WCJSON_OK;
	case L'u':
		if (++ss->pos == ss->len)
			return WCJSON_ABORT_END_OF_INPUT;

		status = scan_hex4(&unescaped, ss);

		if (status != WCJSON_OK)
			return status;

		if (unescaped < 0x20)
			return WCJSON_ABORT_INVALID;

		if (unescaped >= 0xd800 && unescaped <= 0xdfff) {
			// UTF 16 surrogates
			if (unescaped > 0xdbff || ss->txt[ss->pos] != L'\\')
				return WCJSON_ABORT_INVALID;

			if (++ss->pos == ss->len)
				return WCJSON_ABORT_END_OF_INPUT;

			if (ss->txt[ss->pos] != L'u')
				return WCJSON_ABORT_INVALID;

			if (++ss->pos == ss->len)
				return WCJSON_ABORT_END_OF_INPUT;

			status = scan_hex4(&unescaped, ss);

			if (status != WCJSON_OK)
				return status;

			if (unescaped < 0xdc00 || unescaped > 0xdfff)
				return WCJSON_ABORT_INVALID;

			ss->escaped = true;
		}

		return WCJSON_OK;
	default:
		return WCJSON_ABORT_INVALID;
	}
}

static void *
parse_string(struct scan_state *ss, struct wcjson *ctx,
    const struct wcjson_ops *ops, void *doc)
{
	ss->escaped = false;

	if (++ss->pos == ss->len) {
		ctx->status = WCJSON_ABORT_END_OF_INPUT;
		return NULL;
	}

	size_t start = ss->pos;

next_part:
	ctx->status = scan_unescaped(ss);

	if (ctx->status != WCJSON_OK)
		return NULL;

	if (ss->pos == ss->len) {
		ctx->status = WCJSON_ABORT_END_OF_INPUT;
		return NULL;
	}

	switch (ss->txt[ss->pos]) {
	case L'"':
		if (ss->pos < ss->len)
			ss->pos++;

		return ops != NULL ?
		    ss->pos == start ?
		    ops->string_value(ctx, doc, L"", 0, false) :
		    ops->string_value(ctx, doc, &ss->txt[start],
		    ss->pos - start - 1, ss->escaped) :
		    NULL;

	case L'\\':
		ctx->status = scan_escaped(ss);

		if (ctx->status != WCJSON_OK)
			return NULL;

		goto next_part;
	default:
		ctx->status = WCJSON_ABORT_INVALID;
		return NULL;
	}
}

static void *
parse_object(struct scan_state *ss, struct wcjson *ctx,
    const struct wcjson_ops *ops, void *doc,
    void *parent)
{
	void *obj = ops != NULL ? ops->object_start(ctx, doc, parent) : NULL;
	void *key = NULL;
	bool key_seen = false;
	bool value_seen = false;

	if (++ss->pos == ss->len) {
		ctx->status = WCJSON_ABORT_END_OF_INPUT;
		return NULL;
	}

next_token:
	ctx->status = scan_ws(ss);

	if (ctx->status != WCJSON_OK)
		return NULL;

	if (ss->pos == ss->len) {
		ctx->status = WCJSON_ABORT_END_OF_INPUT;
		return NULL;
	}

	switch (scan(ss)) {
	case T_OBJ_END: {
		if (key_seen) {
			ctx->status = WCJSON_ABORT_INVALID;
			return NULL;
		}

		if (ops != NULL)
			ops->object_end(ctx, doc, obj);

		if (ss->pos < ss->len)
			ss->pos++;

		return obj;
	}
	case T_QUOTE: {
		if (key_seen) {
			ctx->status = WCJSON_ABORT_INVALID;
			return NULL;
		}

		key = parse_string(ss, ctx, ops, doc);

		if (ctx->status != WCJSON_OK)
			return NULL;

		key_seen = true;
		goto next_token;
	}
	case T_COLON: {
		if (!key_seen) {
			ctx->status = WCJSON_ABORT_INVALID;
			return NULL;
		}

		if (++ss->pos == ss->len) {
			ctx->status = WCJSON_ABORT_END_OF_INPUT;
			return NULL;
		}

		ctx->status = scan_ws(ss);

		if (ctx->status != WCJSON_OK)
			return NULL;

		if (ss->pos == ss->len) {
			ctx->status = WCJSON_ABORT_END_OF_INPUT;
			return NULL;
		}

		switch (scan(ss)) {
		case T_TRUE: {
			void *value = parse_true(ss, ctx, ops, doc);

			if (ctx->status != WCJSON_OK)
				return NULL;

			if (ops != NULL) {
				ops->object_add(ctx, doc, obj, key, value);

				if (ctx->status != WCJSON_OK)
					return NULL;
			}

			key_seen = false;
			value_seen = true;
			break;
		}
		case T_FALSE: {
			void *value = parse_false(ss, ctx, ops, doc);

			if (ctx->status != WCJSON_OK)
				return NULL;

			if (ops != NULL) {
				ops->object_add(ctx, doc, obj, key, value);

				if (ctx->status != WCJSON_OK)
					return NULL;
			}

			key_seen = false;
			value_seen = true;
			break;
		}
		case T_NULL: {
			void *value = parse_null(ss, ctx, ops, doc);

			if (ctx->status != WCJSON_OK)
				return NULL;

			if (ops != NULL) {
				ops->object_add(ctx, doc, obj, key, value);

				if (ctx->status != WCJSON_OK)
					return NULL;
			}

			key_seen = false;
			value_seen = true;
			break;
		}
		case T_NUMBER: {
			void *value = parse_number(ss, ctx, ops, doc);

			if (ctx->status != WCJSON_OK)
				return NULL;

			if (ops != NULL) {
				ops->object_add(ctx, doc, obj, key, value);

				if (ctx->status != WCJSON_OK)
					return NULL;
			}

			key_seen = false;
			value_seen = true;
			break;
		}
		case T_QUOTE: {
			void *value = parse_string(ss, ctx, ops, doc);

			if (ctx->status != WCJSON_OK)
				return NULL;

			if (ops != NULL) {
				ops->object_add(ctx, doc, obj, key, value);

				if (ctx->status != WCJSON_OK)
					return NULL;
			}

			key_seen = false;
			value_seen = true;
			break;
		}
		case T_OBJ_START: {
			void *value = parse_object(ss, ctx, ops, doc, obj);

			if (ctx->status != WCJSON_OK)
				return NULL;

			if (ops != NULL) {
				ops->object_add(ctx, doc, obj, key, value);

				if (ctx->status != WCJSON_OK)
					return NULL;
			}

			key_seen = false;
			value_seen = true;
			break;
		}
		case T_ARR_START: {
			void *value = parse_array(ss, ctx, ops, doc, obj);

			if (ctx->status != WCJSON_OK)
				return NULL;

			if (ops != NULL) {
				ops->object_add(ctx, doc, obj, key, value);

				if (ctx->status != WCJSON_OK)
					return NULL;
			}

			key_seen = false;
			value_seen = true;
			break;
		}
		default:
			ctx->status = WCJSON_ABORT_INVALID;
			return NULL;
		}
		goto next_token;
	}
	case T_COMMA: {
		if (!value_seen) {
			ctx->status = WCJSON_ABORT_INVALID;
			return NULL;
		}
		value_seen = false;

		if (++ss->pos == ss->len) {
			ctx->status = WCJSON_ABORT_END_OF_INPUT;
			return NULL;
		}

		goto next_token;
	}
	default:
		ctx->status = WCJSON_ABORT_INVALID;
		return NULL;
	}
}

static void *
parse_array(struct scan_state *ss, struct wcjson *ctx,
    const struct wcjson_ops *ops, void *doc,
    void *parent)
{
	void *arr = ops != NULL ? ops->array_start(ctx, doc, parent) : NULL;
	bool value_seen = false;

	if (++ss->pos == ss->len) {
		ctx->status = WCJSON_ABORT_END_OF_INPUT;
		return NULL;
	}

next_token:
	ctx->status = scan_ws(ss);

	if (ctx->status != WCJSON_OK)
		return NULL;

	if (ss->pos == ss->len) {
		ctx->status = WCJSON_ABORT_END_OF_INPUT;
		return NULL;
	}

	switch (scan(ss)) {
	case T_ARR_END: {
		if (ss->pos < ss->len) {
			ss->pos++;
		}

		if (ops != NULL)
			ops->array_end(ctx, doc, arr);

		return arr;
	}
	case T_COMMA: {
		if (!value_seen) {
			ctx->status = WCJSON_ABORT_INVALID;
			return NULL;
		}
		value_seen = false;

		if (++ss->pos == ss->len) {
			ctx->status = WCJSON_ABORT_END_OF_INPUT;
			return NULL;
		}

		goto next_token;
	}
	case T_TRUE: {
		void *value = parse_true(ss, ctx, ops, doc);

		if (ctx->status != WCJSON_OK)
			return NULL;

		if (ops != NULL) {
			ops->array_add(ctx, doc, arr, value);

			if (ctx->status != WCJSON_OK)
				return NULL;
		}

		value_seen = true;
		goto next_token;
	}
	case T_FALSE: {
		void *value = parse_false(ss, ctx, ops, doc);

		if (ctx->status != WCJSON_OK)
			return NULL;

		if (ops != NULL) {
			ops->array_add(ctx, doc, arr, value);

			if (ctx->status != WCJSON_OK)
				return NULL;
		}

		value_seen = true;
		goto next_token;
	}
	case T_NULL: {
		void *value = parse_null(ss, ctx, ops, doc);

		if (ctx->status != WCJSON_OK)
			return NULL;

		if (ops != NULL) {
			ops->array_add(ctx, doc, arr, value);

			if (ctx->status != WCJSON_OK)
				return NULL;
		}

		value_seen = true;
		goto next_token;
	}
	case T_NUMBER: {
		void *value = parse_number(ss, ctx, ops, doc);

		if (ctx->status != WCJSON_OK)
			return NULL;

		if (ops != NULL) {
			ops->array_add(ctx, doc, arr, value);

			if (ctx->status != WCJSON_OK)
				return NULL;
		}

		value_seen = true;
		goto next_token;
	}
	case T_QUOTE: {
		void *value = parse_string(ss, ctx, ops, doc);

		if (ctx->status != WCJSON_OK)
			return NULL;

		if (ops != NULL) {
			ops->array_add(ctx, doc, arr, value);

			if (ctx->status != WCJSON_OK)
				return NULL;
		}

		value_seen = true;
		goto next_token;
	}
	case T_OBJ_START: {
		void *value = parse_object(ss, ctx, ops, doc, arr);

		if (ctx->status != WCJSON_OK)
			return NULL;

		if (ops != NULL) {
			ops->array_add(ctx, doc, arr, value);

			if (ctx->status != WCJSON_OK)
				return NULL;
		}

		value_seen = true;
		goto next_token;
	}
	case T_ARR_START: {
		void *value = parse_array(ss, ctx, ops, doc, arr);

		if (ctx->status != WCJSON_OK)
			return NULL;

		if (ops != NULL) {
			ops->array_add(ctx, doc, arr, value);

			if (ctx->status != WCJSON_OK)
				return NULL;
		}

		value_seen = true;
		goto next_token;
	}
	default:
		ctx->status = WCJSON_ABORT_INVALID;
		return NULL;
	}
}

static void
parse_json_text(struct scan_state *ss, struct wcjson *ctx,
    const struct wcjson_ops *ops, void *doc)
{
	ctx->status = scan_ws(ss);

	if (ctx->status != WCJSON_OK)
		return;

	if (ss->pos == ss->len)
		return;

	switch (scan(ss)) {
	case T_TRUE:
		parse_true(ss, ctx, ops, doc);
		break;
	case T_FALSE:
		parse_false(ss, ctx, ops, doc);
		break;
	case T_NULL:
		parse_null(ss, ctx, ops, doc);
		break;
	case T_NUMBER:
		parse_number(ss, ctx, ops, doc);
		break;
	case T_QUOTE:
		parse_string(ss, ctx, ops, doc);
		break;
	case T_OBJ_START:
		parse_object(ss, ctx, ops, doc, NULL);
		break;
	case T_ARR_START:
		parse_array(ss, ctx, ops, doc, NULL);
		break;
	default:
		ctx->status = WCJSON_ABORT_INVALID;
		return;
	}

	if (ctx->status != WCJSON_OK)
		return;

	ctx->status = scan_ws(ss);

	if (ctx->status == WCJSON_OK && ss->pos != ss->len)
		ctx->status = WCJSON_ABORT_INVALID;
}

int
wcjson(struct wcjson *ctx, const struct wcjson_ops *ops, void *doc,
    const wchar_t *txt, const size_t len)
{
	ctx->status = WCJSON_OK;
	ctx->errnum = 0;

	if (txt != NULL && len > 0 && len < SIZE_MAX) {
		struct scan_state ss = {
			.pos = 0,
			.len = len,
			.txt = txt,
			.escaped = false,
		};

		parse_json_text(&ss, ctx, ops, doc);
	} else
		ctx->status = WCJSON_ABORT_INVALID;

	return ctx->status == WCJSON_OK ? 0 : -1;
}

static int
wctojsons_json(const wchar_t c, wchar_t *d, size_t *d_lenp)
{
	if (*d_lenp < 2)
		goto err_range;

	*d++ = '\\';
	*d = c;
	*d_lenp = 2;
	return 0;
err_range:
	errno = ERANGE;
	return -1;
}

static inline int
wctojsons_uhex4(const uint32_t c, wchar_t *d,
    size_t *d_lenp)
{
	if (*d_lenp < 6)
		goto err_range;

	*d++ = L'\\';
	*d++ = L'u';
	*d++ = hex_digits[(c >> 12) & 0xf];
	*d++ = hex_digits[(c >> 8) & 0xf];
	*d++ = hex_digits[(c >> 4) & 0xf];
	*d++ = hex_digits[c & 0xf];
	*d_lenp = 6;
	return 0;
err_range:
	errno = ERANGE;
	return -1;
}

#if defined(WCHAR_T_UTF32) || defined(WCHAR_T_UTF8)
static inline int
wctojsons_utf32(const uint32_t c, wchar_t *d,
    size_t *d_lenp)
{
	size_t written, d_len = *d_lenp;

	if (c > 0xffff) {
		// UTF 16 surrogates
		written = d_len;

		if (wctojsons_uhex4((0xd800 |
		    (((c - 0x10000) >> 10) & B1111111111)), d, &written))
			return -1;

		d += written;
		d_len -= written;
		written = d_len;

		if (wctojsons_uhex4((0xdc00 | (c & B1111111111)), d, &written))
			return -1;

		d_len -= written;
		*d_lenp -= d_len;
	} else {
		written = d_len;
		if (wctojsons_uhex4(c, d, &written) < 0)
			return -1;

		d_len -= written;
		*d_lenp -= d_len;
	}

	return 0;
}
#endif

static int
wctojsons_ascii(const wchar_t *s, size_t *s_lenp, wchar_t *d,
    size_t *d_lenp)
{
	size_t written, d_len = *d_lenp;

#if defined(WCHAR_T_UTF32)
	written = d_len;

	if (wctojsons_utf32((uint32_t)*s, d, &written) < 0)
		return -1;

	d_len -= written;
	*s_lenp = 1;
	*d_lenp -= d_len;
	return 0;
#elif defined(WCHAR_T_UTF16)
	if (*s >= 0xd800 && *s <= 0xdfff) {
		// UTF 16 surrogates
		if (*s > 0xdbff)
			goto err_ilseq;

		written = d_len;
		if (wctojsons_uhex4((uint32_t)*s, d, &written))
			return -1;

		d += written;
		d_len -= written;

		if ((*s_lenp)-- == 0)
			goto err_range;

		s++;

		if (*s < 0xdc00 || *s > 0xdfff)
			goto err_ilseq;

		written = d_len;
		if (wctojsons_uhex4((uint32_t)*s, d, &written))
			return -1;

		d_len -= written;

		*s_lenp = 2;
		*d_lenp -= d_len;
	} else {
		written = d_len;
		if (wctojsons_uhex4((uint32_t)*s, d, &written) < 0)
			return -1;

		d_len -= written;

		*s_lenp = 1;
		*d_lenp -= d_len;
	}
	return 0;
err_range:
	errno = ERANGE;
	return -1;
err_ilseq:
	errno = EILSEQ;
	return -1;
#elif defined(WCHAR_T_UTF8)
	// Decode UTF 8
	uint32_t cp;

	if ((*s & B11111000) == B11110000) {
		if (4 > *s_lenp || (s[3] & B11000000) != B10000000 ||
		    (s[2] & B11000000) != B10000000 || (s[1] & B11000000) !=
		    B10000000)
			goto err_ilseq;

		cp = ((uint32_t)(s[0] & B111) << 18) |
		    ((uint32_t)(s[1] & B111111) << 12) |
		    ((uint32_t)(s[2] & B111111) << 6) |
		    (uint32_t)(s[3] & B111111);

		*s_lenp = 4;
	} else if ((*s & B11110000) == B11100000) {
		if (3 > *s_lenp || (s[2] & B11000000) != B10000000 ||
		    (s[1] & B11000000) != B10000000)
			goto err_ilseq;

		cp = ((uint32_t)(s[0] & B1111) << 12) |
		    ((uint32_t)(s[1] & B111111) << 6) |
		    (uint32_t)(s[2] & B111111);

		*s_lenp = 3;
	} else if ((*s & B11100000) == B11000000) {
		if (2 > *s_lenp || (s[1] & B11000000) != B10000000)
			goto err_ilseq;

		cp = ((uint32_t)(s[0] & B11111) << 6) |
		    (uint32_t)(s[1] & B111111);

		*s_lenp = 2;
	} else if (*s < 0x80) {
		cp = (uint32_t)*s;
		*s_lenp = 1;
	} else
		goto err_ilseq;

	written = d_len;
	if (wctojsons_utf32(cp, d, &written) < 0)
		return -1;

	d_len -= written;
	*d_lenp -= d_len;
	return 0;
err_ilseq:
	errno = EILSEQ;
	return -1;
#else
#error "Wide character literal encoding not defined"
#endif
}

static int
wctojsons(const wchar_t *s, size_t s_len, wchar_t *d, size_t *d_lenp,
    bool ascii)
{
	size_t d_len = *d_lenp;
	size_t read, written;
	wchar_t *c = NULL;

	while (s_len != 0 && d_len != 0) {
		switch (*s) {
		case L'"':
			c = L"\"";
			break;
		case L'\\':
			c = L"\\";
			break;
		case L'\b':
			c = L"b";
			break;
		case L'\f':
			c = L"f";
			break;
		case L'\n':
			c = L"n";
			break;
		case L'\r':
			c = L"r";
			break;
		case L'\t':
			c = L"t";
			break;
		default:
			if (*s < 0x20)
				goto err_ilseq;

			if (ascii && *s > 0x7f) {
				read = s_len;
				written = d_len;

				if (wctojsons_ascii(s, &read, d, &written) < 0)
					return -1;

				s += read;
				s_len -= read;
				d += written;
				d_len -= written;
			} else {
				*d++ = *s++;
				d_len--;
				s_len--;
			}

			continue;
		}

		written = d_len;
		if (wctojsons_json(*c, d, &written) < 0)
			return -1;

		s++;
		s_len--;

		d += written;
		d_len -= written;
	}

	if (s_len != 0)
		goto err_range;

	*d_lenp -= d_len;
	return 0;
err_range:
	errno = ERANGE;
	return -1;
err_ilseq:
	errno = EILSEQ;
	return -1;
}

int
wctowcjsons(const wchar_t *s, size_t s_len, wchar_t *d, size_t *d_lenp)
{
	return wctojsons(s, s_len, d, d_lenp, false);
}

int
wctoascjsons(const wchar_t *s, size_t s_len, wchar_t *d, size_t *d_lenp)
{
	return wctojsons(s, s_len, d, d_lenp, true);
}

static int
wcjsonstowc_backslash_u(const wchar_t *s, size_t *s_lenp, wchar_t *d,
    size_t *d_lenp)
{
	size_t s_len = *s_lenp;
	size_t d_len = *d_lenp;
	struct scan_state ss = {0};
	enum wcjson_status status;
	uint16_t hs, ls;
	uint32_t cp;

	if (s_len-- == 0)
		goto err_ilseq;

	s++;

	ss.pos = 0;
	ss.txt = s;
	ss.len = s_len;

	status = scan_hex4(&hs, &ss);

	if (status != WCJSON_OK || hs < 0x20)
		goto err_ilseq;

	s += 4;
	s_len -= 4;

	cp = hs;

	if (hs >= 0xd800 && hs <= 0xdfff) {
		// UTF 16 surrogates
		if (hs > 0xdbff || 6 > s_len || *s++ != '\\' || *s++ != 'u')
			goto err_ilseq;

		s_len -= 2;

		ss.pos = 0;
		ss.txt = s;
		ss.len = s_len;

		status = scan_hex4(&ls, &ss);

		if (status != WCJSON_OK)
			goto err_ilseq;

		s_len -= 4;

		if (ls < 0xdc00 || ls > 0xdfff)
			goto err_ilseq;

		cp = (((uint32_t)(hs & B1111111111) << 10) |
		    (uint32_t)(ls & B1111111111)) + (uint32_t)0x10000;
	}

#if defined(WCHAR_T_UTF32)
	if (d_len-- == 0)
		goto err_range;
	*d = (wchar_t)cp;
#elif defined(WCHAR_T_UTF16)
	if (cp <= 0xffff) {
		if (d_len-- == 0)
			goto err_range;
		*d = (wchar_t)cp;
	} else {
		// UTF 16 surrogates
		if (2 > d_len)
			goto err_range;

		*d++ = (wchar_t)(0xd800 |
		    (((cp - 0x10000) >> 10) & B1111111111));

		*d = (wchar_t)(0xdc00 | ((cp - 0x10000) & B1111111111));
		d_len -= 2;
	}
#elif defined(WCHAR_T_UTF8)
	if (cp < 0x80) {
		if (d_len-- == 0)
			goto err_range;

		*d = (wchar_t)cp & 0xff;
	} else if (cp >= 0x80 && cp <= 0x7ff) {
		if (2 > d_len)
			goto err_range;

		*d++ = (wchar_t)(B11000000 | ((cp & B11111000000) >> 6));
		*d = (wchar_t)(B10000000 | (cp & B111111));
		d_len -= 2;
	} else if (cp >= 0x800 && cp <= 0xffff) {
		if (3 > d_len)
			goto err_range;

		*d++ = (wchar_t)(B11100000 | ((cp & B1111000000000000) >> 12));
		*d++ = (wchar_t)(B10000000 | ((cp & B111111000000) >> 6));
		*d = (wchar_t)(B10000000 | (cp & B111111));
		d_len -= 3;
	} else if (cp >= 0x10000 && cp <= 0x10ffff) {
		if (4 > d_len)
			goto err_range;

		*d++ = (wchar_t)(B11110000 |
		    ((cp & B111000000000000000000) >> 18));

		*d++ = (wchar_t)(B10000000 |
		    ((cp & B111111000000000000) >> 12));

		*d++ = (wchar_t)(B10000000 | ((cp & B111111000000) >> 6));
		*d = (wchar_t)(B10000000 | (cp & B111111));
		d_len -= 4;
	} else
		goto err_ilseq;
#else
#error "Wide character literal encoding not defined"
#endif
	*s_lenp -= s_len;
	*d_lenp -= d_len;
	return 0;
err_ilseq:
	errno = EILSEQ;
	return -1;
err_range:
	errno = ERANGE;
	return -1;
}

static int
wcjsonstowc_backslash(const wchar_t *s, size_t *s_lenp, wchar_t *d,
    size_t *d_lenp)
{
	size_t s_len = *s_lenp;
	size_t d_len = *d_lenp;
	size_t read, written;

	if (s_len-- == 0 || d_len == 0)
		goto err_range;

	switch (*++s) {
	case L'"':
		*d = L'"';
		s_len--;
		d_len--;
		break;
	case L'\\':
		*d = L'\\';
		s_len--;
		d_len--;
		break;
	case L'/':
		*d = L'/';
		s_len--;
		d_len--;
		break;
	case L'b':
		*d = L'\b';
		s_len--;
		d_len--;
		break;
	case L'f':
		*d = L'\f';
		s_len--;
		d_len--;
		break;
	case L'n':
		*d = L'\n';
		s_len--;
		d_len--;
		break;
	case L'r':
		*d = L'\r';
		s_len--;
		d_len--;
		break;
	case L't':
		*d = L'\t';
		s_len--;
		d_len--;
		break;
	case L'u':
		read = s_len;
		written = d_len;

		if (wcjsonstowc_backslash_u(s, &read, d, &written))
			return -1;

		s_len -= read;
		d_len -= written;
		break;
	default:
		goto err_ilseq;
	}

	*s_lenp -= s_len;
	*d_lenp -= d_len;
	return 0;
err_range:
	errno = ERANGE;
	return -1;
err_ilseq:
	errno = EILSEQ;
	return -1;
}

int
wcjsonstowc(const wchar_t *s, size_t s_len, wchar_t *d, size_t *d_lenp)
{
	size_t d_len = *d_lenp;
	size_t read, written;

	while (s_len != 0 && d_len != 0) {
		switch (*s) {
		case L'\\': {
			read = s_len;
			written = d_len;

			if (wcjsonstowc_backslash(s, &read, d, &written))
				return -1;

			s += read;
			s_len -= read;
			d += written;
			d_len -= written;
			break;
		}
		default:
			*d++ = *s++;
			s_len--;
			d_len--;
		}
	}

	if (s_len != 0)
		goto err_range;

	*d_lenp -= d_len;
	return 0;
err_range:
	errno = ERANGE;
	return -1;
}
#ifdef __cplusplus
}
#endif
