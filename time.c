/* $SchulteIT: time.c 15189 2025-10-27 05:41:45Z schulte $ */
/* $JDTAUS: time.c 9623 2026-07-06 18:05:45Z schulte $ */

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
#include "math.h"
#include "proc.h"
#include "thread.h"
#include "time.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TIME_ISO8601_MAX_LENGTH (size_t)128
#define TIME_NANOS_CHARS_MAX_LENGTH (size_t)128

#define IS_DIGIT(a) (a >= '0' && a <= '9')
#define VALUE(a) (a - '0')

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
  struct nanos_string_vars {
    struct Numeric *restrict r0;
    struct Numeric *restrict r1;
    struct Numeric *restrict r2;
  } nanos_string;
};

extern const struct Numeric *restrict const zero;
extern const struct Numeric *restrict const second_nanos;
extern const struct Numeric *restrict const minute_nanos;
extern const struct Numeric *restrict const hour_nanos;
extern const struct Numeric *restrict const day_nanos;
extern const struct Numeric *restrict const week_nanos;

static tss_t time_tls_key;

static struct time_tls *const time_tls(void) {
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
    tls->nanos_string.r0 = Numeric_new();
    tls->nanos_string.r1 = Numeric_new();
    tls->nanos_string.r2 = Numeric_new();
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
  Numeric_delete(tls->nanos_string.r0);
  Numeric_delete(tls->nanos_string.r1);
  Numeric_delete(tls->nanos_string.r2);
  heap_free(tls);
  tls_set(time_tls_key, NULL);
}

void time_init(void) { tls_create(&time_tls_key, time_tls_dtor); }

void time_destroy(void) { tls_delete(time_tls_key); }

inline void time_now(struct timespec *restrict const ts) {
  if (timespec_get(ts, TIME_UTC) == 0)
    fatal("%s", "timespec_get");
}

