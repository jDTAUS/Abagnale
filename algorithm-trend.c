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

#ifdef HAVE_HOST_H
#include "host.h"
#endif

#include "abagnale.h"
#include "config.h"
#include "database.h"
#include "heap.h"
#include "proc.h"
#include "queue.h"
#include "thread.h"
#include "time.h"

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#ifndef nitems
#define nitems(a) (sizeof((a)) / sizeof((a)[0]))
#endif

#define TREND_UUID "bfd87009-ea0f-4664-a03a-f9b6e91274dd"

struct trend_state {
  mtx_t mtx;
  struct Numeric *restrict cd_lnanos;
  struct Numeric *restrict cd_langle;
  enum candle_trend cd_ltrend;
};

struct market_plot_ctx {
  void *restrict db;
  const struct Exchange *restrict e;
  struct Market *restrict m;
  struct Queue *restrict market_queue;
  struct thread_group *restrict threads;
  char db_name[DATABASE_CONNECTION_NAME_MAX_LENGTH + 1];
  bool running;
};

struct market_plot_arg {
  struct String *restrict e_id;
  struct String *restrict m_id;
  struct Numeric *restrict s_ns;
  struct Numeric *restrict s_pr;
  struct Numeric *restrict e_ns;
  struct Numeric *restrict e_pr;
  struct Candle *restrict cd;
  struct Array *restrict dp;
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
    struct db_trend_state_rec *restrict db_st;
  } trend_position_open;
  struct trend_market_plot_vars {
    struct db_datapoint_rec *restrict db_pt;
    struct db_marker_rec *restrict db_mk;
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

extern volatile sig_atomic_t terminated;
extern const struct Numeric *restrict const zero;
extern const struct Numeric *restrict const n_one;
extern const struct Numeric *restrict const hundred;

extern const struct Config *restrict const cnf;
extern const bool verbose;
extern const struct timespec thread_timeout;
extern const size_t all_exchanges_nitems;

static tss_t trend_tls_key;
static struct Map *restrict states;
static struct Queue *restrict plot_queue;
static _Atomic bool plot_queue_dequeueing;
static struct thread_group *restrict threads;

static void trend_state_delete(void *restrict const e) {
  if (e == NULL)
    return;

  struct trend_state *restrict st = e;
  Numeric_delete(st->cd_lnanos);
  Numeric_delete(st->cd_langle);
  mutex_destroy(&st->mtx);
  heap_free(e);
}

static void db_datapoint_rec_delete(void *restrict const e) {
  if (e == NULL)
    return;

  struct db_datapoint_rec *restrict rec = e;
  Numeric_delete(rec->x);
  Numeric_delete(rec->y);
  heap_free(rec);
}

static void market_plot_arg_delete(void *restrict const e) {
  if (e == NULL)
    return;

  struct market_plot_arg *restrict arg = e;
  String_delete(arg->e_id);
  String_delete(arg->m_id);
  Numeric_delete(arg->s_ns);
  Numeric_delete(arg->s_pr);
  Numeric_delete(arg->e_ns);
  Numeric_delete(arg->e_pr);
  Candle_delete(arg->cd);
  Array_delete(arg->dp, db_datapoint_rec_delete);
  heap_free(arg);
}

static struct trend_tls *const trend_tls(void) {
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
    tls->trend_position_open.db_st =
        heap_malloc(sizeof(struct db_trend_state_rec));
    tls->trend_position_open.db_st->cd_lnanos = Numeric_new();
    tls->trend_position_open.db_st->cd_langle = Numeric_new();
    tls->trend_market_plot.db_pt = heap_malloc(sizeof(struct db_datapoint_rec));
    tls->trend_market_plot.db_pt->x = Numeric_new();
    tls->trend_market_plot.db_pt->y = Numeric_new();
    tls->trend_market_plot.db_mk = heap_malloc(sizeof(struct db_marker_rec));
    tls->trend_market_plot.db_mk->dp.x = Numeric_new();
    tls->trend_market_plot.db_mk->dp.y = Numeric_new();
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
  Numeric_delete(tls->trend_position_open.db_st->cd_lnanos);
  Numeric_delete(tls->trend_position_open.db_st->cd_langle);
  heap_free(tls->trend_position_open.db_st);
  Numeric_delete(tls->trend_market_plot.db_pt->x);
  Numeric_delete(tls->trend_market_plot.db_pt->y);
  heap_free(tls->trend_market_plot.db_pt);
  Numeric_delete(tls->trend_market_plot.db_mk->dp.x);
  Numeric_delete(tls->trend_market_plot.db_mk->dp.y);
  heap_free(tls->trend_market_plot.db_mk);
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
  for (size_t i = nitems(candle_trend_map); i-- > 0;)
    if (!strcmp(candle_trend_map[i].db, db))
      return candle_trend_map[i].trend;
  panic();
}

static void db_candle_trend(char *restrict const db_trend,
                            const enum candle_trend trend) {
  for (size_t i = nitems(candle_trend_map); i-- > 0;)
    if (candle_trend_map[i].trend == trend) {
      memcpy(db_trend, candle_trend_map[i].db, candle_trend_map[i].db_len);
      db_trend[candle_trend_map[i].db_len] = '\0';
      return;
    }
  panic();
}

static void trend_init(void) {
  algorithm_trend.id = String_cnew(TREND_UUID);
  algorithm_trend.nm = String_cnew("trend");
  tls_create(&trend_tls_key, trend_tls_dtor);
  states = Map_new(StringMapOps, all_exchanges_nitems * 2048);
  plot_queue = Queue_new(MARKETS_QUEUE_CAPACITY, &thread_timeout);
  plot_queue_dequeueing = false;
  threads = thread_group_new();
}

static void trend_destroy(void) {
  Queue_stop(plot_queue);
  thread_group_join(threads);
  thread_group_delete(threads);

  String_delete(algorithm_trend.id);
  String_delete(algorithm_trend.nm);
  tls_delete(trend_tls_key);
  Map_delete(states, trend_state_delete);
  Queue_delete(plot_queue, market_plot_arg_delete);
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

static int market_plot_func(void *restrict const a) {
  struct market_plot_ctx *restrict const p_ctx = a;
  void *const *restrict items;

  struct db_candle_rec db_candle = {0};
  struct db_plot_rec db_plot = {0};
  char plot_fn[BUFSIZ] = {0};

  p_ctx->db = db_connect(p_ctx->db_name);

  db_plot.snanos = Numeric_new();
  db_plot.enanos = Numeric_new();

  do {
    Queue_lock(p_ctx->market_queue);
    p_ctx->running = true;

    struct market_plot_arg *restrict const arg =
        Queue_dequeue_await(p_ctx->market_queue);

    if (arg == NULL) {
      if (Queue_dequeue_timedout(p_ctx->market_queue) &&
          Queue_size(p_ctx->market_queue) > 0) {
        // plot_func may have enqueued during await
        Queue_unlock(p_ctx->market_queue);
        continue;
      }

      p_ctx->running = false;
      Queue_stop(p_ctx->market_queue);
      break;
    }

    Queue_unlock(p_ctx->market_queue);

    Numeric_copy_to(arg->s_ns, db_plot.snanos);
    Numeric_copy_to(arg->e_ns, db_plot.enanos);

    db_tx_begin(p_ctx->db);
    db_tx_trend_plot(&db_plot, p_ctx->db, String_chars(p_ctx->e->id),
                     String_chars(p_ctx->m->id));

    items = Array_items(arg->dp);
    for (size_t i = Array_size(arg->dp);
         i-- > 0 && Numeric_cmp(((struct db_datapoint_rec *)items[i])->x,
                                db_plot.enanos) > 0;) {
      db_tx_plot_datapoint(p_ctx->db, db_plot.id,
                           ((struct db_datapoint_rec *)items[i])->x,
                           ((struct db_datapoint_rec *)items[i])->y);
    }

    db_candle.o = arg->cd->o;
    db_candle.h = arg->cd->h;
    db_candle.l = arg->cd->l;
    db_candle.c = arg->cd->c;
    db_candle.onanos = arg->cd->onanos;
    db_candle.hnanos = arg->cd->hnanos;
    db_candle.lnanos = arg->cd->lnanos;
    db_candle.cnanos = arg->cd->cnanos;

    db_tx_trend_plot_candle(p_ctx->db, String_chars(p_ctx->e->id),
                            String_chars(p_ctx->m->id), &db_candle);

    db_tx_trend_plot_marker(p_ctx->db, String_chars(p_ctx->e->id),
                            String_chars(p_ctx->m->id), arg->cd->hnanos,
                            arg->cd->h, "UP");

    db_tx_trend_plot_marker(p_ctx->db, String_chars(p_ctx->e->id),
                            String_chars(p_ctx->m->id), arg->cd->lnanos,
                            arg->cd->l, "DOWN");

    db_tx_trend_plot_marker(p_ctx->db, String_chars(p_ctx->e->id),
                            String_chars(p_ctx->m->id), arg->s_ns, arg->s_pr,
                            "LEFT");

    db_tx_trend_plot_marker(p_ctx->db, String_chars(p_ctx->e->id),
                            String_chars(p_ctx->m->id), arg->e_ns, arg->s_pr,
                            "RIGHT");

    db_tx_commit(p_ctx->db);

    int r =
        snprintf(plot_fn, sizeof(plot_fn), "%s/%s/%s/%s.m",
                 String_chars(cnf->plts_dir), String_chars(p_ctx->e->nm),
                 String_chars(algorithm_trend.nm), String_chars(p_ctx->m->nm));

    if (r < 0 || (size_t)r >= sizeof(plot_fn))
      panic();

    trend_market_plot(p_ctx->db, p_ctx->e, p_ctx->m, plot_fn);

    if (verbose) {
      char *restrict const s_iso = nanos_to_iso8601(arg->s_ns);
      char *restrict const e_iso = nanos_to_iso8601(arg->e_ns);

      wout("%s: %s: Plot: %s->%s (%s)\n", String_chars(p_ctx->e->nm),
           String_chars(p_ctx->m->nm), s_iso, e_iso, plot_fn);

      heap_free(s_iso);
      heap_free(e_iso);
    }

    market_plot_arg_delete(arg);
  } while (!terminated);

  db_disconnect(p_ctx->db);

  thread_group_end_thread(p_ctx->threads);

  if (!p_ctx->running)
    Queue_unlock(p_ctx->market_queue);

  Numeric_delete(db_plot.snanos);
  Numeric_delete(db_plot.enanos);
  thread_exit(EXIT_SUCCESS);
}

inline static void market_plot_worker_delete(void *restrict const e) {
  if (e == NULL)
    return;

  struct market_plot_ctx *restrict p_ctx = e;
  Queue_delete(p_ctx->market_queue, market_plot_arg_delete);
  Market_delete(p_ctx->m);
  heap_free(p_ctx);
}

static int plot_func(void *restrict const a) {
  thrd_t thrd;

  struct thread_group *restrict const p_threads = thread_group_new();
  struct Map *restrict const plot_workers =
      Map_new(StringMapOps, MARKETS_MAP_CAPACITY);

  while (!terminated) {
    Queue_lock(plot_queue);

    struct market_plot_arg *restrict const arg =
        Queue_dequeue_await(plot_queue);

    if (arg == NULL) {
      if (Queue_dequeue_timedout(plot_queue) && Queue_size(plot_queue) > 0) {
        // market_plot_enqueue may have enqueued during await
        Queue_unlock(plot_queue);
        continue;
      }

      plot_queue_dequeueing = false;
      Queue_stop(plot_queue);
      break;
    }

    Queue_unlock(plot_queue);

  again:
    struct market_plot_ctx *restrict p_ctx = Map_get(plot_workers, arg->m_id);

    if (p_ctx != NULL) {
      Queue_lock(p_ctx->market_queue);

      if (!p_ctx->running) {
        Map_remove(plot_workers, arg->m_id);
        Queue_unlock(p_ctx->market_queue);
        market_plot_worker_delete(p_ctx);
        p_ctx = NULL;
      }
    }

    if (p_ctx == NULL) {
      const struct Exchange *restrict const e = exchange_id(arg->e_id);

      if (e == NULL)
        panic();

      struct Market *restrict m = e->market(arg->m_id);

      if (m == NULL) {
        werr("%s: Market: Not available: %s\n", String_chars(e->nm),
             String_chars(arg->m_id));

        market_plot_arg_delete(arg);
        continue;
      }

      p_ctx = heap_calloc(1, sizeof(struct market_plot_ctx));
      p_ctx->e = e;
      p_ctx->m = Market_copy(m);
      p_ctx->threads = p_threads;
      mutex_unlock(m->mtx);
      m = NULL;

      p_ctx->market_queue =
          Queue_new(MARKET_PLOT_QUEUE_CAPACITY, &thread_timeout);

      Queue_start(p_ctx->market_queue);

      const int r =
          snprintf(p_ctx->db_name, sizeof(p_ctx->db_name), "%s-%s-trend-plots",
                   String_chars(p_ctx->e->nm), String_chars(p_ctx->m->nm));

      if (r < 0 || (size_t)r >= sizeof(p_ctx->db_name))
        panic();

      Map_put(plot_workers, arg->m_id, p_ctx);
      Queue_lock(p_ctx->market_queue);
      p_ctx->running = true;
      thread_group_begin_thread(p_threads);
      thread_create(&thrd, market_plot_func, p_ctx);
      thread_detach(thrd);
    }

    Queue_enqueue_await(p_ctx->market_queue, arg);

    if (!p_ctx->running) {
      // market_plot_func may have stopped during await
      Queue_unlock(p_ctx->market_queue);
      goto again;
    }

    if (Queue_enqueue_timedout(p_ctx->market_queue)) {
      wout("%s: %s: Plots: Stalled: %zu/%zu %" PRIuMAX "s %" PRIuMAX "ns\n",
           String_chars(p_ctx->e->nm), String_chars(p_ctx->m->nm),
           Queue_size(p_ctx->market_queue), Queue_capacity(p_ctx->market_queue),
           (uintmax_t)thread_timeout.tv_sec, (uintmax_t)thread_timeout.tv_nsec);

      Queue_unlock(p_ctx->market_queue);
      goto again;
    }

    Queue_unlock(p_ctx->market_queue);
  }

  if (!plot_queue_dequeueing)
    Queue_unlock(plot_queue);

  struct MapIterator *restrict it = MapIterator_new(plot_workers);
  while (MapIterator_next(it))
    Queue_stop(((struct market_plot_ctx *)MapIterator_value(it))->market_queue);
  MapIterator_delete(it);

  thread_group_join(p_threads);
  thread_group_delete(p_threads);

  Map_delete(plot_workers, market_plot_worker_delete);
  thread_group_end_thread(threads);
  thread_exit(EXIT_SUCCESS);
}

static void market_plot_enqueue(const struct Exchange *restrict const e,
                                const struct Market *restrict const m,
                                struct market_plot_arg *restrict const arg) {
  thrd_t thrd;

  Queue_lock(plot_queue);

again:
  if (!terminated) {
    if (!plot_queue_dequeueing) {
      plot_queue_dequeueing = true;
      Queue_start(plot_queue);
      thread_group_begin_thread(threads);
      thread_create(&thrd, plot_func, NULL);
      thread_detach(thrd);
    }

    Queue_enqueue_await(plot_queue, arg);

    if (!plot_queue_dequeueing) {
      // plot_func may have stopped during await
      goto again;
    }

    if (Queue_enqueue_timedout(plot_queue)) {
      wout("%s: %s: Plots: Stalled: %zu/%zu %" PRIuMAX "s %" PRIuMAX "ns\n",
           String_chars(e->nm), String_chars(m->nm), Queue_size(plot_queue),
           Queue_capacity(plot_queue), (uintmax_t)thread_timeout.tv_sec,
           (uintmax_t)thread_timeout.tv_nsec);

      goto again;
    }
  }

  Queue_unlock(plot_queue);
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
  struct db_trend_state_rec *restrict const db_st =
      tls->trend_position_open.db_st;
  struct trend_state *restrict const st = trend_state(db, e->id, m->id);
  struct Position *restrict p = NULL;
  void *const *restrict items;

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
       i-- > 0 &&
       (cd_first->t == CANDLE_NONE
            ? Numeric_cmp(((struct Sample *)items[i])->nanos, st->cd_lnanos) > 0
            : true) &&
       (cd_last->t == CANDLE_NONE || cd_last->t == cd_first->t);) {
    Numeric_copy_to(((struct Sample *)items[i])->price, cd_cur->o);
    Numeric_copy_to(((struct Sample *)items[i])->nanos, cd_cur->onanos);

    if (Numeric_cmp(pr_cur, cd_cur->o) == 0)
      continue;

    Numeric_copy_to(cd_cur->o, pr_cur);

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

    // (100 / open * close) - 100
    Numeric_div_to(hundred, cd_cur->o, r0);
    Numeric_mul_to(r0, sample->price, r1);
    Numeric_sub_to(r1, hundred, cd_cur->pc);

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
  for (size_t i = Array_size(samples); i-- > 0;) {
    if (Numeric_cmp(pr_min, ((struct Sample *)items[i])->price) > 0)
      Numeric_copy_to(((struct Sample *)items[i])->price, pr_min);
    if (Numeric_cmp(pr_max, ((struct Sample *)items[i])->price) < 0)
      Numeric_copy_to(((struct Sample *)items[i])->price, pr_max);
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
    panic();
  }

  st->cd_ltrend = t->open_cd.t;
  Numeric_copy_to(sample->nanos, st->cd_lnanos);

  Numeric_copy_to(st->cd_lnanos, db_st->cd_lnanos);
  Numeric_copy_to(st->cd_langle, db_st->cd_langle);
  db_candle_trend(db_st->cd_ltrend, st->cd_ltrend);
  db_trend_state_update(db, String_chars(e->id), String_chars(m->id), db_st);

  mutex_unlock(&st->mtx);

  if (cnf->plts_dir) {
    struct market_plot_arg *restrict const plot_arg =
        heap_calloc(1, sizeof(*plot_arg));

    struct Sample *restrict const head = Array_head(samples);
    plot_arg->e_id = String_copy(e->id);
    plot_arg->m_id = String_copy(m->id);
    plot_arg->s_ns = Numeric_copy(head->nanos);
    plot_arg->s_pr = Numeric_copy(head->price);
    plot_arg->e_ns = Numeric_copy(sample->nanos);
    plot_arg->e_pr = Numeric_copy(sample->price);
    plot_arg->dp = Array_new(Array_size(samples));
    plot_arg->cd = Candle_new();
    Candle_copy_to(&t->open_cd, plot_arg->cd);

    items = Array_items(samples);
    for (size_t i = Array_size(samples); i-- > 0;) {
      struct Sample *restrict const s = items[i];
      struct db_datapoint_rec *restrict const dp_rec =
          heap_calloc(1, sizeof(*dp_rec));

      dp_rec->x = Numeric_copy(s->nanos);
      dp_rec->y = Numeric_copy(s->price);
      Array_add_tail(plot_arg->dp, dp_rec);
    }

    market_plot_enqueue(e, m, plot_arg);
  }

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
    panic();
  }

  if (close && verbose && !p->tl_trg.set) {
    wout("%s: %s: Position: Trend not confirmed: %s\n", String_chars(e->nm),
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
  struct db_marker_rec *restrict const db_mk = tls->trend_market_plot.db_mk;
  struct db_candle_rec *restrict const db_cd = tls->trend_market_plot.db_cd;
  size_t cd_red_cnt = 0, cd_green_cnt = 0, up_cnt = 0, down_cnt = 0,
         left_cnt = 0, right_cnt = 0;

  FILE *restrict const f = fopen(fn, "w");

  if (f == NULL) {
    werr("%s: %s: %s: %s\n", String_chars(e->nm), String_chars(m->nm), fn,
         strerror(errno));
    return false;
  }

  db_tx_begin(db);

  fprintf(f, "samples = [\n");
  db_tx_trend_plot_samples_open(db, String_chars(e->id), String_chars(m->id));
  while (!terminated && db_tx_trend_plot_samples_next(db_pt, db)) {
    char *restrict const x = Numeric_to_char(db_pt->x, 0);
    char *restrict const y = Numeric_to_char(db_pt->y, m->p_sc);
    fprintf(f, "\t%s, %s;\n", x, y);
    Numeric_char_free(x);
    Numeric_char_free(y);
  }
  db_tx_trend_plot_samples_close(db);
  fprintf(f, "];\n");

  db_tx_trend_plot_candles_open(db, String_chars(e->id), String_chars(m->id));
  while (!terminated && db_tx_trend_plot_candles_next(db_cd, db)) {
    char *restrict const onanos = Numeric_to_char(db_cd->onanos, 0);
    char *restrict const o = Numeric_to_char(db_cd->o, m->p_sc);
    char *restrict const hnanos = Numeric_to_char(db_cd->hnanos, 0);
    char *restrict const h = Numeric_to_char(db_cd->h, m->p_sc);
    char *restrict const lnanos = Numeric_to_char(db_cd->lnanos, 0);
    char *restrict const l = Numeric_to_char(db_cd->l, m->p_sc);
    char *restrict const cnanos = Numeric_to_char(db_cd->cnanos, 0);
    char *restrict const c = Numeric_to_char(db_cd->c, m->p_sc);
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
  while (!terminated && db_tx_trend_plot_markers_next(db_mk, db)) {
    char *restrict const x = Numeric_to_char(db_mk->dp.x, 0);
    char *restrict const y = Numeric_to_char(db_mk->dp.y, m->p_sc);

    if (!strcmp("UP", db_mk->type))
      fprintf(f, "candle%zu_high = [\t%s, %s;\t];\n", up_cnt++, x, y);
    else if (!strcmp("DOWN", db_mk->type))
      fprintf(f, "candle%zu_low = [\t%s, %s;\t];\n", down_cnt++, x, y);
    else if (!strcmp("LEFT", db_mk->type))
      fprintf(f, "window%zu_close = [\t%s, %s;\t];\n", left_cnt++, x, y);
    else if (!strcmp("RIGHT", db_mk->type))
      fprintf(f, "window%zu_open = [\t%s, %s;\t];\n", right_cnt++, x, y);
    else
      panic();

    Numeric_char_free(x);
    Numeric_char_free(y);
  }
  db_tx_trend_plot_markers_close(db);

  db_tx_commit(db);

  fprintf(f, "plot(\n");
  fprintf(f, "\tsamples(:,1), samples(:,2), \"-k;%s;\"", String_chars(m->nm));

  for (size_t i = cd_red_cnt; i-- > 0;)
    fprintf(f, ",\n\tred_candle%zu(:,1), red_candle%zu(:,2), \"-r\"", i, i);

  for (size_t i = cd_green_cnt; i-- > 0;)
    fprintf(f, ",\n\tgreen_candle%zu(:,1), green_candle%zu(:,2), \"-g\"", i, i);

  for (size_t i = up_cnt; i-- > 0;)
    fprintf(f, ",\n\tcandle%zu_high(:,1), candle%zu_high(:,2), \"b^\"", i, i);

  for (size_t i = down_cnt; i-- > 0;)
    fprintf(f, ",\n\tcandle%zu_low(:,1), candle%zu_low(:,2), \"bv\"", i, i);

  for (size_t i = left_cnt; i-- > 0;)
    fprintf(f, ",\n\twindow%zu_close(:,1), window%zu_close(:,2), \"m<\"", i, i);

  for (size_t i = right_cnt; i-- > 0;)
    fprintf(f, ",\n\twindow%zu_open(:,1), window%zu_open(:,2), \"m>\"", i, i);

  fprintf(f, "\n);\n");
  fprintf(f,
          "legend(\"off\")\ntitle(\"%s Trends\", \"interpreter\", "
          "\"none\")\nxlabel(\"Time (ns)\", \"interpreter\", "
          "\"none\")\nylabel(\"Price (%s)\", \"interpreter\", \"none\")\n",
          String_chars(m->nm), String_chars(m->q_id));

  if (fclose(f) == EOF) {
    werr("%s: %s: %s: %s\n", String_chars(e->nm), String_chars(m->nm), fn,
         strerror(errno));
    return false;
  }

  return true;
}
