/* $SchulteIT: array.h 15189 2025-10-27 05:41:45Z schulte $ */
/* $JDTAUS$ */

/*
 * Copyright (c) 2018 - 2026 Christian Schulte <cs@schulte.it>
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

#ifndef ABAG_ARRAY_H
#define ABAG_ARRAY_H

#include <stddef.h>
#include <threads.h>

struct Array;

struct Array *Array_new(const size_t);
void Array_delete(struct Array *restrict const,
                  void (*cb)(void *restrict const));

mtx_t *Array_mutex(struct Array *restrict const);
void Array_lock(struct Array *restrict const);
void Array_unlock(struct Array *restrict const);

struct Array *Array_copy(const struct Array *restrict const,
                         void *(*cb)(void *restrict const));
void Array_clear(struct Array *restrict const,
                 void (*cb)(void *restrict const));
void Array_shrink(struct Array *restrict const);

void Array_add_tail(struct Array *restrict const, void *restrict const);
void *Array_tail(const struct Array *restrict const);
void Array_add_head(struct Array *restrict const, void *restrict const);
void *Array_head(const struct Array *);

void *Array_remove_tail(struct Array *restrict const);
void *Array_remove_head(struct Array *restrict const);
void *Array_remove_idx(struct Array *restrict const, const size_t);

const size_t Array_size(const struct Array *restrict const);
void **Array_items(const struct Array *restrict const);
#endif