bool nanos_from_iso8601(const char *restrict const iso, const size_t len,
                        struct Numeric *restrict const res) {
  const struct time_tls *restrict const tls = time_tls();
  struct Numeric *restrict const s_ns = tls->nanos_from_iso8601.s_ns;
  struct Numeric *restrict const f_ns = tls->nanos_from_iso8601.f_ns;
  struct Numeric *restrict const secs = tls->nanos_from_iso8601.secs;
  struct Numeric *restrict fraction = NULL;
  char fr[TIME_ISO8601_MAX_LENGTH + 1] = {0};
  char *restrict fr_p = fr;
  const char *restrict p;

  if (len < 19)
    return false;

  if (!(IS_DIGIT(iso[0]) && IS_DIGIT(iso[1]) && IS_DIGIT(iso[2]) &&
        IS_DIGIT(iso[3]) && IS_DIGIT(iso[5]) && IS_DIGIT(iso[6]) &&
        IS_DIGIT(iso[8]) && IS_DIGIT(iso[9]) && IS_DIGIT(iso[11]) &&
        IS_DIGIT(iso[12]) && IS_DIGIT(iso[14]) && IS_DIGIT(iso[15]) &&
        IS_DIGIT(iso[17]) && IS_DIGIT(iso[18]) && iso[4] == '-' &&
        iso[7] == '-' && iso[10] == 'T' && iso[13] == ':' && iso[16] == ':'))
    return false;

  struct tm t = {
      .tm_sec = 10 * VALUE(iso[17]) + VALUE(iso[18]),
      .tm_min = 10 * VALUE(iso[14]) + VALUE(iso[15]),
      .tm_hour = 10 * VALUE(iso[11]) + VALUE(iso[12]),
      .tm_mday = 10 * VALUE(iso[8]) + VALUE(iso[9]),
      .tm_mon = 10 * VALUE(iso[5]) + VALUE(iso[6]) - 1,
      .tm_year = 1000 * VALUE(iso[0]) + 100 * VALUE(iso[1]) +
                 10 * VALUE(iso[2]) + VALUE(iso[3]) - 1900,
      .tm_isdst = -1,
  };

  int tz_sec = 0;
  bool neg = false;

  if (len > 19) {
    p = &iso[19];

    if (*p == '.' || *p == ',') {
      *fr_p++ = '0';
      *fr_p++ = '.';
      p++;

      size_t fr_len = sizeof(fr) - 3;

      while (fr_len-- != 0 && IS_DIGIT(*p))
        *fr_p++ = *p++;

      if (fr_len == SIZE_MAX)
        panic();

      if (fr_p - fr < 3)
        return false;

      *fr_p = '\0';
      fraction = Numeric_from_char(fr);
      if (fraction == NULL)
        return false;
    }

    if (*p != 'z' && *p != 'Z') {
      if (*p == '-') {
        neg = true;
        p++;
      } else if (*p == '+')
        p++;

      if (!(IS_DIGIT(*p) && IS_DIGIT(*(p + 1))))
        return false;

      tz_sec = (10 * VALUE(*p) + VALUE(*(p + 1))) * 3600;
      p += 2;

      if (*p == ':')
        p++;

      if (!(IS_DIGIT(*p) && IS_DIGIT(*(p + 1))))
        return false;

      tz_sec += (10 * VALUE(*p) + VALUE(*(p + 1))) * 60;
    }
  }

  if (neg)
    t.tm_sec += tz_sec;
  else
    t.tm_sec -= tz_sec;

  time_t time = timegm(&t);
  if (time == (time_t)-1)
    fatal("%s", strerror(errno));

  Numeric_from_long_to(time, secs);
  Numeric_mul_to(secs, second_nanos, s_ns);

  if (fraction != NULL) {
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
  const struct time_tls *restrict const tls = time_tls();
  struct Numeric *restrict const s = tls->nanos_to_iso8601.s;
  struct tm t = {0};

  Numeric_div_to(nanos, second_nanos, s);
  time_t time = Numeric_to_long(s);

#if defined(_MSC_VER)
  // This is not errno_t and localtime_s from C Annex K
  errno_t lt_s = localtime_s(&t, &time);
  if (lt_s != 0)
    fatal("%s", strerror(lt_s));
#else
  if (localtime_r(&time, &t) == NULL)
    fatal("%s", "localtime_r");
#endif

  char *restrict const r = heap_malloc(TIME_ISO8601_MAX_LENGTH + 1);
  if (strftime(r, TIME_ISO8601_MAX_LENGTH + 1, "%Y-%m-%dT%H:%M:%S%Z", &t) == 0)
    panic();

  return r;
}

char *nanos_string(const struct Numeric *restrict const nanos) {
  const struct time_tls *restrict const tls = time_tls();
  struct Numeric *restrict const r0 = tls->nanos_string.r0;
  struct Numeric *restrict const r1 = tls->nanos_string.r1;
  struct Numeric *restrict const r2 = tls->nanos_string.r2;

  Numeric_div_to(nanos, week_nanos, r0);
  Numeric_scale(r0, 0);
  char *restrict const weeks = Numeric_to_char(r0, 0);
  Numeric_mul_to(r0, week_nanos, r1);
  Numeric_sub_to(nanos, r1, r0);

  Numeric_div_to(r0, day_nanos, r1);
  Numeric_scale(r1, 0);
  char *restrict const days = Numeric_to_char(r1, 0);
  Numeric_mul_to(r1, day_nanos, r2);
  Numeric_sub_to(r0, r2, r1);

  Numeric_div_to(r1, hour_nanos, r0);
  Numeric_scale(r0, 0);
  char *restrict const hours = Numeric_to_char(r0, 0);
  Numeric_mul_to(r0, hour_nanos, r2);
  Numeric_sub_to(r1, r2, r0);

  Numeric_div_to(r0, minute_nanos, r1);
  Numeric_scale(r1, 0);
  char *restrict const minutes = Numeric_to_char(r1, 0);
  Numeric_mul_to(r1, minute_nanos, r2);
  Numeric_sub_to(r0, r2, r1);

  Numeric_div_to(r1, second_nanos, r0);
  Numeric_scale(r0, 0);
  char *restrict const seconds = Numeric_to_char(r0, 0);
  Numeric_mul_to(r0, second_nanos, r2);
  Numeric_sub_to(r1, r2, r0);

  char *restrict const ns = Numeric_to_char(r0, 0);
  char *restrict const chars = heap_malloc(TIME_NANOS_CHARS_MAX_LENGTH + 1);
  const int r =
      snprintf(chars, TIME_NANOS_CHARS_MAX_LENGTH + 1, "%sw%sd%sh%sm%ss%sns",
               weeks, days, hours, minutes, seconds, ns);

  if (r < 0 || (size_t)r >= TIME_NANOS_CHARS_MAX_LENGTH + 1)
    panic();

  Numeric_char_free(weeks);
  Numeric_char_free(days);
  Numeric_char_free(hours);
  Numeric_char_free(minutes);
  Numeric_char_free(seconds);
  Numeric_char_free(ns);

  return chars;
}
