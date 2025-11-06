/* $SchulteIT: algorithm-trend.c 15268 2025-11-04 21:48:33Z schulte $ */
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

#include "abagnale.h"
#include "config.h"
#include "database.h"
#include "heap.h"
#include "proc.h"
#include "thread.h"
#include "time.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define nitems(a) (sizeof((a)) / sizeof((a)[0]))

#define TREND_UUID "bfd87009-ea0f-4664-a03a-f9b6e91274dd"

extern _Atomic bool terminated;
extern const struct Numeric *restrict const zero;
extern const struct Numeric *restrict const n_one;
extern const struct Numeric *restrict const fourty;
extern const struct Numeric *restrict const hundred;

extern const struct Config *restrict const cnf;
extern const bool verbose;

struct trend_state {
  struct Numeric *restrict cd_lnanos;
  struct Numeric *restrict cd_langle;
  enum candle_trend cd_ltrend;
};

struct trend_tls {
  struct trend_position_select_vars {
    struct Numeric *restrict r0;
    struct Numeric *restrict r1;
    struct Numeric *restrict fee_pc;
    struct Numeric *restrict cd_pc;
    struct Numeric *restrict cd_n_pc;
    struct Numeric *restrict d_pc;
    struct Numeric *restrict s_pc;
    struct Numeric *restrict pr_min;
    struct Numeric *restrict pr_max;
    struct Numeric *restrict pr_cur;
    struct Candle *restrict cd_cur;
    struct Candle *restrict cd_first;
    struct Candle *restrict cd_last;
    struct db_plot_res *restrict plot_res;
    struct db_trend_state_res *restrict st_res;
  } trend_position_select;
  struct trend_product_plot_vars {
    struct db_datapoint_res *restrict pt_res;
    struct db_candle_res *restrict cd_res;
  } trend_product_plot;
  struct trend_configure_vars {
    struct db_trend_state_res *restrict st_res;
  } trend_configure;
  struct trend_position_done_vars {
    struct db_trend_state_res *restrict st_res;
  } trend_position_done;
};

static tss_t trend_tls_key;

static const struct {
  enum candle_trend trend;
  char *restrict db;
} candle_trend_map[] = {
    {CANDLE_UP, "UP"},
    {CANDLE_DOWN, "DOWN"},
    {CANDLE_NONE, "NONE"},
};

static struct trend_tls *trend_tls(void) {
  struct trend_tls *tls = tls_get(trend_tls_key);
  if (tls == NULL) {
    tls = heap_malloc(sizeof(struct trend_tls));
    tls->trend_position_select.r0 = Numeric_from_int(0);
    tls->trend_position_select.r1 = Numeric_from_int(0);
    tls->trend_position_select.cd_pc = Numeric_from_int(0);
    tls->trend_position_select.cd_n_pc = Numeric_from_int(0);
    tls->trend_position_select.fee_pc = Numeric_from_int(0);
    tls->trend_position_select.d_pc = Numeric_from_int(0);
    tls->trend_position_select.s_pc = Numeric_from_int(0);
    tls->trend_position_select.pr_min = Numeric_from_int(0);
    tls->trend_position_select.pr_max = Numeric_from_int(0);
    tls->trend_position_select.pr_cur = Numeric_new();
    tls->trend_position_select.cd_cur = Candle_new();
    tls->trend_position_select.cd_first = Candle_new();
    tls->trend_position_select.cd_last = Candle_new();
    tls->trend_position_select.plot_res =
        heap_malloc(sizeof(struct db_plot_res));
    tls->trend_position_select.plot_res->snanos = Numeric_from_int(0);
    tls->trend_position_select.plot_res->enanos = Numeric_from_int(0);
    tls->trend_position_select.st_res =
        heap_malloc(sizeof(struct db_trend_state_res));
    tls->trend_position_select.st_res->cd_lnanos = Numeric_new();
    tls->trend_position_select.st_res->cd_langle = Numeric_new();
    tls->trend_product_plot.pt_res =
        heap_malloc(sizeof(struct db_datapoint_res));
    tls->trend_product_plot.pt_res->x = Numeric_new();
    tls->trend_product_plot.pt_res->y = Numeric_new();
    tls->trend_product_plot.cd_res = heap_malloc(sizeof(struct db_candle_res));
    tls->trend_product_plot.cd_res->o = Numeric_new();
    tls->trend_product_plot.cd_res->h = Numeric_new();
    tls->trend_product_plot.cd_res->l = Numeric_new();
    tls->trend_product_plot.cd_res->c = Numeric_new();
    tls->trend_product_plot.cd_res->onanos = Numeric_new();
    tls->trend_product_plot.cd_res->hnanos = Numeric_new();
    tls->trend_product_plot.cd_res->lnanos = Numeric_new();
    tls->trend_product_plot.cd_res->cnanos = Numeric_new();
    tls->trend_configure.st_res =
        heap_malloc(sizeof(struct db_trend_state_res));
    tls->trend_configure.st_res->cd_lnanos = Numeric_new();
    tls->trend_configure.st_res->cd_langle = Numeric_new();
    tls->trend_position_done.st_res =
        heap_malloc(sizeof(struct db_trend_state_res));
    tls->trend_position_done.st_res->cd_lnanos = Numeric_new();
    tls->trend_position_done.st_res->cd_langle = Numeric_new();
    tls_set(trend_tls_key, tls);
  }
  return tls;
}

