/* $SchulteIT: math-postgresql.c 15189 2025-10-27 05:41:45Z schulte $ */
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

#include "heap.h"
#include "math.h"
#include "proc.h"

#include <limits.h>
#include <math.h>
#include <pgtypes_numeric.h>

struct Numeric {
  numeric *restrict n;
#ifdef ABAG_MATH_DEBUG
  char *restrict s;
#endif
};

extern const struct Numeric *restrict const zero;
extern const struct Numeric *restrict const n_one;

inline struct Numeric *Numeric_new(void) {
  numeric *res = PGTYPESnumeric_new();
  if (res == NULL) {
    werr("%s: %d: %s: Failure allocating numeric\n", __FILE__, __LINE__,
         __func__);
    fatal();
  }
  struct Numeric *restrict n = heap_malloc(sizeof(struct Numeric));
  n->n = res;
#ifdef ABAG_MATH_DEBUG
  n->s = NULL;
#endif
  return n;
}

inline void Numeric_delete(void *restrict const n) {
  if (n == NULL)
    return;

  struct Numeric *restrict const num = n;
  PGTYPESnumeric_free(num->n);
#ifdef ABAG_MATH_DEBUG
  Numeric_char_free(num->s);
#endif
  heap_free(num);
}

inline void *Numeric_db(const struct Numeric *restrict const n) { return n->n; }

inline struct Numeric *Numeric_from_char(const char *restrict const s) {
  struct Numeric *restrict n = NULL;
  // XXX: (char *)
  numeric *res = PGTYPESnumeric_from_asc((char *)s, NULL);
  if (res != NULL) {
    n = heap_malloc(sizeof(struct Numeric));
    n->n = res;
#ifdef ABAG_MATH_DEBUG
    n->s = Numeric_to_char(n, 20);
#endif
  }
  return n;
}

inline char *Numeric_to_char(const struct Numeric *restrict const n,
                             const int d) {
  char *restrict const s = PGTYPESnumeric_to_asc(n->n, d);
  if (s == NULL) {
    werr("%s: %d: %s: Failure creating string from numeric\n", __FILE__,
         __LINE__, __func__);
    fatal();
  }
  return s;
}

inline void Numeric_char_free(char *restrict const s) { PGTYPESchar_free(s); }

inline struct Numeric *Numeric_add(const struct Numeric *restrict const n1,
                                   const struct Numeric *restrict const n2) {
  struct Numeric *restrict const res = Numeric_new();
  Numeric_add_to(n1, n2, res);
  return res;
}

inline void Numeric_add_to(const struct Numeric *restrict const n1,
                           const struct Numeric *restrict const n2,
                           struct Numeric *restrict const res) {
  const int ret = PGTYPESnumeric_add(n1->n, n2->n, res->n);
  if (ret < 0) {
    werr("%s: %d: %s: Failure adding numerics\n", __FILE__, __LINE__, __func__);
    fatal();
  }
#ifdef ABAG_MATH_DEBUG
  Numeric_char_free(res->s);
  res->s = Numeric_to_char(res, 20);
#endif
}

inline struct Numeric *Numeric_sub(const struct Numeric *restrict const n1,
                                   const struct Numeric *restrict const n2) {
  struct Numeric *restrict const res = Numeric_new();
  Numeric_sub_to(n1, n2, res);
  return res;
}

inline void Numeric_sub_to(const struct Numeric *restrict const n1,
                           const struct Numeric *restrict const n2,
                           struct Numeric *restrict const res) {
  const int ret = PGTYPESnumeric_sub(n1->n, n2->n, res->n);
  if (ret < 0) {
    werr("%s: %d: %s: Failure substracting numerics\n", __FILE__, __LINE__,
         __func__);
    fatal();
  }
#ifdef ABAG_MATH_DEBUG
  Numeric_char_free(res->s);
  res->s = Numeric_to_char(res, 20);
#endif
}

