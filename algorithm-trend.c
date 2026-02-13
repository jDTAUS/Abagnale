/* $SchulteIT: algorithm-trend.c 15268 2025-11-04 21:48:33Z schulte $ */
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

struct trend_state {
  mtx_t mtx;
  struct Numeric *restrict cd_lnanos;
  struct Numeric *restrict cd_langle;
  enum candle_trend cd_ltrend;
};

struct trend_tls {
  struct trend_state_vars {
    struct db_trend_state_rec *restrict db_st;
  } trend_state;
  struct trend_position_open_vars {
    struct Numeric *restrict r0;
    struct Numeric *restrict r1;
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
    struct db_plot_rec *restrict db_plot;
    struct db_trend_state_rec *restrict db_st;
  } trend_position_open;
  struct trend_market_plot_vars {
    struct db_datapoint_rec *restrict db_pt;
    struct db_candle_rec *restrict db_cd;
  } trend_market_plot;
};

static const struct {
  enum candle_trend trend;
  char *restrict db;
  size_t db_len;
} candle_trend_map[] = {
    {CANDLE_UP, "UP", 2},
    {CANDLE_DOWN, "DOWN", 4},
    {CANDLE_NONE, "NONE", 4},
};

extern _Atomic bool terminated;
extern const struct Numeric *restrict const zero;
extern const struct Numeric *restrict const n_one;
extern const struct Numeric *restrict const hundred;

extern const struct Config *restrict const cnf;
extern const bool verbose;
extern const size_t all_exchanges_nitems;

static tss_t trend_tls_key;
static struct Map *restrict states;

static void trend_state_delete(void *restrict const e) {
  if (e == NULL)
    return;

  struct trend_state *restrict st = e;
  Numeric_delete(st->cd_lnanos);
  Numeric_delete(st->cd_langle);
  mutex_destroy(&st->mtx);
  heap_free(e);
}

static struct trend_tls *trend_tls(void) {
  struct trend_tls *restrict tls = tls_get(trend_tls_key);
  if (tls == NULL) {
    tls = heap_malloc(sizeof(struct trend_tls));
    tls->trend_state.db_st = heap_malloc(sizeof(struct db_trend_state_rec));
    tls->trend_state.db_st->cd_lnanos = Numeric_new();
    tls->trend_state.db_st->cd_langle = Numeric_new();
    tls->trend_position_open.r0 = Numeric_from_int(0);
    tls->trend_position_open.r1 = Numeric_from_int(0);
    tls->trend_position_open.cd_pc = Numeric_from_int(0);
    tls->trend_position_open.cd_n_pc = Numeric_from_int(0);
    tls->trend_position_open.d_pc = Numeric_from_int(0);
    tls->trend_position_open.s_pc = Numeric_from_int(0);
    tls->trend_position_open.pr_min = Numeric_from_int(0);
    tls->trend_position_open.pr_max = Numeric_from_int(0);
    tls->trend_position_open.pr_cur = Numeric_new();
    tls->trend_position_open.cd_cur = Candle_new();
    tls->trend_position_open.cd_first = Candle_new();
    tls->trend_position_open.cd_last = Candle_new();
    tls->trend_position_open.db_plot = heap_malloc(sizeof(struct db_plot_rec));
    tls->trend_position_open.db_plot->snanos = Numeric_from_int(0);
    tls->trend_position_open.db_plot->enanos = Numeric_from_int(0);
    tls->trend_position_open.db_st =
        heap_malloc(sizeof(struct db_trend_state_rec));
    tls->trend_position_open.db_st->cd_lnanos = Numeric_new();
    tls->trend_position_open.db_st->cd_langle = Numeric_new();
    tls->trend_market_plot.db_pt = heap_malloc(sizeof(struct db_datapoint_rec));
    tls->trend_market_plot.db_pt->x = Numeric_new();
    tls->trend_market_plot.db_pt->y = Numeric_new();
    tls->trend_market_plot.db_cd = heap_malloc(sizeof(struct db_candle_rec));
    tls->trend_market_plot.db_cd->o = Numeric_new();
    tls->trend_market_plot.db_cd->h = Numeric_new();
    tls->trend_market_plot.db_cd->l = Numeric_new();
    tls->trend_market_plot.db_cd->c = Numeric_new();
    tls->trend_market_plot.db_cd->onanos = Numeric_new();
    tls->trend_market_plot.db_cd->hnanos = Numeric_new();
    tls->trend_market_plot.db_cd->lnanos = Numeric_new();
    tls->trend_market_plot.db_cd->cnanos = Numeric_new();
    tls_set(trend_tls_key, tls);
  }
  return tls;
}

