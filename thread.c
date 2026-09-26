/* $SchulteIT: thread.c 15189 2025-10-27 05:41:45Z schulte $ */
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

#ifdef HAVE_HOST_H
#include "host.h"
#endif

#include "heap.h"
#include "map.h"
#include "proc.h"
#include "thread.h"

#include <limits.h>
#include <stdint.h>

struct thread_tls {
  struct thread_locked_vars {
    struct Map *restrict mutexes;
  } thread_locked;
};

static tss_t thread_tls_key;

const char *strthrd(const int r);
struct thread_tls *const thread_tls(void);

struct thread_tls *const thread_tls(void) {
  struct thread_tls *restrict tls = tls_get(thread_tls_key);
  if (tls == NULL) {
    tls = heap_malloc(sizeof(struct thread_tls));
    tls->thread_locked.mutexes = Map_new(IdentityMapOps, 64);
    tls_set(thread_tls_key, tls);
  }
  return tls;
}

static void thread_tls_dtor(void *e) {
  struct thread_tls *restrict const tls = e;
  Map_delete(tls->thread_locked.mutexes, NULL);
  heap_free(tls);
  tls_set(thread_tls_key, NULL);
}

void thread_init(void) { tls_create(&thread_tls_key, thread_tls_dtor); }
void thread_destroy(void) { tls_delete(thread_tls_key); }

inline const char *strthrd(const int r) {
  switch (r) {
  case thrd_success:
    return "success";
  case thrd_busy:
    return "busy";
  case thrd_error:
    return "error";
  case thrd_nomem:
    return "nomem";
  case thrd_timedout:
    return "timeout";
  default:
    panic();
  }
}

inline void thread_create(thrd_t *restrict const t, int (*entry)(void *),
                          void *arg) {
  int r = thrd_create(t, entry, arg);
  if (r != thrd_success)
    fatal("%s", strthrd(r));
}

inline void thread_detach(thrd_t t) {
  int r = thrd_detach(t);
  if (r != thrd_success)
    fatal("%s", strthrd(r));
}

inline void thread_join(const thrd_t t, int *restrict const res) {
  int r = thrd_join(t, res);
  if (r != thrd_success)
    fatal("%s", strthrd(r));
}

inline void thread_sleep(const struct timespec *restrict const duration) {
  int r;
  struct timespec rmng;
  struct timespec drtn = *duration;

  do {
    rmng.tv_sec = -1;
    rmng.tv_nsec = -1;

    r = thrd_sleep(&drtn, &rmng);

    if (r < 0) {
      if (rmng.tv_sec == -1 && rmng.tv_nsec == -1)
        fatal("%d", r);

      drtn = rmng;
    }

  } while (r != 0 && (rmng.tv_sec != 0 || rmng.tv_nsec != 0));
}

inline bool thread_locked(const mtx_t *restrict const m) {
  const struct thread_tls *restrict const tls = thread_tls();
  struct Map *restrict const mutexes = tls->thread_locked.mutexes;

  return Map_exists(mutexes, m);
}

_Noreturn void thread_exit(const int res) { thrd_exit(res); }

inline void mutex_init(mtx_t *restrict const m) {
  int r = mtx_init(m, mtx_plain | mtx_recursive);
  if (r != thrd_success)
    fatal("%s", strthrd(r));
}

inline void mutex_destroy(mtx_t *restrict const m) { mtx_destroy(m); }

inline void mutex_lock(mtx_t *restrict const m) {
  const struct thread_tls *restrict const tls = thread_tls();
  struct Map *restrict const mutexes = tls->thread_locked.mutexes;

  int r = mtx_lock(m);
  if (r != thrd_success)
    fatal("%s", strthrd(r));

  if (Map_exists(mutexes, m)) {
    unsigned depth = (unsigned)(uintptr_t)Map_get(mutexes, m);

    if (depth == UINT_MAX)
      panic();

    Map_put(mutexes, m, (void *)(uintptr_t)(depth + 1));
  } else
    Map_put(mutexes, m, (void *)(uintptr_t)1);
}

