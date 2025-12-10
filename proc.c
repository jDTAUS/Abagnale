/* $SchulteIT: proc.c 15189 2025-10-27 05:41:45Z schulte $ */
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

#include "proc.h"
#include "thread.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static mtx_t stdout_mutex;
static mtx_t stderr_mutex;

#ifdef ABAG_DEBUG
_Noreturn void fatal(void) { abort(); }
#else
_Noreturn void fatal(void) { exit(EXIT_FAILURE); }
#endif

void proc_init(void) {
  mutex_init(&stdout_mutex);
  mutex_init(&stderr_mutex);
}

void proc_destroy(void) {
  mutex_destroy(&stdout_mutex);
  mutex_destroy(&stderr_mutex);
}

void wout(const char *restrict fmt, ...) {
  va_list ap;
  mutex_lock(&stdout_mutex);
  (void)fprintf(stdout, " info: ");
  va_start(ap, fmt);
  (void)vfprintf(stdout, fmt, ap);
  va_end(ap);
  (void)fflush(stdout);
  mutex_unlock(&stdout_mutex);
}

void werr(const char *restrict fmt, ...) {
  va_list ap;
  mutex_lock(&stderr_mutex);
  (void)fprintf(stderr, "error: ");
  va_start(ap, fmt);
  (void)vfprintf(stderr, fmt, ap);
  va_end(ap);
  (void)fflush(stderr);
  mutex_unlock(&stderr_mutex);
}

#ifdef ABAG_DEBUG
void wdebug(const char *restrict fmt, ...) {
  va_list ap;
  mutex_lock(&stdout_mutex);
  (void)fprintf(stdout, "debug: ");
  va_start(ap, fmt);
  (void)vfprintf(stdout, fmt, ap);
  va_end(ap);
  (void)fflush(stdout);
  mutex_unlock(&stdout_mutex);
}
#endif