static void trend_tls_dtor(void *e) {
  struct trend_tls *restrict const tls = e;
  Numeric_delete(tls->trend_state.db_st->cd_lnanos);
  Numeric_delete(tls->trend_state.db_st->cd_langle);
  heap_free(tls->trend_state.db_st);
  Numeric_delete(tls->trend_position_open.r0);
  Numeric_delete(tls->trend_position_open.r1);
  Numeric_delete(tls->trend_position_open.cd_pc);
  Numeric_delete(tls->trend_position_open.cd_n_pc);
  Numeric_delete(tls->trend_position_open.d_pc);
  Numeric_delete(tls->trend_position_open.s_pc);
  Numeric_delete(tls->trend_position_open.pr_min);
  Numeric_delete(tls->trend_position_open.pr_max);
  Numeric_delete(tls->trend_position_open.pr_cur);
  Candle_delete(tls->trend_position_open.cd_cur);
  Candle_delete(tls->trend_position_open.cd_first);
  Candle_delete(tls->trend_position_open.cd_last);
  Numeric_delete(tls->trend_position_open.db_plot->snanos);
  Numeric_delete(tls->trend_position_open.db_plot->enanos);
  heap_free(tls->trend_position_open.db_plot);
  Numeric_delete(tls->trend_position_open.db_st->cd_lnanos);
  Numeric_delete(tls->trend_position_open.db_st->cd_langle);
  heap_free(tls->trend_position_open.db_st);
  Numeric_delete(tls->trend_market_plot.db_pt->x);
  Numeric_delete(tls->trend_market_plot.db_pt->y);
  heap_free(tls->trend_market_plot.db_pt);
  Numeric_delete(tls->trend_market_plot.db_cd->o);
  Numeric_delete(tls->trend_market_plot.db_cd->h);
  Numeric_delete(tls->trend_market_plot.db_cd->l);
  Numeric_delete(tls->trend_market_plot.db_cd->c);
  Numeric_delete(tls->trend_market_plot.db_cd->onanos);
  Numeric_delete(tls->trend_market_plot.db_cd->hnanos);
  Numeric_delete(tls->trend_market_plot.db_cd->lnanos);
  Numeric_delete(tls->trend_market_plot.db_cd->cnanos);
  heap_free(tls->trend_market_plot.db_cd);
  heap_free(tls);
  tls_set(trend_tls_key, NULL);
}

static void trend_init(void);
static void trend_destroy(void);
static struct Position *trend_position_open(
    const void *restrict const, const struct Exchange *restrict const,
    const struct Market *restrict const, struct Trade *restrict const,
    const struct Array *restrict const, const struct Sample *restrict const);
static bool trend_position_close(const void *restrict const,
                                 const struct Exchange *restrict const,
                                 const struct Market *restrict const,
                                 const struct Trade *restrict const,
                                 const struct Position *restrict const);
static bool trend_market_plot(const void *restrict const,
                              const struct Exchange *restrict const,
                              const struct Market *restrict const,
                              const char *restrict const);

struct Algorithm algorithm_trend = {
    .nm = NULL,
    .init = trend_init,
    .destroy = trend_destroy,
    .position_open = trend_position_open,
    .position_close = trend_position_close,
    .market_plot = trend_market_plot,
};