static void trend_tls_dtor(void *e) {
  struct trend_tls *tls = e;
  Numeric_delete(tls->trend_position_select.r0);
  Numeric_delete(tls->trend_position_select.r1);
  Numeric_delete(tls->trend_position_select.cd_pc);
  Numeric_delete(tls->trend_position_select.cd_n_pc);
  Numeric_delete(tls->trend_position_select.fee_pc);
  Numeric_delete(tls->trend_position_select.d_pc);
  Numeric_delete(tls->trend_position_select.s_pc);
  Numeric_delete(tls->trend_position_select.pr_min);
  Numeric_delete(tls->trend_position_select.pr_max);
  Numeric_delete(tls->trend_position_select.pr_cur);
  Candle_delete(tls->trend_position_select.cd_cur);
  Candle_delete(tls->trend_position_select.cd_first);
  Candle_delete(tls->trend_position_select.cd_last);
  Numeric_delete(tls->trend_position_select.plot_res->snanos);
  Numeric_delete(tls->trend_position_select.plot_res->enanos);
  heap_free(tls->trend_position_select.plot_res);
  Numeric_delete(tls->trend_position_select.st_res->cd_lnanos);
  Numeric_delete(tls->trend_position_select.st_res->cd_langle);
  heap_free(tls->trend_position_select.st_res);
  Numeric_delete(tls->trend_product_plot.pt_res->x);
  Numeric_delete(tls->trend_product_plot.pt_res->y);
  heap_free(tls->trend_product_plot.pt_res);
  Numeric_delete(tls->trend_product_plot.cd_res->o);
  Numeric_delete(tls->trend_product_plot.cd_res->h);
  Numeric_delete(tls->trend_product_plot.cd_res->l);
  Numeric_delete(tls->trend_product_plot.cd_res->c);
  Numeric_delete(tls->trend_product_plot.cd_res->onanos);
  Numeric_delete(tls->trend_product_plot.cd_res->hnanos);
  Numeric_delete(tls->trend_product_plot.cd_res->lnanos);
  Numeric_delete(tls->trend_product_plot.cd_res->cnanos);
  heap_free(tls->trend_product_plot.cd_res);
  Numeric_delete(tls->trend_configure.st_res->cd_lnanos);
  Numeric_delete(tls->trend_configure.st_res->cd_langle);
  heap_free(tls->trend_configure.st_res);
  Numeric_delete(tls->trend_position_done.st_res->cd_lnanos);
  Numeric_delete(tls->trend_position_done.st_res->cd_langle);
  heap_free(tls->trend_position_done.st_res);
  heap_free(tls);
  tls_set(trend_tls_key, NULL);
}

static void trend_init(void);
static void trend_terminate(void);
static void trend_configure(const char *restrict const,
                            const struct Exchange *restrict const,
                            struct Trade *restrict const);
static void trend_deconfigure(struct Trade *restrict const);
static struct Position *trend_position_select(
    const char *restrict const, const struct Exchange *restrict const,
    struct Trade *restrict const, const struct Array *restrict const,
    const struct Sample *restrict const);