inline struct Numeric *Numeric_mul(const struct Numeric *restrict const n1,
                                   const struct Numeric *restrict const n2) {
  struct Numeric *restrict const res = Numeric_new();
  Numeric_mul_to(n1, n2, res);
  return res;
}

inline void Numeric_mul_to(const struct Numeric *restrict const n1,
                           const struct Numeric *restrict const n2,
                           struct Numeric *restrict const res) {
  const int ret = PGTYPESnumeric_mul(n1->n, n2->n, res->n);
  if (ret < 0) {
    werr("%s: %d: %s: Failure multiplying numerics\n", __FILE__, __LINE__,
         __func__);
    fatal();
  }
#ifdef ABAG_MATH_DEBUG
  Numeric_char_free(res->s);
  res->s = Numeric_to_char(res, 20);
#endif
}

inline struct Numeric *Numeric_div(const struct Numeric *restrict const n1,
                                   const struct Numeric *restrict const n2) {
  struct Numeric *restrict const res = Numeric_new();
  Numeric_div_to(n1, n2, res);
  return res;
}

inline void Numeric_div_to(const struct Numeric *restrict const n1,
                           const struct Numeric *restrict const n2,
                           struct Numeric *restrict const res) {
  const int ret = PGTYPESnumeric_div(n1->n, n2->n, res->n);
  if (ret < 0) {
    werr("%s: %d: %s: Failure dividing numerics\n", __FILE__, __LINE__,
         __func__);
    fatal();
  }
#ifdef ABAG_MATH_DEBUG
  Numeric_char_free(res->s);
  res->s = Numeric_to_char(res, 20);
#endif
}

inline int Numeric_cmp(const struct Numeric *restrict const n1,
                       const struct Numeric *restrict const n2) {
  const int ret = PGTYPESnumeric_cmp(n1->n, n2->n);
  if (ret == INT_MAX) {
    werr("%s: %d: %s: Failure comparing numerics\n", __FILE__, __LINE__,
         __func__);
    fatal();
  }
  return ret;
}

inline struct Numeric *Numeric_from_int(const signed int i) {
  struct Numeric *restrict const res = Numeric_new();
  Numeric_from_int_to(i, res);
  return res;
}

inline void Numeric_from_int_to(const signed int i,
                                struct Numeric *restrict const res) {
  const int ret = PGTYPESnumeric_from_int(i, res->n);
  if (ret < 0) {
    werr("%s: %d: %s: %d: Failure creating numeric from integer\n", __FILE__,
         __LINE__, __func__, i);
    fatal();
  }
#ifdef ABAG_MATH_DEBUG
  res->s = Numeric_to_char(res, 20);
#endif
}

inline int Numeric_to_int(const struct Numeric *restrict const n) {
  int res = 0;
  const int ret = PGTYPESnumeric_to_int(n->n, &res);
  if (ret < 0) {
    werr("%s: %d: %s: Failure creating integer from numeric\n", __FILE__,
         __LINE__, __func__);
    fatal();
  }
  return res;
}

inline struct Numeric *Numeric_from_long(const signed long int l) {
  struct Numeric *restrict const res = Numeric_new();
  Numeric_from_long_to(l, res);
  return res;
}

inline void Numeric_from_long_to(const signed long int l,
                                 struct Numeric *restrict const res) {
  const int ret = PGTYPESnumeric_from_long(l, res->n);
  if (ret < 0) {
    werr("%s: %d: %s: %ld: Failure creating numeric from long\n", __FILE__,
         __LINE__, __func__, l);
    fatal();
  }
#ifdef ABAG_MATH_DEBUG
  res->s = Numeric_to_char(res, 20);
#endif
}

inline long Numeric_to_long(const struct Numeric *restrict const n) {
  long res = 0;
  const int ret = PGTYPESnumeric_to_long(n->n, &res);
  if (ret < 0) {
    werr("%s: %d: %s: Failure creating long integer from numeric\n", __FILE__,
         __LINE__, __func__);
    fatal();
  }
  return res;
}