static enum candle_trend candle_trend_db(const char *const db) {
  for (size_t i = nitems(candle_trend_map); i > 0; i--)
    if (!strcmp(candle_trend_map[i - 1].db, db))
      return candle_trend_map[i - 1].trend;

  werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, db);
  fatal();
}

static void db_candle_trend(char *restrict const db_trend,
                            const enum candle_trend trend) {
  for (size_t i = nitems(candle_trend_map); i > 0; i--)
    if (candle_trend_map[i - 1].trend == trend) {
      memcpy(db_trend, candle_trend_map[i - 1].db,
             candle_trend_map[i - 1].db_len);
      db_trend[candle_trend_map[i - 1].db_len] = '\0';
      return;
    }

  werr("%s: %d: %s: %u\n", __FILE__, __LINE__, __func__, trend);
  fatal();
}

static void trend_init(void) {
  algorithm_trend.id = String_cnew(TREND_UUID);
  algorithm_trend.nm = String_cnew("trend");
  tls_create(&trend_tls_key, trend_tls_dtor);
  states = Map_new(all_exchanges_nitems * 2048);
}

static void trend_destroy(void) {
  String_delete(algorithm_trend.id);
  String_delete(algorithm_trend.nm);
  tls_delete(trend_tls_key);
  Map_delete(states, trend_state_delete);
}

static struct trend_state *trend_state(const void *restrict const db,
                                       struct String *restrict const e_id,
                                       struct String *restrict const m_id) {
  const struct trend_tls *restrict const tls = trend_tls();
  struct trend_state *restrict st = NULL;
  struct db_trend_state_rec *restrict const db_st = tls->trend_state.db_st;

  Map_lock(states);

  st = Map_get(states, m_id);

  if (st == NULL) {
    db_trend_state(db_st, db, String_chars(e_id), String_chars(m_id));

    st = heap_malloc(sizeof(struct trend_state));
    st->cd_lnanos = Numeric_copy(db_st->cd_lnanos);
    st->cd_langle = Numeric_copy(db_st->cd_langle);
    st->cd_ltrend = candle_trend_db(db_st->cd_ltrend);
    mutex_init(&st->mtx);
    Map_put(states, m_id, st);
  }

  Map_unlock(states);
  mutex_lock(&st->mtx);
  return st;
}