static bool trend_position_close(const char *restrict const,
                                 const struct Exchange *restrict const,
                                 const struct Trade *restrict const,
                                 const struct Position *restrict const);
static void trend_position_done(const char *restrict const,
                                const struct Exchange *restrict const,
                                const struct Trade *restrict const,
                                const struct Position *restrict const);
static bool trend_product_plot(const char *restrict const,
                               const char *restrict const,
                               const struct Exchange *restrict const,
                               const struct Product *restrict const);

struct Algorithm algorithm_trend = {
    .nm = NULL,
    .init = trend_init,
    .terminate = trend_terminate,
    .configure = trend_configure,
    .deconfigure = trend_deconfigure,
    .position_select = trend_position_select,
    .position_close = trend_position_close,
    .position_done = trend_position_done,
    .product_plot = trend_product_plot,
};

static enum candle_trend candle_trend_db(const char *const db) {
  for (size_t i = nitems(candle_trend_map); i > 0; i--)
    if (!strcmp(candle_trend_map[i - 1].db, db))
      return candle_trend_map[i - 1].trend;

  werr("%s: %d: %s: %s: Unsupported candle trend\n", __FILE__, __LINE__,
       __func__, db);
  fatal();
}

static char *db_candle_trend(const enum candle_trend trend) {
  for (size_t i = nitems(candle_trend_map); i > 0; i--)
    if (candle_trend_map[i - 1].trend == trend)
      return candle_trend_map[i - 1].db;

  werr("%s: %d: %s: %u: Unsupported candle trend\n", __FILE__, __LINE__,
       __func__, trend);
  fatal();
}

static void trend_init(void) {
  algorithm_trend.id = String_new(TREND_UUID);
  algorithm_trend.nm = String_new("trend");
  tls_create(&trend_tls_key, trend_tls_dtor);
}

static void trend_terminate(void) {
  String_delete(algorithm_trend.id);
  String_delete(algorithm_trend.nm);
  tls_delete(trend_tls_key);
}

static void trend_configure(const char *restrict const dbcon,
                            const struct Exchange *restrict const e,
                            struct Trade *restrict const t) {
  if (t->a_st == NULL) {
    const struct trend_tls *restrict const tls = trend_tls();
    struct db_trend_state_res *restrict const st_res =
        tls->trend_configure.st_res;

    char ltrend[DATABASE_CANDLE_VALUE_MAX_LENGTH] = {0};
    st_res->cd_ltrend = ltrend;

    db_trend_state(st_res, dbcon, String_chars(e->id), String_chars(t->p_id));

    struct trend_state *restrict const st =
        heap_malloc(sizeof(struct trend_state));

    st->cd_lnanos = Numeric_new();
    st->cd_langle = Numeric_new();
    Numeric_copy_to(st_res->cd_lnanos, st->cd_lnanos);
    Numeric_copy_to(st_res->cd_langle, st->cd_langle);
    st->cd_ltrend = candle_trend_db(st_res->cd_ltrend);
    t->a_st = st;
  }
}

static void trend_deconfigure(struct Trade *restrict const t) {
  struct trend_state *restrict const st = t->a_st;
  Numeric_delete(st->cd_lnanos);
  Numeric_delete(st->cd_langle);
  heap_free(st);
  t->a_st = NULL;
}