inline struct Numeric *Numeric_copy(const struct Numeric *restrict const n) {
  struct Numeric *restrict const res = Numeric_new();
  Numeric_copy_to(n, res);
  return res;
}

inline void Numeric_copy_to(const struct Numeric *restrict const n,
                            struct Numeric *restrict const res) {
  const int ret = PGTYPESnumeric_copy(n->n, res->n);
  if (ret < 0) {
    werr("%s: %d: %s: Failure copying numerics\n", __FILE__, __LINE__,
         __func__);
    fatal();
  }
#ifdef ABAG_MATH_DEBUG
  Numeric_char_free(res->s);
  res->s = Numeric_to_char(res, 20);
#endif
}

inline struct Numeric *Numeric_from_double(const double d) {
  struct Numeric *res = Numeric_new();
  Numeric_from_double_to(d, res);
  return res;
}

inline void Numeric_from_double_to(const double d,
                                   struct Numeric *restrict const res) {
  const int ret = PGTYPESnumeric_from_double(d, res->n);
  if (ret < 0) {
    werr("%s: %d: %s: %lf: Failure creating numeric from double\n", __FILE__,
         __LINE__, __func__, d);
    fatal();
  }
#ifdef ABAG_MATH_DEBUG
  res->s = Numeric_to_char(res, 20);
#endif
}

inline double Numeric_to_double(const struct Numeric *restrict const n) {
  double res = 0;
  const int ret = PGTYPESnumeric_to_double(n->n, &res);
  if (ret < 0) {
    werr("%s: %d: %s: Failure creating double from numeric\n", __FILE__,
         __LINE__, __func__);
    fatal();
  }
  return res;
}

inline void Numeric_abs(struct Numeric *restrict const n) {
  if (Numeric_cmp(n, zero) < 0) {
    const int ret = PGTYPESnumeric_mul(n_one->n, n->n, n->n);
    if (ret < 0) {
      werr("%s: %d: %s: Failure calculating absolute numeric value\n", __FILE__,
           __LINE__, __func__);
      fatal();
    }
  }
#ifdef ABAG_MATH_DEBUG
  Numeric_char_free(n->s);
  n->s = Numeric_to_char(n, 20);
#endif
}

inline void Numeric_scale(struct Numeric *restrict const n, const int scale) {
  char *restrict const s = Numeric_to_char(n, scale);
  struct Numeric *restrict const scaled = Numeric_from_char(s);
  if (scaled == NULL) {
    werr("%s: %d: %s: Failure scaling numeric\n", __FILE__, __LINE__, __func__);
    fatal();
  }
  Numeric_copy_to(scaled, n);
  Numeric_delete(scaled);
  Numeric_char_free(s);
}

inline struct Numeric *Numeric_atan(const struct Numeric *restrict const n) {
  struct Numeric *restrict const res = Numeric_new();
  Numeric_atan_to(n, res);
  return res;
}

inline void Numeric_atan_to(const struct Numeric *restrict const n,
                            struct Numeric *restrict const res) {
  Numeric_from_double_to(atan(Numeric_to_double(n)), res);
#ifdef ABAG_MATH_DEBUG
  Numeric_char_free(res->s);
  res->s = Numeric_to_char(res, 20);
#endif
}

inline struct Numeric *Numeric_cos(const struct Numeric *restrict const n) {
  struct Numeric *restrict const res = Numeric_new();
  Numeric_cos_to(n, res);
  return res;
}

inline void Numeric_cos_to(const struct Numeric *restrict const n,
                           struct Numeric *restrict const res) {
  Numeric_from_double_to(cos(Numeric_to_double(n)), res);
#ifdef ABAG_MATH_DEBUG
  Numeric_char_free(res->s);
  res->s = Numeric_to_char(res, 20);
#endif
}