inline bool mutex_trylock(mtx_t *restrict const m) {
  const struct thread_tls *restrict const tls = thread_tls();
  struct Map *restrict const mutexes = tls->thread_locked.mutexes;
  int r = mtx_trylock(m);

  if (r == thrd_busy)
    return false;

  if (r != thrd_success)
    fatal("%s", strthrd(r));

  if (Map_exists(mutexes, m)) {
    unsigned depth = (unsigned)(uintptr_t)Map_get(mutexes, m);

    if (depth == UINT_MAX)
      panic();

    Map_put(mutexes, m, (void *)(uintptr_t)(depth + 1));
  } else
    Map_put(mutexes, m, (void *)(uintptr_t)1);

  return true;
}

inline void mutex_unlock(mtx_t *restrict const m) {
  const struct thread_tls *restrict const tls = thread_tls();
  struct Map *restrict const mutexes = tls->thread_locked.mutexes;

  if (Map_exists(mutexes, m)) {
    unsigned depth = (unsigned)(uintptr_t)Map_get(mutexes, m);

    if (depth == 1)
      Map_remove(mutexes, m);
    else
      Map_put(mutexes, m, (void *)(uintptr_t)(depth - 1));
  }

  int r = mtx_unlock(m);
  if (r != thrd_success)
    fatal("%s", strthrd(r));
}

inline void tls_create(tss_t *restrict const key, tss_dtor_t dtor) {
  int r = tss_create(key, dtor);
  if (r != thrd_success)
    fatal("%s", strthrd(r));
}

inline void tls_delete(const tss_t key) { tss_delete(key); }

inline void *tls_get(const tss_t key) { return tss_get(key); }

inline void tls_set(const tss_t key, void *restrict const val) {
  int r = tss_set(key, val);
  if (r != thrd_success)
    fatal("%s", strthrd(r));
}

inline void condition_init(cnd_t *restrict const cond) {
  int r = cnd_init(cond);
  if (r != thrd_success)
    fatal("%s", strthrd(r));
}

inline void condition_destroy(cnd_t *restrict const cond) { cnd_destroy(cond); }

inline void condition_broadcast(cnd_t *restrict const cond) {
  int r = cnd_broadcast(cond);
  if (r != thrd_success)
    fatal("%s", strthrd(r));
}

inline void condition_signal(cnd_t *restrict const cond) {
  int r = cnd_signal(cond);
  if (r != thrd_success)
    fatal("%s", strthrd(r));
}

inline bool condition_timedwait(cnd_t *restrict const cond,
                                mtx_t *restrict const mtx,
                                const struct timespec *restrict const ts) {
  const struct thread_tls *restrict const tls = thread_tls();
  struct Map *restrict const mutexes = tls->thread_locked.mutexes;

  if (Map_exists(mutexes, mtx)) {
    unsigned depth = (unsigned)(uintptr_t)Map_get(mutexes, mtx);

    if (depth != 1)
      panic();

    Map_remove(mutexes, mtx);
  }

  int r = cnd_timedwait(cond, mtx, ts);

  Map_put(mutexes, mtx, (void *)(uintptr_t)1);

  switch (r) {
  case thrd_timedout:
    return false;
  default:
    if (r != thrd_success)
      fatal("%s", strthrd(r));
  }

  return true;
}

inline void condition_wait(cnd_t *restrict const cond,
                           mtx_t *restrict const mtx) {
  const struct thread_tls *restrict const tls = thread_tls();
  struct Map *restrict const mutexes = tls->thread_locked.mutexes;

  if (Map_exists(mutexes, mtx)) {
    unsigned depth = (unsigned)(uintptr_t)Map_get(mutexes, mtx);

    if (depth != 1)
      panic();

    Map_remove(mutexes, mtx);
  }

  int r = cnd_wait(cond, mtx);

  Map_put(mutexes, mtx, (void *)(uintptr_t)1);

  if (r != thrd_success)
    fatal("%s", strthrd(r));
}
