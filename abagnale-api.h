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

#ifndef ABAGNALE_API_H
#define ABAGNALE_API_H

#ifdef HAVE_HOST_H
#include "host.h"
#endif

#include "abagnale.h"
#include "database.h"
#include "heap.h"
#include "http.h"
#include "patterns.h"
#include "proc.h"
#include "queue.h"
#include "thread.h"
#include "time.h"
#include "version.h"
#include "wcjson-document.h"

#include <signal.h>

// main.c: Type mismatches are intentional and of no interest to the linker.
extern const struct Numeric *restrict const Numeric_0;
extern const struct Numeric *restrict const Numeric_1;
extern const struct Numeric *restrict const Numeric_neg1;
extern const struct Numeric *restrict const Numeric_2;
extern const struct Numeric *restrict const Numeric_neg2;
extern const struct Numeric *restrict const Numeric_4;
extern const struct Numeric *restrict const Numeric_5;
extern const struct Numeric *restrict const Numeric_7;
extern const struct Numeric *restrict const Numeric_10;
extern const struct Numeric *restrict const Numeric_24;
extern const struct Numeric *restrict const Numeric_60;
extern const struct Numeric *restrict const Numeric_100;
extern const struct Numeric *restrict const Numeric_1000;

extern const struct Numeric *restrict const Numeric_usec_ns;
extern const struct Numeric *restrict const Numeric_msec_ns;
extern const struct Numeric *restrict const Numeric_sec_ns;
extern const struct Numeric *restrict const Numeric_min_ns;
extern const struct Numeric *restrict const Numeric_hour_ns;
extern const struct Numeric *restrict const Numeric_36hours_ns;
extern const struct Numeric *restrict const Numeric_day_ns;
extern const struct Numeric *restrict const Numeric_week_ns;

extern const struct String *restrict const app_nm;
extern const struct String *restrict const proc_id;
extern const volatile sig_atomic_t terminated;
extern const struct timespec thread_timeout;
extern const bool ticker_exporter;
extern const bool verbose;
extern const struct Array *restrict const volatility_windows;

extern const struct Algorithm *restrict const all_algorithms[];
extern const struct Exchange *restrict const all_exchanges[];

extern const struct Config *restrict const cnf;
extern const struct Array *restrict const algorithms;
extern const struct Array *restrict const exchanges;

#endif