static struct Position *trend_position_select(
    const char *restrict const dbcon, const struct Exchange *restrict const e,
    struct Trade *restrict const t, const struct Array *restrict const samples,
    const struct Sample *restrict const sample) {
  const struct trend_tls *restrict const tls = trend_tls();
  struct Numeric *restrict const r0 = tls->trend_position_select.r0;
  struct Numeric *restrict const r1 = tls->trend_position_select.r1;
  struct Numeric *restrict const fee_pc = tls->trend_position_select.fee_pc;
  struct Numeric *restrict const cd_pc = tls->trend_position_select.cd_pc;
  struct Numeric *restrict const cd_n_pc = tls->trend_position_select.cd_n_pc;
  struct Numeric *restrict const d_pc = tls->trend_position_select.d_pc;
  struct Numeric *restrict const s_pc = tls->trend_position_select.s_pc;
  struct Numeric *restrict const pr_min = tls->trend_position_select.pr_min;
  struct Numeric *restrict const pr_max = tls->trend_position_select.pr_max;
  struct Numeric *restrict const pr_cur = tls->trend_position_select.pr_cur;
  struct Candle *restrict const cd_cur = tls->trend_position_select.cd_cur;
  struct Candle *restrict const cd_first = tls->trend_position_select.cd_first;
  struct Candle *restrict const cd_last = tls->trend_position_select.cd_last;
  struct db_plot_res *restrict const plot_res =
      tls->trend_position_select.plot_res;
  struct db_trend_state_res *restrict const st_res =
      tls->trend_position_select.st_res;
  struct db_candle_res candle_res = {0};
  struct trend_state *restrict const st = t->a_st;
  struct Position *restrict p = NULL;
  void **items;

  char pl_id[DATABASE_UUID_MAX_LENGTH] = {0};
  plot_res->id = pl_id;

  if (Numeric_cmp(t->fee_pc, fee_pc) != 0) {
    Numeric_copy_to(t->fee_pc, fee_pc);
    // tp_pc / 40 * 100 + fee_pc
    // Take profit percent is 40% of candle percent.
    Numeric_div_to(t->tp_pc, fourty, r0);
    Numeric_mul_to(r0, hundred, r1);
    Numeric_add_to(r1, t->fee_pc, cd_pc);
    Numeric_mul_to(n_one, cd_pc, cd_n_pc);
  }

  Candle_reset(cd_cur);
  Numeric_copy_to(sample->price, cd_cur->h);
  Numeric_copy_to(sample->nanos, cd_cur->hnanos);
  Numeric_copy_to(sample->price, cd_cur->l);
  Numeric_copy_to(sample->nanos, cd_cur->lnanos);
  Candle_copy_to(cd_cur, cd_first);
  Candle_copy_to(cd_cur, cd_last);
  Numeric_copy_to(sample->price, pr_cur);

  items = Array_items(samples);
  for (size_t i = Array_size(samples);
       i > 0 &&
       (cd_first->t == CANDLE_NONE
            ? Numeric_cmp(((struct Sample *)items[i - 1])->nanos,
                          st->cd_lnanos) > 0
            : true) &&
       (cd_last->t == CANDLE_NONE || cd_last->t == cd_first->t);
       i--) {
    Numeric_copy_to(((struct Sample *)items[i - 1])->price, cd_cur->o);
    Numeric_copy_to(((struct Sample *)items[i - 1])->nanos, cd_cur->onanos);

    if (Numeric_cmp(pr_cur, cd_cur->o) == 0)
      continue;

    Numeric_copy_to(cd_cur->o, pr_cur);

    // (100 / open * close) - 100
    Numeric_div_to(hundred, cd_cur->o, r0);
    Numeric_mul_to(r0, sample->price, r1);
    Numeric_sub_to(r1, hundred, cd_cur->pc);

    if (Numeric_cmp(cd_cur->o, cd_cur->h) >= 0) {
      Numeric_copy_to(cd_cur->o, cd_cur->h);
      Numeric_copy_to(cd_cur->onanos, cd_cur->hnanos);
      continue;
    }

    if (Numeric_cmp(cd_cur->o, cd_cur->l) <= 0) {
      Numeric_copy_to(cd_cur->o, cd_cur->l);
      Numeric_copy_to(cd_cur->onanos, cd_cur->lnanos);
      continue;
    }

    if (Numeric_cmp(cd_cur->pc, cd_n_pc) <= 0) {
      cd_cur->t = CANDLE_DOWN;

      Numeric_copy_to(sample->nanos, cd_cur->cnanos);
      Numeric_copy_to(sample->price, cd_cur->c);

      Candle_copy_to(cd_cur, cd_last);

      if (cd_first->t == CANDLE_NONE)
        Candle_copy_to(cd_cur, cd_first);

      Numeric_copy_to(sample->price, cd_cur->h);
      Numeric_copy_to(sample->price, cd_cur->l);
      Numeric_copy_to(sample->nanos, cd_cur->hnanos);
      Numeric_copy_to(sample->nanos, cd_cur->lnanos);
      continue;
    }

    if (Numeric_cmp(cd_cur->pc, cd_pc) >= 0) {
      cd_cur->t = CANDLE_UP;

      Numeric_copy_to(sample->nanos, cd_cur->cnanos);
      Numeric_copy_to(sample->price, cd_cur->c);

      Candle_copy_to(cd_cur, cd_last);

      if (cd_first->t == CANDLE_NONE)
        Candle_copy_to(cd_cur, cd_first);

      Numeric_copy_to(sample->price, cd_cur->h);
      Numeric_copy_to(sample->price, cd_cur->l);
      Numeric_copy_to(sample->nanos, cd_cur->hnanos);
      Numeric_copy_to(sample->nanos, cd_cur->lnanos);
      continue;
    }
  }

  if (cd_first->t == CANDLE_NONE || cd_first->t != cd_last->t)
    return NULL;

  /*
   * In order to be able to perform trigonometric functions, time and amount
   * values need to be scaled to a common base.
   */

  // duration percent = 100 / window duration * duration
  Numeric_sub_to(((struct Sample *)Array_tail(samples))->nanos,
                 ((struct Sample *)Array_head(samples))->nanos, r0);
  Numeric_div_to(hundred, r0, r1);
  Numeric_sub_to(cd_first->cnanos, cd_first->onanos, r0);
  Numeric_mul_to(r1, r0, d_pc);

  Numeric_copy_to(sample->price, pr_min);
  Numeric_copy_to(sample->price, pr_max);

  items = Array_items(samples);
  for (size_t i = Array_size(samples); i > 0; i--) {
    if (Numeric_cmp(pr_min, ((struct Sample *)items[i - 1])->price) > 0)
      Numeric_copy_to(((struct Sample *)items[i - 1])->price, pr_min);
    if (Numeric_cmp(pr_max, ((struct Sample *)items[i - 1])->price) < 0)
      Numeric_copy_to(((struct Sample *)items[i - 1])->price, pr_max);
  }

  // spread percent = 100 / window spread * spread
  Numeric_sub_to(pr_max, pr_min, r0);
  Numeric_div_to(hundred, r0, r1);
  Numeric_sub_to(cd_first->c, cd_first->o, r0);
  Numeric_abs(r0);
  Numeric_mul_to(r1, r0, s_pc);

  Numeric_div_to(s_pc, d_pc, r0);
  Numeric_atan_to(r0, r1);
  // r1: angle

  if (Numeric_cmp(st->cd_langle, st->cd_ltrend != cd_first->t ? zero : r1) > 0)
    return NULL;

  Numeric_copy_to(r1, st->cd_langle);
  Numeric_copy_to(st->cd_langle, cd_first->a);

  if (cnf->plts_dir) {
    Numeric_copy_to(cd_last->onanos, plot_res->snanos);
    Numeric_copy_to(cd_first->cnanos, plot_res->enanos);

    db_tx_begin(dbcon);
    db_tx_trend_plot(plot_res, dbcon, String_chars(e->id),
                     String_chars(t->p_id));

    items = Array_items(samples);
    for (size_t i = Array_size(samples);
         i > 0 && Numeric_cmp(((struct Sample *)items[i - 1])->nanos,
                              plot_res->enanos) > 0;
         i--) {
      db_tx_plot_datapoint(dbcon, plot_res->id,
                           ((struct Sample *)items[i - 1])->nanos,
                           ((struct Sample *)items[i - 1])->price);
    }

    db_tx_plot_enanos(dbcon, plot_res->id, sample->nanos);

    candle_res.o = cd_first->o;
    candle_res.h = cd_first->h;
    candle_res.l = cd_first->l;
    candle_res.c = cd_first->c;
    candle_res.onanos = cd_first->onanos;
    candle_res.hnanos = cd_first->hnanos;
    candle_res.lnanos = cd_first->lnanos;
    candle_res.cnanos = cd_first->cnanos;

    db_tx_trend_plot_candle(dbcon, String_chars(e->id), String_chars(t->p_id),
                            &candle_res);

    db_tx_trend_plot_marker(dbcon, String_chars(e->id), String_chars(t->p_id),
                            cd_first->hnanos, cd_first->h);

    db_tx_trend_plot_marker(dbcon, String_chars(e->id), String_chars(t->p_id),
                            cd_first->lnanos, cd_first->l);

    db_tx_commit(dbcon);
  }

  Candle_copy_to(cd_first, &t->bet_cd);

  switch (t->bet_cd.t) {
  case CANDLE_UP:
    // Trending up. Buy at candle high and expect the price to go up further.
    p = &t->p_long;
    Numeric_copy_to(t->bet_cd.h, p->price);
    break;
  case CANDLE_DOWN:
    // Trending down. Sell at candle low and expect the price to go down
    // further.
    p = &t->p_short;
    Numeric_copy_to(t->bet_cd.l, p->price);
    break;
  default:
    werr("%s: %d: %s: Candle neiter up nor down\n", __FILE__, __LINE__,
         __func__);
    fatal();
  }

  Numeric_copy_to(sample->nanos, st->cd_lnanos);
  Numeric_copy_to(st->cd_lnanos, st_res->cd_lnanos);
  Numeric_copy_to(st->cd_langle, st_res->cd_langle);
  st_res->cd_ltrend = db_candle_trend(t->bet_cd.t);

  db_trend_state_update(dbcon, String_chars(e->id), String_chars(t->p_id),
                        st_res);

  return p;
}

