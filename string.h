/* $SchulteIT: string.h 15189 2025-10-27 05:41:45Z schulte $ */
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

#ifndef ABAG_STRING_H
#define ABAG_STRING_H

#include <stdbool.h>
#include <stddef.h>

struct String;

struct String *String_new(const struct String *restrict const, const size_t,
                          const size_t);
struct String *String_cnew(const char *restrict);
struct String *String_cnnew(const char *restrict, size_t);
struct String *String_use(struct String *restrict const);
bool String_equals(const struct String *restrict const,
                   const struct String *restrict const);
void String_delete(void *restrict const);

const char *String_chars(const struct String *restrict const);
const size_t String_length(const struct String *restrict const);
const size_t String_hash(const struct String *restrict const);

#endif
