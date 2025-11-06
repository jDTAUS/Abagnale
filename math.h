/* $SchulteIT: math.h 15189 2025-10-27 05:41:45Z schulte $ */
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

#ifndef ABAG_MATH_H
#define ABAG_MATH_H

struct Numeric;

struct Numeric *Numeric_new(void);
void Numeric_delete(void *restrict const);

void *Numeric_db(const struct Numeric *restrict const);

struct Numeric *Numeric_from_char(const char *restrict const);

char *Numeric_to_char(const struct Numeric *restrict const, const int);

struct Numeric *Numeric_add(const struct Numeric *restrict const,
                            const struct Numeric *restrict const);
void Numeric_add_to(const struct Numeric *restrict const,
                    const struct Numeric *restrict const,
                    struct Numeric *restrict const);
struct Numeric *Numeric_sub(const struct Numeric *restrict const,
                            const struct Numeric *restrict const);
void Numeric_sub_to(const struct Numeric *restrict const,
                    const struct Numeric *restrict const,
                    struct Numeric *restrict const);
struct Numeric *Numeric_mul(const struct Numeric *restrict const,
                            const struct Numeric *restrict const);
void Numeric_mul_to(const struct Numeric *restrict const,
                    const struct Numeric *restrict const,
                    struct Numeric *restrict const);
struct Numeric *Numeric_div(const struct Numeric *restrict const,
                            const struct Numeric *restrict const);
void Numeric_div_to(const struct Numeric *restrict const,
                    const struct Numeric *restrict const,
                    struct Numeric *restrict const);

int Numeric_cmp(const struct Numeric *restrict const,
                const struct Numeric *restrict const);
struct Numeric *Numeric_from_int(const signed int);
void Numeric_from_int_to(const signed int, struct Numeric *restrict const);
int Numeric_to_int(const struct Numeric *restrict const);

struct Numeric *Numeric_from_long(const signed long int);
void Numeric_from_long_to(const signed long int,
                          struct Numeric *restrict const);
long Numeric_to_long(const struct Numeric *restrict const);

struct Numeric *Numeric_copy(const struct Numeric *restrict const);
void Numeric_copy_to(const struct Numeric *restrict const,
                     struct Numeric *restrict const);

struct Numeric *Numeric_from_double(const double);
void Numeric_from_double_to(const double, struct Numeric *restrict const);
double Numeric_to_double(const struct Numeric *restrict const);

void Numeric_abs(struct Numeric *restrict const);
void Numeric_scale(struct Numeric *restrict const, const int);

struct Numeric *Numeric_atan(const struct Numeric *restrict const);
void Numeric_atan_to(const struct Numeric *restrict const,
                     struct Numeric *restrict const);

void Numeric_char_free(char *restrict const);
#endif