static bool trend_position_close(const char *restrict const dbcon,
                                 const struct Exchange *restrict const e,
                                 const struct Trade *restrict const t,
                                 const struct Position *restrict const p) {
  struct trend_state *restrict const st = t->a_st;
  bool close = false;

  switch (p->side) {
  case POSITION_SIDE_BUY:
    if (st->cd_ltrend != CANDLE_UP) {
      close = true;

      if (verbose && !p->tl_trg.set) {
        wout("%s: %s->%s: %s: Trending against buy position\n",
             String_chars(e->nm), String_chars(t->q_id), String_chars(t->b_id),
             String_chars(t->id));
      }
    }
    break;
  case POSITION_SIDE_SELL:
    if (st->cd_ltrend != CANDLE_DOWN) {
      close = true;

      if (verbose && !p->tl_trg.set) {
        wout("%s: %s->%s: %s: Trending against sell position\n",
             String_chars(e->nm), String_chars(t->q_id), String_chars(t->b_id),
             String_chars(t->id));
      }
    }
    break;
  default:
    werr("%s: %d: %s: Position neither buy nor sell\n", __FILE__, __LINE__,
         __func__);
    fatal();
  }

  return close;
}

static void trend_position_done(const char *restrict const dbcon,
                                const struct Exchange *restrict const e,
                                const struct Trade *restrict const t,
                                const struct Position *restrict const p) {
  const struct trend_tls *restrict const tls = trend_tls();
  struct db_trend_state_res *restrict const st_res =
      tls->trend_position_done.st_res;
  struct trend_state *restrict const st = t->a_st;

  Numeric_copy_to(zero, st->cd_langle);
  nanos_now(st->cd_lnanos);

  Numeric_copy_to(st->cd_lnanos, st_res->cd_lnanos);
  Numeric_copy_to(st->cd_langle, st_res->cd_langle);
  st_res->cd_ltrend = db_candle_trend(st->cd_ltrend);

  db_trend_state_update(dbcon, String_chars(e->id), String_chars(t->p_id),
                        st_res);
}