static struct Position *trend_position_open(
    const void *restrict const db, const struct Exchange *restrict const e,
    const struct Market *restrict const m, struct Trade *restrict const t,
    const struct Array *restrict const samples,
    const struct Sample *restrict const sample) {
  const struct trend_tls *restrict const tls = trend_tls();
  struct Numeric *restrict const r0 = tls->trend_position_open.r0;
  struct Numeric *restrict const r1 = tls->trend_position_open.r1;
  struct Numeric *restrict const cd_pc = tls->trend_position_open.cd_pc;
  struct Numeric *restrict const cd_n_pc = tls->trend_position_open.cd_n_pc;
  struct Numeric *restrict const d_pc = tls->trend_position_open.d_pc;
  struct Numeric *restrict const s_pc = tls->trend_position_open.s_pc;
  struct Numeric *restrict const pr_min = tls->trend_position_open.pr_min;
  struct Numeric *restrict const pr_max = tls->trend_position_open.pr_max;
  struct Numeric *restrict const pr_cur = tls->trend_position_open.pr_cur;
  struct Candle *restrict const cd_cur = tls->trend_position_open.cd_cur;
  struct Candle *restrict const cd_first = tls->trend_position_open.cd_first;
  struct Candle *restrict const cd_last = tls->trend_position_open.cd_last;
  struct db_plot_rec *restrict const db_plot = tls->trend_position_open.db_plot;
  struct db_trend_state_rec *restrict const db_st =
      tls->trend_position_open.db_st;
  struct db_candle_rec db_candle = {0};
  struct trend_state *restrict const st = trend_state(db, e->id, m->id);
  struct Position *restrict p = NULL;
  void **items;

  Numeric_copy_to(t->tp_pc, cd_pc);
  Numeric_mul_to(t->tp_pc, n_one, r0);
  Numeric_copy_to(r0, cd_n_pc);

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

  if (cd_first->t == CANDLE_NONE || cd_first->t != cd_last->t) {
    mutex_unlock(&st->mtx);
    return NULL;
  }

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

  if (st->cd_ltrend == cd_first->t &&
      Numeric_cmp(((struct Sample *)Array_head(samples))->nanos,
                  st->cd_lnanos) <= 0 &&
      Numeric_cmp(st->cd_langle, r1) > 0) {
    mutex_unlock(&st->mtx);
    return NULL;
  }

  Numeric_copy_to(r1, st->cd_langle);
  Numeric_copy_to(st->cd_langle, cd_first->a);

  if (cnf->plts_dir) {
    Numeric_copy_to(cd_last->onanos, db_plot->snanos);
    Numeric_copy_to(cd_first->cnanos, db_plot->enanos);

    db_tx_begin(db);
    db_tx_trend_plot(db_plot, db, String_chars(e->id), String_chars(m->id));

    items = Array_items(samples);
    for (size_t i = Array_size(samples);
         i > 0 && Numeric_cmp(((struct Sample *)items[i - 1])->nanos,
                              db_plot->enanos) > 0;
         i--) {
      db_tx_plot_datapoint(db, db_plot->id,
                           ((struct Sample *)items[i - 1])->nanos,
                           ((struct Sample *)items[i - 1])->price);
    }

    db_tx_plot_enanos(db, db_plot->id, sample->nanos);

    db_candle.o = cd_first->o;
    db_candle.h = cd_first->h;
    db_candle.l = cd_first->l;
    db_candle.c = cd_first->c;
    db_candle.onanos = cd_first->onanos;
    db_candle.hnanos = cd_first->hnanos;
    db_candle.lnanos = cd_first->lnanos;
    db_candle.cnanos = cd_first->cnanos;

    db_tx_trend_plot_candle(db, String_chars(e->id), String_chars(m->id),
                            &db_candle);

    db_tx_trend_plot_marker(db, String_chars(e->id), String_chars(m->id),
                            cd_first->hnanos, cd_first->h);

    db_tx_trend_plot_marker(db, String_chars(e->id), String_chars(m->id),
                            cd_first->lnanos, cd_first->l);

    db_tx_commit(db);
  }

  Candle_copy_to(cd_first, &t->open_cd);

  switch (t->open_cd.t) {
  case CANDLE_UP:
    // Trending up. Buy at current price expecting increasing price.
    p = &t->p_long;
    Numeric_copy_to(sample->price, p->price);
    break;
  case CANDLE_DOWN:
    // Trending down. Sell at current price expecting decreasing price.
    p = &t->p_short;
    Numeric_copy_to(sample->price, p->price);
    break;
  default:
    werr("%s: %d: %s\n", __FILE__, __LINE__, __func__);
    fatal();
  }

  st->cd_ltrend = t->open_cd.t;
  Numeric_copy_to(sample->nanos, st->cd_lnanos);

  Numeric_copy_to(st->cd_lnanos, db_st->cd_lnanos);
  Numeric_copy_to(st->cd_langle, db_st->cd_langle);
  db_candle_trend(db_st->cd_ltrend, st->cd_ltrend);
  db_trend_state_update(db, String_chars(e->id), String_chars(m->id), db_st);

  mutex_unlock(&st->mtx);
  return p;
}

