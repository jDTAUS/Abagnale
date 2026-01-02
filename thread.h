/* $SchulteIT: thread.h 15189 2025-10-27 05:41:45Z schulte $ */
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

#ifndef ABAG_THREAD_H
#define ABAG_THREAD_H

#include <threads.h>
#include <time.h>

void thread_create(thrd_t *restrict const, int (*)(void *), void *arg);
void thread_join(const thrd_t, int *restrict const);
void thread_sleep(const struct timespec *restrict const);
_Noreturn void thread_exit(const int);

void mutex_init(mtx_t *restrict const);
void mutex_destroy(mtx_t *restrict const);
void mutex_lock(mtx_t *restrict const);
void mutex_unlock(mtx_t *restrict const);

void tls_create(tss_t *restrict const, tss_dtor_t);
void tls_delete(tss_t);
void *tls_get(const tss_t);
void tls_set(const tss_t, void *restrict const);

void condition_init(cnd_t *restrict const);
void condition_destroy(cnd_t *restrict const);
void condition_broadcast(cnd_t *restrict const);
void condition_signal(cnd_t *restrict const);
void condition_timedwait(cnd_t *restrict const, mtx_t *restrict const,
                         const struct timespec *restrict const);
void condition_wait(cnd_t *restrict const, mtx_t *restrict const);
#endif