static bool trend_product_plot(const char *restrict const fn,
                               const char *restrict const dbcon,
                               const struct Exchange *restrict const e,
                               const struct Product *restrict const p) {
  const struct trend_tls *restrict const tls = trend_tls();
  struct db_datapoint_res *restrict const pt_res =
      tls->trend_product_plot.pt_res;
  struct db_candle_res *restrict const cd_res = tls->trend_product_plot.cd_res;
  size_t cd_red_cnt = 0, cd_green_cnt = 0, mk_cnt = 0;
  FILE *restrict const f = fopen(fn, "w");

  if (f == NULL) {
    werr("%s: %d: %s: %s: %s\n", __FILE__, __LINE__, __func__, fn,
         strerror(errno));
    return false;
  }

  db_tx_begin(dbcon);

  fprintf(f, "samples = [\n");
  db_tx_trend_plot_samples_open(dbcon, String_chars(e->id),
                                String_chars(p->id));
  while (!terminated && db_tx_trend_plot_samples_next(pt_res, dbcon)) {
    char *restrict const x = Numeric_to_char(pt_res->x, 0);
    char *restrict const y = Numeric_to_char(pt_res->y, p->q_sc);
    fprintf(f, "\t%s, %s;\n", x, y);
    Numeric_char_free(x);
    Numeric_char_free(y);
  }
  db_tx_trend_plot_samples_close(dbcon);
  fprintf(f, "];\n");

  db_tx_trend_plot_candles_open(dbcon, String_chars(e->id),
                                String_chars(p->id));
  while (!terminated && db_tx_trend_plot_candles_next(cd_res, dbcon)) {
    char *restrict const onanos = Numeric_to_char(cd_res->onanos, 0);
    char *restrict const o = Numeric_to_char(cd_res->o, p->q_sc);
    char *restrict const hnanos = Numeric_to_char(cd_res->hnanos, 0);
    char *restrict const h = Numeric_to_char(cd_res->h, p->q_sc);
    char *restrict const lnanos = Numeric_to_char(cd_res->lnanos, 0);
    char *restrict const l = Numeric_to_char(cd_res->l, p->q_sc);
    char *restrict const cnanos = Numeric_to_char(cd_res->cnanos, 0);
    char *restrict const c = Numeric_to_char(cd_res->c, p->q_sc);
    const bool red = Numeric_cmp(cd_res->o, cd_res->c) > 0;

    fprintf(f, "%scandle%zu = [\n", red ? "red_" : "green_",
            red ? cd_red_cnt++ : cd_green_cnt++);

    fprintf(f, "\t%s, %s;\n\t%s, %s;\n];\n", onanos, o, cnanos, c);

    Numeric_char_free(o);
    Numeric_char_free(h);
    Numeric_char_free(l);
    Numeric_char_free(c);
    Numeric_char_free(onanos);
    Numeric_char_free(hnanos);
    Numeric_char_free(lnanos);
    Numeric_char_free(cnanos);
  }
  db_tx_trend_plot_candles_close(dbcon);

  db_tx_trend_plot_markers_open(dbcon, String_chars(e->id),
                                String_chars(p->id));
  while (!terminated && db_tx_trend_plot_markers_next(pt_res, dbcon)) {
    fprintf(f, "marker%zu = [", mk_cnt);
    char *restrict const x = Numeric_to_char(pt_res->x, 0);
    char *restrict const y = Numeric_to_char(pt_res->y, p->q_sc);

    fprintf(f, "\t%s, %s;\t];\n", x, y);
    mk_cnt++;
    Numeric_char_free(x);
    Numeric_char_free(y);
  }
  db_tx_trend_plot_markers_close(dbcon);

  db_tx_commit(dbcon);

  fprintf(f, "plot(\n");
  fprintf(f, "\tsamples(:,1), samples(:,2), \"-k;%s;\"", String_chars(p->nm));

  for (size_t i = cd_red_cnt; i > 0; i--)
    fprintf(f, ",\n\tred_candle%zu(:,1), red_candle%zu(:,2), \"-r\"", i - 1,
            i - 1);

  for (size_t i = cd_green_cnt; i > 0; i--)
    fprintf(f, ",\n\tgreen_candle%zu(:,1), green_candle%zu(:,2), \"-g\"", i - 1,
            i - 1);

  for (size_t i = mk_cnt; i > 0; i--)
    fprintf(f, ",\n\tmarker%zu(:,1), marker%zu(:,2), \"bo\", \"markersize\", 2",
            i - 1, i - 1);

  fprintf(f, "\n);\n");
  fprintf(f,
          "legend(\"off\")\ntitle(\"%s Trends\")\nxlabel(\"Time "
          "(ns)\")\nylabel(\"Price (%s)\")\n",
          String_chars(p->nm), String_chars(p->q_id));

  if (fclose(f) == EOF) {
    werr("%s: %d: %s: %s: %s\n", __FILE__, __LINE__, __func__, fn,
         strerror(errno));
    return false;
  }

  return true;
}