static bool trend_position_close(const void *restrict const db,
                                 const struct Exchange *restrict const e,
                                 const struct Market *restrict const m,
                                 const struct Trade *restrict const t,
                                 const struct Position *restrict const p) {
  struct trend_state *restrict const st = trend_state(db, e->id, m->id);
  bool close = false;

  switch (p->type) {
  case POSITION_TYPE_LONG:
    close = st->cd_ltrend != CANDLE_UP;
    break;
  case POSITION_TYPE_SHORT:
    close = st->cd_ltrend != CANDLE_DOWN;
    break;
  default:
    werr("%s: %d: %s\n", __FILE__, __LINE__, __func__);
    fatal();
  }

  if (close && verbose && !p->tl_trg.set) {
    wout("%s: %s: %s: Trend not confirmed\n", String_chars(e->nm),
         String_chars(m->nm), String_chars(t->id));
  }

  mutex_unlock(&st->mtx);
  return close;
}

static bool trend_market_plot(const void *restrict const db,
                              const struct Exchange *restrict const e,
                              const struct Market *restrict const m,
                              const char *restrict const fn) {
  const struct trend_tls *restrict const tls = trend_tls();
  struct db_datapoint_rec *restrict const db_pt = tls->trend_market_plot.db_pt;
  struct db_candle_rec *restrict const db_cd = tls->trend_market_plot.db_cd;
  size_t cd_red_cnt = 0, cd_green_cnt = 0, mk_cnt = 0;
  FILE *restrict const f = fopen(fn, "w");

  if (f == NULL) {
    werr("%s: %d: %s: %s: %s\n", __FILE__, __LINE__, __func__, fn,
         strerror(errno));
    return false;
  }

  db_tx_begin(db);

  fprintf(f, "samples = [\n");
  db_tx_trend_plot_samples_open(db, String_chars(e->id), String_chars(m->id));
  while (!terminated && db_tx_trend_plot_samples_next(db_pt, db)) {
    char *restrict const x = Numeric_to_char(db_pt->x, 0);
    char *restrict const y = Numeric_to_char(db_pt->y, m->q_sc);
    fprintf(f, "\t%s, %s;\n", x, y);
    Numeric_char_free(x);
    Numeric_char_free(y);
  }
  db_tx_trend_plot_samples_close(db);
  fprintf(f, "];\n");

  db_tx_trend_plot_candles_open(db, String_chars(e->id), String_chars(m->id));
  while (!terminated && db_tx_trend_plot_candles_next(db_cd, db)) {
    char *restrict const onanos = Numeric_to_char(db_cd->onanos, 0);
    char *restrict const o = Numeric_to_char(db_cd->o, m->q_sc);
    char *restrict const hnanos = Numeric_to_char(db_cd->hnanos, 0);
    char *restrict const h = Numeric_to_char(db_cd->h, m->q_sc);
    char *restrict const lnanos = Numeric_to_char(db_cd->lnanos, 0);
    char *restrict const l = Numeric_to_char(db_cd->l, m->q_sc);
    char *restrict const cnanos = Numeric_to_char(db_cd->cnanos, 0);
    char *restrict const c = Numeric_to_char(db_cd->c, m->q_sc);
    const bool red = Numeric_cmp(db_cd->o, db_cd->c) > 0;

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
  db_tx_trend_plot_candles_close(db);

  db_tx_trend_plot_markers_open(db, String_chars(e->id), String_chars(m->id));
  while (!terminated && db_tx_trend_plot_markers_next(db_pt, db)) {
    fprintf(f, "marker%zu = [", mk_cnt);
    char *restrict const x = Numeric_to_char(db_pt->x, 0);
    char *restrict const y = Numeric_to_char(db_pt->y, m->q_sc);

    fprintf(f, "\t%s, %s;\t];\n", x, y);
    mk_cnt++;
    Numeric_char_free(x);
    Numeric_char_free(y);
  }
  db_tx_trend_plot_markers_close(db);

  db_tx_commit(db);

  fprintf(f, "plot(\n");
  fprintf(f, "\tsamples(:,1), samples(:,2), \"-k;%s;\"", String_chars(m->nm));

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
          String_chars(m->nm), String_chars(m->q_id));

  if (fclose(f) == EOF) {
    werr("%s: %d: %s: %s: %s\n", __FILE__, __LINE__, __func__, fn,
         strerror(errno));
    return false;
  }

  return true;
}
