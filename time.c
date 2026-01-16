/* $SchulteIT: time.c 15189 2025-10-27 05:41:45Z schulte $ */
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

#include "time.h"
#include "heap.h"
#include "math.h"
#include "proc.h"
#include "thread.h"

#include <errno.h>
#include <string.h>

#define TIME_ISO8601_MAX_LENGTH (size_t)128

#define IS_DIGIT(a) (a >= '0' && a <= '9')
#define IS_ALPHA(a) (a >= 'A' && a <= 'Z')
#define VALUE(a) (a - '0');

struct time_tls {
  struct nanos_from_iso8601_vars {
    struct Numeric *restrict s_ns;
    struct Numeric *restrict f_ns;
    struct Numeric *restrict secs;
  } nanos_from_iso8601;
  struct nanos_from_ts_vars {
    struct Numeric *restrict s;
    struct Numeric *restrict s_ns;
    struct Numeric *restrict ns;
  } nanos_from_ts;
  struct nanos_to_iso8601_vars {
    struct Numeric *restrict s;
  } nanos_to_iso8601;
};

extern const struct Numeric *restrict const zero;
extern const struct Numeric *restrict const second_nanos;

static tss_t time_tls_key;
static mtx_t time_mtx;

static struct time_tls *time_tls(void) {
  struct time_tls *restrict tls = tls_get(time_tls_key);
  if (tls == NULL) {
    tls = heap_malloc(sizeof(struct time_tls));
    tls->nanos_from_iso8601.s_ns = Numeric_new();
    tls->nanos_from_iso8601.f_ns = Numeric_new();
    tls->nanos_from_iso8601.secs = Numeric_new();
    tls->nanos_from_ts.s = Numeric_new();
    tls->nanos_from_ts.s_ns = Numeric_new();
    tls->nanos_from_ts.ns = Numeric_new();
    tls->nanos_to_iso8601.s = Numeric_new();
    tls_set(time_tls_key, tls);
  }
  return tls;
}

static void time_tls_dtor(void *restrict e) {
  struct time_tls *restrict const tls = e;
  Numeric_delete(tls->nanos_from_iso8601.s_ns);
  Numeric_delete(tls->nanos_from_iso8601.f_ns);
  Numeric_delete(tls->nanos_from_iso8601.secs);
  Numeric_delete(tls->nanos_from_ts.s);
  Numeric_delete(tls->nanos_from_ts.s_ns);
  Numeric_delete(tls->nanos_from_ts.ns);
  Numeric_delete(tls->nanos_to_iso8601.s);
  heap_free(tls);
  tls_set(time_tls_key, NULL);
}

void time_init(void) {
  tls_create(&time_tls_key, time_tls_dtor);
  mutex_init(&time_mtx);
}

void time_destroy(void) {
  tls_delete(time_tls_key);
  mutex_destroy(&time_mtx);
}

inline void time_now(struct timespec *restrict const ts) {
  if (!timespec_get(ts, TIME_UTC)) {
    werr("%s: %d: %s\n", __FILE__, __LINE__, __func__);
    fatal();
  }
}

