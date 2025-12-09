/* $SchulteIT: thread.c 15189 2025-10-27 05:41:45Z schulte $ */
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

#include "thread.h"
#include "proc.h"

const char *strthrd(const int r);

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
    return "timedout";
  default:
    werr("%s: %d: %s: %d\n", __FILE__, __LINE__, __func__, r);
    fatal();
  }
}

inline void thread_create(thrd_t *restrict const t, int (*entry)(void *),
                          void *arg) {
  int r = thrd_create(t, entry, arg);
  if (r != thrd_success) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strthrd(r));
    fatal();
  }
}

inline void thread_join(const thrd_t t, int *restrict const res) {
  int r = thrd_join(t, res);
  if (r != thrd_success) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strthrd(r));
    fatal();
  }
}

inline void thread_sleep(const struct timespec *restrict const duration) {
  int r = thrd_sleep(duration, NULL);
  if (r != thrd_success) {
    werr("%s: %d: %s. %s\n", __FILE__, __LINE__, __func__, strthrd(r));
    fatal();
  }
}

_Noreturn void thread_exit(const int res) { thrd_exit(res); }

inline void mutex_init(mtx_t *restrict const m) {
  int r = mtx_init(m, mtx_plain | mtx_recursive);
  if (r != thrd_success) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strthrd(r));
    fatal();
  }
}

inline void mutex_destroy(mtx_t *restrict const m) { mtx_destroy(m); }

inline void mutex_lock(mtx_t *restrict const m) {
  int r = mtx_lock(m);
  if (r != thrd_success) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strthrd(r));
    fatal();
  }
}

inline void mutex_unlock(mtx_t *restrict const m) {
  int r = mtx_unlock(m);
  if (r != thrd_success) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strthrd(r));
    fatal();
  }
}

inline void tls_create(tss_t *restrict const key, tss_dtor_t dtor) {
  int r = tss_create(key, dtor);
  if (r != thrd_success) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strthrd(r));
    fatal();
  }
}

inline void tls_delete(const tss_t key) { tss_delete(key); }

inline void *tls_get(const tss_t key) { return tss_get(key); }

inline void tls_set(const tss_t key, void *restrict const val) {
  int r = tss_set(key, val);
  if (r != thrd_success) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strthrd(r));
    fatal();
  }
}

inline void condition_init(cnd_t *restrict const cond) {
  int r = cnd_init(cond);
  if (r != thrd_success) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strthrd(r));
    fatal();
  }
}

inline void condition_destroy(cnd_t *restrict const cond) { cnd_destroy(cond); }

inline void condition_broadcast(cnd_t *restrict const cond) {
  int r = cnd_broadcast(cond);
  if (r != thrd_success) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strthrd(r));
    fatal();
  }
}

inline void condition_signal(cnd_t *restrict const cond) {
  int r = cnd_signal(cond);
  if (r != thrd_success) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strthrd(r));
    fatal();
  }
}

inline void condition_timedwait(cnd_t *restrict const cond,
                                mtx_t *restrict const mtx,
                                const struct timespec *restrict const ts) {
  int r = cnd_timedwait(cond, mtx, ts);
  if (r != thrd_success) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strthrd(r));
    fatal();
  }
}

inline void condition_wait(cnd_t *restrict const cond,
                           mtx_t *restrict const mtx) {
  int r = cnd_wait(cond, mtx);
  if (r != thrd_success) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strthrd(r));
    fatal();
  }
}