bool nanos_from_iso8601(const char *restrict const iso, const size_t len,
                        struct Numeric *restrict const res) {
  const struct time_tls *restrict const tls = time_tls();
  struct Numeric *restrict const s_ns = tls->nanos_from_iso8601.s_ns;
  struct Numeric *restrict const f_ns = tls->nanos_from_iso8601.f_ns;
  struct Numeric *restrict const secs = tls->nanos_from_iso8601.secs;

  unsigned year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  char fr[len];
  fr[0] = '\0';
  char *frp = fr;
  char tz[len];
  tz[0] = '\0';
  char *tzp = tz;
  bool in_fraction = false;
  bool has_fraction = false;
  bool in_timezone = false;

  for (size_t i = 0; i < len; i++) {
    switch (i) {
    case 0: {
      if (!IS_DIGIT(iso[i]))
        return false;

      year = 1000 * VALUE(iso[i]);
      break;
    }
    case 1: {
      if (!IS_DIGIT(iso[i]))
        return false;

      year += 100 * VALUE(iso[i]);
      break;
    }
    case 2: {
      if (!IS_DIGIT(iso[i]))
        return false;

      year += 10 * VALUE(iso[i]);
      break;
    }
    case 3: {
      if (!IS_DIGIT(iso[i]))
        return false;

      year += VALUE(iso[i]);
      break;
    }
    case 4:
    case 7: {
      if (iso[i] != '-')
        return false;

      break;
    }
    case 5: {
      if (!IS_DIGIT(iso[i]))
        return false;

      month = 10 * VALUE(iso[i]);
      break;
    }
    case 6: {
      if (!IS_DIGIT(iso[i]))
        return false;

      month += VALUE(iso[i]);
      break;
    }
    case 8: {
      if (!IS_DIGIT(iso[i]))
        return false;

      day = 10 * VALUE(iso[i]);
      break;
    }
    case 9: {
      if (!IS_DIGIT(iso[i]))
        return false;

      day += VALUE(iso[i]);
      break;
    }
    case 10: {
      if (iso[i] != 'T')
        return false;

      break;
    }
    case 11: {
      if (!IS_DIGIT(iso[i]))
        return false;

      hour = 10 * VALUE(iso[i]);
      break;
    }
    case 12: {
      if (!IS_DIGIT(iso[i]))
        return false;

      hour += VALUE(iso[i]);
      break;
    }
    case 13:
    case 16: {
      if (iso[i] != ':')
        return false;

      break;
    }
    case 14: {
      if (!IS_DIGIT(iso[i]))
        return false;

      minute = 10 * VALUE(iso[i]);
      break;
    }
    case 15: {
      if (!IS_DIGIT(iso[i]))
        return false;

      minute += VALUE(iso[i]);
      break;
    }
    case 17: {
      if (!IS_DIGIT(iso[i]))
        return false;

      second = 10 * VALUE(iso[i]);
      break;
    }
    case 18: {
      if (!IS_DIGIT(iso[i]))
        return false;

      second += VALUE(iso[i]);
      break;
    }
    case 19: {
      if (iso[i] == '.' || iso[i] == ',') {
        in_fraction = true;
        has_fraction = true;
        *frp = '0';
        frp++;
        *frp = '.';
        frp++;
      } else if (IS_ALPHA(iso[i])) {
        in_timezone = true;
        *tzp = iso[i];
        tzp++;
      } else
        return false;

      break;
    }
    default: {
      if (in_fraction) {
        if (IS_DIGIT(iso[i])) {
          *frp = iso[i];
          frp++;
        } else if (IS_ALPHA(iso[i])) {
          in_fraction = false;
          in_timezone = true;
          *frp = '\0';
          *tzp = iso[i];
          tzp++;
        } else
          return false;

      } else if (in_timezone) {
        if (IS_ALPHA(iso[i])) {
          *tzp = iso[i];
          tzp++;
        } else
          return false;
      }
    }
    }
  }

  *frp = '\0';
  *tzp = '\0';

  struct tm t = {0};
  t.tm_sec = second;
  t.tm_min = minute;
  t.tm_hour = hour;
  t.tm_mday = day;
  t.tm_mon = month - 1;
  t.tm_year = year - 1900;
  t.tm_isdst = 0;

  time_t time = mktime(&t);
  if (time == (time_t)-1) {
    werr("%s: %d: %s\n", __FILE__, __LINE__, __func__);
    fatal();
  }

  Numeric_from_long_to(time, secs);
  Numeric_mul_to(secs, second_nanos, s_ns);

  if (has_fraction) {
    struct Numeric *restrict const fraction = Numeric_from_char(fr);

    if (fraction == NULL) {
      werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, fr);
      fatal();
    }

    Numeric_mul_to(fraction, second_nanos, f_ns);
    Numeric_delete(fraction);
    Numeric_add_to(s_ns, f_ns, res);
  } else
    Numeric_copy_to(s_ns, res);

  return true;
}

static inline void nanos_from_ts(struct Numeric *restrict const res,
                                 const struct timespec *restrict const ts) {
  const struct time_tls *restrict const tls = time_tls();
  struct Numeric *restrict const s = tls->nanos_from_ts.s;
  struct Numeric *restrict const s_ns = tls->nanos_from_ts.s_ns;
  struct Numeric *restrict const ns = tls->nanos_from_ts.ns;

  Numeric_from_long_to(ts->tv_sec, s);
  Numeric_from_long_to(ts->tv_nsec, ns);
  Numeric_mul_to(s, second_nanos, s_ns);
  Numeric_add_to(s_ns, ns, res);
}

void nanos_now(struct Numeric *restrict const res) {
  struct timespec ts;

  time_now(&ts);
  nanos_from_ts(res, &ts);
}

char *nanos_to_iso8601(const struct Numeric *restrict const nanos) {
  const struct time_tls *tls = time_tls();
  struct Numeric *restrict const s = tls->nanos_to_iso8601.s;

  Numeric_div_to(nanos, second_nanos, s);
  time_t time = Numeric_to_long(s);

  char *restrict const res = heap_malloc(TIME_ISO8601_MAX_LENGTH);
  mutex_lock(&time_mtx);
  struct tm *tm = localtime(&time);
  if (strftime(res, TIME_ISO8601_MAX_LENGTH, "%Y-%m-%dT%H:%M:%S%Z", tm) == 0) {
    werr("%s: %d: %s\n", __FILE__, __LINE__, __func__);
    fatal();
  }
  mutex_unlock(&time_mtx);
  return res;
}
