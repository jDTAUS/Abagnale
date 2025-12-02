/* $SchulteIT: abagnale.c 15281 2025-11-05 06:02:51Z schulte $ */
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
#include "exchange.h"
#include "heap.h"
#include "map.h"
#include "math.h"
#include "proc.h"
#include "thread.h"
#include "time.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ABAG_WORKERS
#define ABAG_WORKERS 13
#endif

#ifndef ABAG_MAX_PRODUCTS
#define ABAG_MAX_PRODUCTS 1200
#endif

#ifndef ABAG_ORDER_RELOAD_INTERVAL_NANOS
// 15min
#define ABAG_ORDER_RELOAD_INTERVAL_NANOS 900000000000L
#endif

#ifndef ABAG_BOOT_DELAY_NANOS
// 6min
#define ABAG_BOOT_DELAY_NANOS 360000000000L
#endif

#ifndef nitems
#define nitems(_a) (sizeof((_a)) / sizeof((_a)[0]))
#endif

struct worker_ctx {
  char db[DATABASE_CONNECTION_NAME_MAX_LENGTH];
  const struct Algorithm *restrict a;
  const struct Exchange *restrict ex;
  const struct ProductConfig *restrict p_cnf;
  struct Product *restrict p;
  struct Numeric *restrict q_tgt;
};

struct abag_tls {
  struct candle_string_vars {
    struct Numeric *restrict s;
  } candle_string;
  struct samples_per_nano_vars {
    struct Numeric *restrict size;
    struct Numeric *restrict duration;
  } samples_per_nano;
  struct samples_per_second_vars {
    struct Numeric *restrict n;
  } samples_per_second;
  struct samples_per_minute_vars {
    struct Numeric *restrict s;
  } samples_per_minute;
  struct samples_load_vars {
    struct Numeric *restrict now;
    struct Numeric *restrict filter;
    struct Numeric *restrict sr;
    struct db_sample_res *restrict sample;
  } samples_load;
  struct worker_config_vars {
    struct Numeric *restrict sr;
    struct Numeric *restrict now;
    struct Numeric *restrict r0;
  } worker_config;
  struct orders_process_vars {
    struct Numeric *restrict tp;
  } orders_process;
  struct samples_process_vars {
    struct Numeric *restrict outdated_ns;
    struct Numeric *restrict tp;
    struct Numeric *restrict nanos;
  } samples_process;
  struct position_pricing_vars {
    struct Numeric *restrict r0;
    struct Numeric *restrict r1;
    struct Numeric *restrict r2;
    struct Numeric *restrict r3;
  } position_pricing;
  struct position_open_vars {
    struct Numeric *restrict csecs;
  } position_open;
  struct position_fill_vars {
    struct Numeric *restrict csecs;
    struct Numeric *restrict dsecs;
  } position_fill;
  struct position_cancel_vars {
    struct Numeric *restrict r0;
  } position_cancel;
  struct position_timeout_vars {
    struct Numeric *restrict age;
    struct Numeric *restrict stats_to;
    struct Numeric *restrict factor_to;
    struct Numeric *restrict total_to;
    struct Numeric *restrict sl_to;
    struct Numeric *restrict n;
    struct db_stats_res *restrict stats;
  } position_timeout;
  struct position_maintain_vars {
    struct Numeric *restrict m;
    struct Numeric *restrict r0;
  } position_maintain;
  struct position_trigger_vars {
    struct Numeric *restrict sr;
    struct Numeric *restrict age;
  } position_trigger;
  struct position_trade_vars {
    struct Numeric *restrict o_pr;
    struct Numeric *restrict r0;
  } position_trade;
  struct trade_create_vars {
    struct db_stats_res *restrict stats;
  } trade_create;
  struct trade_maintain_vars {
    struct Numeric *restrict q_delta;
    struct Numeric *restrict q_costs;
    struct Numeric *restrict q_profit;
  } trade_maintain;
  struct trade_pricing_vars {
    struct Numeric *restrict r0;
  } trade_pricing;
  struct trade_bet_vars {
    struct Numeric *restrict b_avail;
    struct Numeric *restrict q_avail;
    struct Numeric *restrict q_costs;
    struct Numeric *restrict r0;
    struct db_balance_res *restrict hold;
  } trade_bet;
  struct trades_load_vars {
    struct db_trade_res *restrict trade;
  } trades_load;
};

extern char *__progname;
extern _Atomic bool terminated;

extern const struct Array *restrict const algorithms;
extern const struct Array *restrict const exchanges;

extern const struct Config *restrict const cnf;
extern const bool verbose;

extern const struct Numeric *restrict const zero;
extern const struct Numeric *restrict const one;
extern const struct Numeric *restrict const two;
extern const struct Numeric *restrict const sixty;
extern const struct Numeric *restrict const hundred;
extern const struct Numeric *restrict const second_nanos;
extern const struct Numeric *restrict const boot_nanos;

static thrd_t *workers;
static struct Map *product_samples;
static mtx_t global_mtx;
static struct Map *product_prices;
static struct Map *product_trades;
static tss_t abag_tls_key;

static struct Numeric *restrict twenty_five_percent_factor;
static struct Numeric *restrict ninety_percent_factor;
static struct Numeric *restrict order_reload_interval_nanos;
static struct Numeric *restrict boot_delay_nanos;

int abagnale(int argc, char *argv[]);

static struct abag_tls *abag_tls(void) {
  struct abag_tls *tls = tls_get(abag_tls_key);
  if (tls == NULL) {
    tls = heap_malloc(sizeof(struct abag_tls));
    tls->candle_string.s = Numeric_new();
    tls->samples_per_nano.size = Numeric_new();
    tls->samples_per_nano.duration = Numeric_new();
    tls->samples_per_second.n = Numeric_new();
    tls->samples_per_minute.s = Numeric_new();
    tls->samples_load.sample = heap_malloc(sizeof(struct db_sample_res));
    tls->samples_load.sample->nanos = Numeric_new();
    tls->samples_load.sample->price = Numeric_new();
    tls->samples_load.now = Numeric_new();
    tls->samples_load.filter = Numeric_new();
    tls->samples_load.sr = Numeric_new();
    tls->worker_config.sr = Numeric_new();
    tls->worker_config.now = Numeric_new();
    tls->worker_config.r0 = Numeric_new();
    tls->orders_process.tp = Numeric_new();
    tls->samples_process.outdated_ns = Numeric_new();
    tls->samples_process.tp = Numeric_new();
    tls->samples_process.nanos = Numeric_new();
    tls->position_pricing.r0 = Numeric_new();
    tls->position_pricing.r1 = Numeric_new();
    tls->position_pricing.r2 = Numeric_new();
    tls->position_pricing.r3 = Numeric_new();
    tls->position_open.csecs = Numeric_new();
    tls->position_fill.csecs = Numeric_new();
    tls->position_fill.dsecs = Numeric_new();
    tls->position_cancel.r0 = Numeric_new();
    tls->position_timeout.age = Numeric_new();
    tls->position_timeout.stats_to = Numeric_new();
    tls->position_timeout.factor_to = Numeric_new();
    tls->position_timeout.total_to = Numeric_new();
    tls->position_timeout.sl_to = Numeric_new();
    tls->position_timeout.n = Numeric_new();
    tls->position_timeout.stats = heap_malloc(sizeof(struct db_stats_res));
    tls->position_timeout.stats->bd_min = Numeric_new();
    tls->position_timeout.stats->bd_max = Numeric_new();
    tls->position_timeout.stats->bd_avg = Numeric_new();
    tls->position_timeout.stats->sd_min = Numeric_new();
    tls->position_timeout.stats->sd_max = Numeric_new();
    tls->position_timeout.stats->sd_avg = Numeric_new();
    tls->position_timeout.stats->bcl_factor = Numeric_new();
    tls->position_timeout.stats->scl_factor = Numeric_new();
    tls->position_maintain.m = Numeric_new();
    tls->position_maintain.r0 = Numeric_new();
    tls->position_trigger.sr = Numeric_new();
    tls->position_trigger.age = Numeric_new();
    tls->position_trade.o_pr = Numeric_new();
    tls->position_trade.r0 = Numeric_new();
    tls->trade_create.stats = heap_malloc(sizeof(struct db_stats_res));
    tls->trade_create.stats->bd_min = Numeric_new();
    tls->trade_create.stats->bd_max = Numeric_new();
    tls->trade_create.stats->bd_avg = Numeric_new();
    tls->trade_create.stats->sd_min = Numeric_new();
    tls->trade_create.stats->sd_max = Numeric_new();
    tls->trade_create.stats->sd_avg = Numeric_new();
    tls->trade_create.stats->bcl_factor = Numeric_new();
    tls->trade_create.stats->scl_factor = Numeric_new();
    tls->trade_maintain.q_delta = Numeric_new();
    tls->trade_maintain.q_costs = Numeric_new();
    tls->trade_maintain.q_profit = Numeric_new();
    tls->trade_pricing.r0 = Numeric_new();
    tls->trade_bet.hold = heap_malloc(sizeof(struct db_balance_res));
    tls->trade_bet.hold->b = Numeric_new();
    tls->trade_bet.hold->q = Numeric_new();
    tls->trade_bet.b_avail = Numeric_new();
    tls->trade_bet.q_avail = Numeric_new();
    tls->trade_bet.q_costs = Numeric_new();
    tls->trade_bet.r0 = Numeric_new();
    tls->trades_load.trade = heap_malloc(sizeof(struct db_trade_res));
    tls->trades_load.trade->b_cnanos = Numeric_new();
    tls->trades_load.trade->b_dnanos = Numeric_new();
    tls->trades_load.trade->b_price = Numeric_new();
    tls->trades_load.trade->b_b_ordered = Numeric_new();
    tls->trades_load.trade->b_b_filled = Numeric_new();
    tls->trades_load.trade->b_q_fees = Numeric_new();
    tls->trades_load.trade->b_q_filled = Numeric_new();
    tls->trades_load.trade->s_cnanos = Numeric_new();
    tls->trades_load.trade->s_dnanos = Numeric_new();
    tls->trades_load.trade->s_price = Numeric_new();
    tls->trades_load.trade->s_b_ordered = Numeric_new();
    tls->trades_load.trade->s_b_filled = Numeric_new();
    tls->trades_load.trade->s_q_fees = Numeric_new();
    tls->trades_load.trade->s_q_filled = Numeric_new();
    tls_set(abag_tls_key, tls);
  }
  return tls;
}

static void abag_tls_dtor(void *e) {
  struct abag_tls *tls = e;
  Numeric_delete(tls->candle_string.s);
  Numeric_delete(tls->samples_per_nano.size);
  Numeric_delete(tls->samples_per_nano.duration);
  Numeric_delete(tls->samples_per_second.n);
  Numeric_delete(tls->samples_per_minute.s);
  Numeric_delete(tls->samples_load.sample->nanos);
  Numeric_delete(tls->samples_load.sample->price);
  heap_free(tls->samples_load.sample);
  Numeric_delete(tls->samples_load.now);
  Numeric_delete(tls->samples_load.filter);
  Numeric_delete(tls->samples_load.sr);
  Numeric_delete(tls->worker_config.sr);
  Numeric_delete(tls->worker_config.now);
  Numeric_delete(tls->worker_config.r0);
  Numeric_delete(tls->orders_process.tp);
  Numeric_delete(tls->samples_process.outdated_ns);
  Numeric_delete(tls->samples_process.tp);
  Numeric_delete(tls->samples_process.nanos);
  Numeric_delete(tls->position_pricing.r0);
  Numeric_delete(tls->position_pricing.r1);
  Numeric_delete(tls->position_pricing.r2);
  Numeric_delete(tls->position_pricing.r3);
  Numeric_delete(tls->position_open.csecs);
  Numeric_delete(tls->position_fill.csecs);
  Numeric_delete(tls->position_fill.dsecs);
  Numeric_delete(tls->position_cancel.r0);
  Numeric_delete(tls->position_timeout.age);
  Numeric_delete(tls->position_timeout.stats_to);
  Numeric_delete(tls->position_timeout.factor_to);
  Numeric_delete(tls->position_timeout.total_to);
  Numeric_delete(tls->position_timeout.sl_to);
  Numeric_delete(tls->position_timeout.n);
  Numeric_delete(tls->position_timeout.stats->bd_min);
  Numeric_delete(tls->position_timeout.stats->bd_max);
  Numeric_delete(tls->position_timeout.stats->bd_avg);
  Numeric_delete(tls->position_timeout.stats->sd_min);
  Numeric_delete(tls->position_timeout.stats->sd_max);
  Numeric_delete(tls->position_timeout.stats->sd_avg);
  Numeric_delete(tls->position_timeout.stats->bcl_factor);
  Numeric_delete(tls->position_timeout.stats->scl_factor);
  heap_free(tls->position_timeout.stats);
  Numeric_delete(tls->position_maintain.m);
  Numeric_delete(tls->position_maintain.r0);
  Numeric_delete(tls->position_trigger.sr);
  Numeric_delete(tls->position_trigger.age);
  Numeric_delete(tls->position_trade.o_pr);
  Numeric_delete(tls->position_trade.r0);
  Numeric_delete(tls->trade_create.stats->bd_min);
  Numeric_delete(tls->trade_create.stats->bd_max);
  Numeric_delete(tls->trade_create.stats->bd_avg);
  Numeric_delete(tls->trade_create.stats->sd_min);
  Numeric_delete(tls->trade_create.stats->sd_max);
  Numeric_delete(tls->trade_create.stats->sd_avg);
  Numeric_delete(tls->trade_create.stats->bcl_factor);
  Numeric_delete(tls->trade_create.stats->scl_factor);
  heap_free(tls->trade_create.stats);
  Numeric_delete(tls->trade_maintain.q_delta);
  Numeric_delete(tls->trade_maintain.q_costs);
  Numeric_delete(tls->trade_maintain.q_profit);
  Numeric_delete(tls->trade_pricing.r0);
  Numeric_delete(tls->trade_bet.hold->b);
  Numeric_delete(tls->trade_bet.hold->q);
  heap_free(tls->trade_bet.hold);
  Numeric_delete(tls->trade_bet.b_avail);
  Numeric_delete(tls->trade_bet.q_avail);
  Numeric_delete(tls->trade_bet.q_costs);
  Numeric_delete(tls->trade_bet.r0);
  Numeric_delete(tls->trades_load.trade->b_cnanos);
  Numeric_delete(tls->trades_load.trade->b_dnanos);
  Numeric_delete(tls->trades_load.trade->b_price);
  Numeric_delete(tls->trades_load.trade->b_b_ordered);
  Numeric_delete(tls->trades_load.trade->b_b_filled);
  Numeric_delete(tls->trades_load.trade->b_q_fees);
  Numeric_delete(tls->trades_load.trade->b_q_filled);
  Numeric_delete(tls->trades_load.trade->s_cnanos);
  Numeric_delete(tls->trades_load.trade->s_dnanos);
  Numeric_delete(tls->trades_load.trade->s_price);
  Numeric_delete(tls->trades_load.trade->s_b_ordered);
  Numeric_delete(tls->trades_load.trade->s_b_filled);
  Numeric_delete(tls->trades_load.trade->s_q_fees);
  Numeric_delete(tls->trades_load.trade->s_q_filled);
  heap_free(tls->trades_load.trade);
  heap_free(tls);
  tls_set(abag_tls_key, NULL);
}

static inline void trigger_init(struct Trigger *restrict const t) {
  t->cnt = 0;
  t->set = false;
  t->nanos = Numeric_copy(zero);
}

static inline void trigger_reset(struct Trigger *restrict const tr) {
  tr->cnt = 0;
  tr->set = false;
  Numeric_copy_to(zero, tr->nanos);
}

static inline void trigger_delete(const struct Trigger *restrict const t) {
  Numeric_delete(t->nanos);
}

static inline void candle_init(struct Candle *restrict const c) {
  c->t = CANDLE_NONE;
  c->o = Numeric_copy(zero);
  c->c = Numeric_copy(zero);
  c->h = Numeric_copy(zero);
  c->l = Numeric_copy(zero);
  c->pc = Numeric_copy(zero);
  c->a = Numeric_copy(zero);
  c->onanos = Numeric_copy(zero);
  c->hnanos = Numeric_copy(zero);
  c->lnanos = Numeric_copy(zero);
  c->cnanos = Numeric_copy(zero);
}

struct Candle *Candle_new(void) {
  struct Candle *restrict const c = heap_malloc(sizeof(struct Candle));
  candle_init(c);
  return c;
}

void Candle_delete(void *restrict const c) {
  if (c == NULL)
    return;

  struct Candle *restrict const cd = c;
  Numeric_delete(cd->o);
  Numeric_delete(cd->c);
  Numeric_delete(cd->h);
  Numeric_delete(cd->l);
  Numeric_delete(cd->pc);
  Numeric_delete(cd->a);
  Numeric_delete(cd->onanos);
  Numeric_delete(cd->cnanos);
  Numeric_delete(cd->hnanos);
  Numeric_delete(cd->lnanos);
}

void Candle_reset(struct Candle *restrict const c) {
  c->t = CANDLE_NONE;
  Numeric_copy_to(zero, c->o);
  Numeric_copy_to(zero, c->h);
  Numeric_copy_to(zero, c->l);
  Numeric_copy_to(zero, c->c);
  Numeric_copy_to(zero, c->pc);
  Numeric_copy_to(zero, c->a);
  Numeric_copy_to(zero, c->onanos);
  Numeric_copy_to(zero, c->hnanos);
  Numeric_copy_to(zero, c->lnanos);
  Numeric_copy_to(zero, c->cnanos);
}

void Candle_copy_to(const struct Candle *restrict const src,
                    struct Candle *restrict const tgt) {
  tgt->t = src->t;
  Numeric_copy_to(src->o, tgt->o);
  Numeric_copy_to(src->c, tgt->c);
  Numeric_copy_to(src->h, tgt->h);
  Numeric_copy_to(src->l, tgt->l);
  Numeric_copy_to(src->pc, tgt->pc);
  Numeric_copy_to(src->a, tgt->a);
  Numeric_copy_to(src->onanos, tgt->onanos);
  Numeric_copy_to(src->cnanos, tgt->cnanos);
  Numeric_copy_to(src->hnanos, tgt->hnanos);
  Numeric_copy_to(src->lnanos, tgt->lnanos);
}

const struct Algorithm *algorithm(const struct String *restrict const nm) {
  void **items = Array_items(algorithms);
  for (size_t i = Array_size(algorithms); i > 0; i--)
    if (String_equals(nm, ((struct Algorithm *)items[i - 1])->nm))
      return items[i - 1];

  return NULL;
}

const struct Exchange *exchange(const struct String *restrict const nm) {
  void **items = Array_items(exchanges);
  for (size_t i = Array_size(exchanges); i > 0; i--)
    if (String_equals(nm, ((struct Exchange *)items[i - 1])->nm))
      return items[i - 1];

  return NULL;
}

static char *candle_string(const struct Candle *restrict const c,
                           const char *restrict const c_id, const int c_sc) {
#define CANDLE_STRING_MAX_LENGTH (size_t)512
  const struct abag_tls *restrict const tls = abag_tls();
  struct Numeric *restrict const s = tls->candle_string.s;

  Numeric_sub_to(c->h, c->l, s);

  char *restrict const o = Numeric_to_char(c->o, c_sc);
  char *restrict const cl = Numeric_to_char(c->c, c_sc);
  char *restrict const h = Numeric_to_char(c->h, c_sc);
  char *restrict const l = Numeric_to_char(c->l, c_sc);
  char *restrict const s_info = Numeric_to_char(s, c_sc);
  char *restrict const pc = Numeric_to_char(c->pc, 2);
  char *restrict const a = Numeric_to_char(c->a, 4);
  char *restrict const open = nanos_to_iso8601(c->onanos);
  char *restrict const close = nanos_to_iso8601(c->cnanos);
  char *restrict const res = heap_malloc(CANDLE_STRING_MAX_LENGTH);
  char i = ' ';

  switch (c->t) {
  case CANDLE_NONE:
    i = '?';
    break;
  case CANDLE_UP:
    i = '+';
    break;
  case CANDLE_DOWN:
    i = '-';
    break;
  }

  const int r = snprintf(
      res, CANDLE_STRING_MAX_LENGTH,
      "[%c: O%s%s: H%s%s: L%s%s: C%s%s: S%s%s: %s%%: %srad: %s->%s]", i, o,
      c_id, h, c_id, l, c_id, cl, c_id, s_info, c_id, pc, a, open, close);

  if (r < 0 || (size_t)r >= CANDLE_STRING_MAX_LENGTH) {
    werr("%s: %d: %s: Candle string exceeds %zu characters\n", __FILE__,
         __LINE__, __func__, CANDLE_STRING_MAX_LENGTH);
    fatal();
  }

  Numeric_char_free(o);
  Numeric_char_free(h);
  Numeric_char_free(l);
  Numeric_char_free(cl);
  Numeric_char_free(s_info);
  Numeric_char_free(pc);
  Numeric_char_free(a);
  heap_free(open);
  heap_free(close);
  return res;
}

static inline void position_init(struct Position *restrict const p) {
  p->done = false;
  p->filled = false;
  p->id = NULL;
  p->cnanos = Numeric_copy(zero);
  p->dnanos = Numeric_copy(zero);
  p->rnanos = Numeric_copy(zero);
  p->price = Numeric_copy(zero);
  p->sl_price = Numeric_copy(zero);
  p->tp_price = Numeric_copy(zero);
  p->b_ordered = Numeric_copy(zero);
  p->b_filled = Numeric_copy(zero);
  p->q_fees = Numeric_copy(zero);
  p->fee_pc = Numeric_copy(zero);
  p->q_filled = Numeric_copy(zero);
  p->q_ordered = Numeric_copy(zero);
  p->cl_samples = Numeric_copy(zero);
  p->cl_factor = Numeric_copy(one);
  p->sl_samples = Numeric_copy(zero);
  trigger_init(&p->sl_trg);
  trigger_init(&p->tl_trg);
  trigger_init(&p->tp_trg);
}

static inline void position_reset(struct Position *restrict const p) {
  p->done = false;
  p->filled = false;

  String_delete(p->id);
  p->id = NULL;

  Numeric_copy_to(zero, p->cnanos);
  Numeric_copy_to(zero, p->dnanos);
  Numeric_copy_to(zero, p->rnanos);
  Numeric_copy_to(zero, p->price);
  Numeric_copy_to(zero, p->sl_price);
  Numeric_copy_to(zero, p->tp_price);
  Numeric_copy_to(zero, p->b_ordered);
  Numeric_copy_to(zero, p->b_filled);
  Numeric_copy_to(zero, p->q_fees);
  Numeric_copy_to(zero, p->fee_pc);
  Numeric_copy_to(zero, p->q_filled);
  Numeric_copy_to(zero, p->q_ordered);
  Numeric_copy_to(zero, p->cl_samples);
  Numeric_copy_to(zero, p->sl_samples);
  trigger_reset(&p->sl_trg);
  trigger_reset(&p->tl_trg);
  trigger_reset(&p->tp_trg);
}

static inline void position_delete(const struct Position *restrict const p) {
  String_delete(p->id);
  Numeric_delete(p->cnanos);
  Numeric_delete(p->dnanos);
  Numeric_delete(p->rnanos);
  Numeric_delete(p->price);
  Numeric_delete(p->sl_price);
  Numeric_delete(p->tp_price);
  Numeric_delete(p->b_ordered);
  Numeric_delete(p->b_filled);
  Numeric_delete(p->q_fees);
  Numeric_delete(p->fee_pc);
  Numeric_delete(p->q_filled);
  Numeric_delete(p->q_ordered);
  Numeric_delete(p->cl_samples);
  Numeric_delete(p->cl_factor);
  Numeric_delete(p->sl_samples);
  trigger_delete(&p->sl_trg);
  trigger_delete(&p->tl_trg);
  trigger_delete(&p->tp_trg);
}

static char *position_string(const struct Trade *restrict const t,
                             const struct Position *restrict const p) {
#define POSITION_STRING_MAX_LENGTH (size_t)512
  char *restrict const pr = Numeric_to_char(p->price, t->p_sc);
  char *restrict const sl_pr = Numeric_to_char(p->sl_price, t->q_sc);
  char *restrict const tp_pr = Numeric_to_char(p->tp_price, t->q_sc);
  char *restrict const b_o = Numeric_to_char(p->b_ordered, t->b_sc);
  char *restrict const q_fee = Numeric_to_char(p->q_fees, t->q_sc);
  char *restrict const q_f = Numeric_to_char(p->q_filled, t->q_sc);
  char *restrict const b_f = Numeric_to_char(p->b_filled, t->b_sc);
  char *restrict const c = nanos_to_iso8601(p->cnanos);
  char *restrict const res = heap_malloc(POSITION_STRING_MAX_LENGTH);
  const char *restrict side;

  switch (p->side) {
  case POSITION_SIDE_BUY:
    side = "Buy position";
    break;
  case POSITION_SIDE_SELL:
    side = "Sell position";
    break;
  default:
    werr("%s: %d: %s: Position neither buy nor sell\n", __FILE__, __LINE__,
         __func__);
    fatal();
  }

  const int r = snprintf(
      res, POSITION_STRING_MAX_LENGTH,
      "%s: %s: %s%s@%s%s, b: %s%s, q: %s%s, f: %s%s, sl: %s%s, tp: %s%s", side,
      c, b_o, String_chars(t->b_id), pr, String_chars(t->q_id), b_f,
      String_chars(t->b_id), q_f, String_chars(t->q_id), q_fee,
      String_chars(t->q_id), sl_pr, String_chars(t->q_id), tp_pr,
      String_chars(t->q_id));

  if (r < 0 || (size_t)r >= POSITION_STRING_MAX_LENGTH) {
    werr("%s: %d: %s: Position string exceeds %zu characters\n", __FILE__,
         __LINE__, __func__, POSITION_STRING_MAX_LENGTH);
    fatal();
  }

  Numeric_char_free(pr);
  Numeric_char_free(sl_pr);
  Numeric_char_free(tp_pr);
  Numeric_char_free(b_o);
  Numeric_char_free(q_fee);
  Numeric_char_free(q_f);
  Numeric_char_free(b_f);
  heap_free(c);
  return res;
}

static inline struct Trade *trade_new(
    struct String *restrict const e_id, struct String *restrict const p_id,
    struct String *restrict const b_id, struct String *restrict const q_id,
    struct String *restrict const ba_id, struct String *restrict const qa_id,
    unsigned int b_scale, unsigned int q_scale, unsigned int p_scale) {
  struct Trade *restrict const t = heap_malloc(sizeof(struct Trade));
  t->id = NULL;
  t->status = TRADE_STATUS_NEW;
  t->e_id = e_id;
  t->p_id = p_id;
  t->b_id = b_id;
  t->q_id = q_id;
  t->ba_id = ba_id;
  t->qa_id = qa_id;
  t->b_sc = b_scale;
  t->q_sc = q_scale;
  t->p_sc = p_scale;
  t->fee_pc = Numeric_copy(zero);
  t->fee_pf = Numeric_copy(one);
  t->tp_pc = Numeric_copy(zero);
  t->tp_pf = Numeric_copy(one);
  t->tp = Numeric_copy(zero);
  t->a = NULL;
  t->a_st = NULL;
  candle_init(&t->bet_cd);
  position_init(&t->p_long);
  t->p_long.side = POSITION_SIDE_BUY;
  position_init(&t->p_short);
  t->p_short.side = POSITION_SIDE_SELL;
  trigger_init(&t->bet_trg);
  return t;
}

static inline void trade_delete(void *restrict const t) {
  if (t == NULL)
    return;

  struct Trade *restrict const trade = t;
  String_delete(trade->id);
  String_delete(trade->e_id);
  String_delete(trade->p_id);
  String_delete(trade->b_id);
  String_delete(trade->q_id);
  String_delete(trade->ba_id);
  String_delete(trade->qa_id);
  Numeric_delete(trade->fee_pc);
  Numeric_delete(trade->fee_pf);
  Numeric_delete(trade->tp_pc);
  Numeric_delete(trade->tp_pf);
  Numeric_delete(trade->tp);
  Candle_delete(&trade->bet_cd);
  position_delete(&trade->p_long);
  position_delete(&trade->p_short);
  trigger_delete(&trade->bet_trg);

  if (trade->a != NULL)
    trade->a->deconfigure(trade);

  heap_free(trade);
}

static inline void sample_array_delete(void *restrict const entry) {
  Array_delete(entry, Sample_delete);
}

static inline void trade_array_delete(void *restrict const entry) {
  Array_delete(entry, trade_delete);
}

static inline int sample_cmp(const void *restrict const e1,
                             const void *restrict const e2) {
  return Numeric_cmp(((const struct Sample **)e1)[0]->nanos,
                     ((const struct Sample **)e2)[0]->nanos);
}

void samples_per_nano(struct Numeric *restrict const ret,
                      const struct Array *restrict const samples) {
  const struct abag_tls *restrict const tls = abag_tls();
  struct Numeric *restrict const size = tls->samples_per_nano.size;
  struct Numeric *restrict const duration = tls->samples_per_nano.duration;
  const size_t s_size = Array_size(samples);

  if (s_size > 1) {
    const struct Sample *restrict const s_tail = Array_tail(samples);
    const struct Sample *restrict const s_head = Array_head(samples);

    Numeric_from_long_to(s_size, size);
    Numeric_sub_to(s_tail->nanos, s_head->nanos, duration);

    if (Numeric_cmp(zero, duration) != 0)
      Numeric_div_to(size, duration, ret);
    else
      Numeric_copy_to(zero, ret);

  } else
    Numeric_copy_to(zero, ret);
}

void samples_per_second(struct Numeric *restrict const ret,
                        const struct Array *restrict const samples) {
  const struct abag_tls *restrict const tls = abag_tls();
  struct Numeric *restrict const n = tls->samples_per_second.n;

  samples_per_nano(n, samples);
  Numeric_mul_to(n, second_nanos, ret);
}

void samples_per_minute(struct Numeric *restrict const ret,
                        const struct Array *restrict const samples) {
  const struct abag_tls *restrict const tls = abag_tls();
  struct Numeric *restrict const s = tls->samples_per_minute.s;

  samples_per_second(s, samples);
  Numeric_mul_to(s, sixty, ret);
}

static struct Array *
samples_load(const struct worker_ctx *restrict const w_ctx) {
  const struct abag_tls *restrict const tls = abag_tls();
  struct Numeric *restrict const now = tls->samples_load.now;
  struct Numeric *restrict const filter = tls->samples_load.filter;
  struct Numeric *restrict const sr = tls->samples_load.sr;
  struct db_sample_res *restrict const sample = tls->samples_load.sample;

  nanos_now(now);
  Numeric_sub_to(now, cnf->wnanos_max, filter);
  db_samples_open(w_ctx->db, String_chars(w_ctx->ex->id),
                  String_chars(w_ctx->p->id), filter);

  struct Array *restrict const a = Array_new(100000);

  while (!terminated && db_samples_next(sample, w_ctx->db)) {
    struct Sample *restrict const s = Sample_new();
    s->p_id = String_copy(w_ctx->p->id);
    s->nanos = Numeric_copy(sample->nanos);
    s->price = Numeric_copy(sample->price);

    Array_add_tail(a, s);
  }

  db_samples_close(w_ctx->db);
  Array_shrink(a);

  if (verbose && Array_size(a) > 1) {
    samples_per_minute(sr, a);
    const struct Sample *restrict const s_head = Array_head(a);
    const struct Sample *restrict const s_tail = Array_tail(a);
    char *restrict const b = nanos_to_iso8601(s_head->nanos);
    char *restrict const e = nanos_to_iso8601(s_tail->nanos);
    char *restrict const sr_asc = Numeric_to_char(sr, 2);

    wout("%s: %s->%s: Loaded %zu samples: %s samples/min, %s->%s\n",
         String_chars(w_ctx->ex->nm), String_chars(w_ctx->p->q_id),
         String_chars(w_ctx->p->b_id), Array_size(a), sr_asc, b, e);

    heap_free(b);
    heap_free(e);
    Numeric_char_free(sr_asc);
  }

  return a;
}

static void worker_config(struct worker_ctx *restrict const w_ctx,
                          const struct Array *restrict const samples) {
  const struct abag_tls *restrict const tls = abag_tls();
  struct Numeric *restrict const sr = tls->worker_config.sr;
  struct Numeric *restrict const now = tls->worker_config.now;
  struct Numeric *restrict const r0 = tls->worker_config.r0;
  void **items;
  samples_per_minute(sr, samples);

  items = Array_items(cnf->p_cnf);
  for (size_t i = Array_size(cnf->p_cnf); i > 0; i--) {
    const struct ProductConfig *restrict const c = items[i - 1];

    if (!String_equals(c->e_nm, w_ctx->ex->nm))
      continue;

    if (c->sr_min != NULL && Numeric_cmp(sr, c->sr_min) < 0)
      continue;

    if (c->sr_max != NULL && Numeric_cmp(sr, c->sr_max) >= 0)
      continue;

    if (!ProductConfig_market(c, w_ctx->p->nm))
      continue;

    w_ctx->p_cnf = c;
    w_ctx->a = algorithm(c->a_nm);
    break;
  }

  if (w_ctx->p_cnf == NULL)
    return;

  if (!String_equals(w_ctx->p->q_id, w_ctx->p_cnf->q_id)) {
    struct Product *restrict q_p = NULL;
    struct Array *restrict const products = w_ctx->ex->products();

    Array_lock(products);
    items = Array_items(products);
    for (size_t i = Array_size(products); i > 0; i--) {
      struct Product *restrict const needle = items[i - 1];
      if (String_equals(needle->q_id, w_ctx->p_cnf->q_id) &&
          String_equals(needle->b_id, w_ctx->p->q_id)) {
        q_p = Product_copy(needle);
        break;
      }
    }
    Array_unlock(products);

    if (q_p == NULL) {
      nanos_now(now);
      Numeric_sub_to(now, boot_nanos, r0);
      if (Numeric_cmp(r0, boot_delay_nanos) > 0)
        werr("%s: Failure calculating funds: Product %s->%s not found\n",
             String_chars(w_ctx->ex->nm), String_chars(w_ctx->p_cnf->q_id),
             String_chars(w_ctx->p->q_id));

      w_ctx->q_tgt = NULL;
      return;
    }

    Map_lock(product_samples);
    struct Array *restrict const q_samples = Map_get(product_samples, q_p->id);

    Product_delete(q_p);

    if (q_samples == NULL) {
      nanos_now(now);
      Numeric_sub_to(now, boot_nanos, r0);
      if (Numeric_cmp(r0, boot_delay_nanos) > 0)
        werr("%s: Failure calculating funds: Product %s->%s not found\n",
             String_chars(w_ctx->ex->nm), String_chars(w_ctx->p_cnf->q_id),
             String_chars(w_ctx->p->q_id));

      Map_unlock(product_samples);
      w_ctx->q_tgt = NULL;
      return;
    }
    Map_unlock(product_samples);

    Array_lock(q_samples);
    const struct Sample *restrict const q_sample = Array_tail(q_samples);

    if (q_sample == NULL) {
      nanos_now(now);
      Numeric_sub_to(now, boot_nanos, r0);
      if (Numeric_cmp(r0, boot_delay_nanos) > 0)
        werr("%s: Failure calculating funds: Price %s->%s not found\n",
             String_chars(w_ctx->ex->nm), String_chars(w_ctx->p_cnf->q_id),
             String_chars(w_ctx->p->q_id));

      Array_unlock(q_samples);
      w_ctx->q_tgt = NULL;
      return;
    }

    Numeric_div_to(one, q_sample->price, r0);
    Numeric_mul_to(r0, w_ctx->p_cnf->q_tgt, w_ctx->q_tgt);
    Array_unlock(q_samples);
  } else
    Numeric_copy_to(w_ctx->p_cnf->q_tgt, w_ctx->q_tgt);
}

static void position_pricing(const struct worker_ctx *restrict const w_ctx,
                             struct Trade *restrict const t,
                             struct Position *restrict const p,
                             const bool create) {
  const struct abag_tls *restrict const tls = abag_tls();
  struct Numeric *restrict const r0 = tls->position_pricing.r0;
  struct Numeric *restrict const r1 = tls->position_pricing.r1;
  struct Numeric *restrict const r2 = tls->position_pricing.r2;
  struct Numeric *restrict const r3 = tls->position_pricing.r3;

  /*
   * Let
   *  b = Minimum base amount to order.
   *  p = Order price.
   *  fee = Fee percent factor (e.g. 0.01).
   *  tgt = Target percent factor (e.g. 1.01).
   *  tp = Targetted take profit.
   *  tp_pr_long = Take profit price for a long position.
   *  sl_pr_long = Stop loss price for a long position.
   *  tp_pr_short = Take profit price for a short position.
   *  sl_pr_short = Stop loss price for a short position.
   *
   *                tp
   *    b = ------------------
   *         (1+fee)(tgt-1)*p
   *
   *    sl_pr_long = p+2*p*fee
   *
   *    tp_pr_long = sl_pr_long * tgt
   *
   *    tp_pr_short = 2*p-tp_pr_long
   *
   *    sl_pr_short = p-2*p*fee
   *
   */

  if (create) {
    Numeric_div_to(t->fee_pc, hundred, r0);
    // r0 = fee
    Numeric_add_to(one, r0, r1);
    // r1 = (1+fee)
    Numeric_sub_to(t->tp_pf, one, r2);
    // r2 = (tgt-1)
    Numeric_mul_to(r1, r2, r3);
    Numeric_mul_to(r3, p->price, r1);
    // r1: (1+fee)(tgt-1)p
    Numeric_div_to(t->tp, r1, p->b_ordered);
    Numeric_scale(p->b_ordered, t->b_sc);
  }

  if (Numeric_cmp(t->fee_pc, p->fee_pc) > 0) {
    Numeric_copy_to(t->fee_pc, p->fee_pc);
    Numeric_div_to(t->fee_pc, hundred, r0);
    // r0 = fee

    // Stop loss price long.
    Numeric_mul_to(two, r0, r1);
    // r1 = 2*fee
    Numeric_mul_to(r1, p->price, r2);
    // r2 = p*2*fee
    Numeric_add_to(p->price, r2, p->sl_price);

    // Take profit price long.
    Numeric_mul_to(p->sl_price, t->tp_pf, p->tp_price);

    // Quote.
    Numeric_mul_to(p->b_ordered, p->price, p->q_ordered);
    Numeric_mul_to(p->q_ordered, r0, p->q_fees);
    Numeric_scale(p->q_fees, t->q_sc);
    Numeric_scale(p->q_ordered, t->q_sc);

    switch (p->side) {
    case POSITION_SIDE_BUY:
      break;
    case POSITION_SIDE_SELL:
      // Take profit price short.
      Numeric_mul_to(two, p->price, r1);
      // r1 = 2*p
      Numeric_sub_to(r1, p->tp_price, r2);
      // r2 = 2*p-tp_pr_long
      Numeric_copy_to(r2, p->tp_price);

      // Stop loss price short.
      Numeric_mul_to(two, r0, r1);
      // r1 = 2*fee
      Numeric_mul_to(r1, p->price, r2);
      // r2 = p*2*fee
      Numeric_sub_to(p->price, r2, p->sl_price);
      break;
    default:
      werr("%s: %d: %s: Position neither buy nor sell\n", __FILE__, __LINE__,
           __func__);
      fatal();
    }

    Numeric_scale(p->tp_price, t->p_sc);
    Numeric_scale(p->sl_price, t->p_sc);
  }
}

static void position_create(const struct worker_ctx *restrict const w_ctx,
                            struct Trade *restrict const t,
                            struct Position *restrict const p) {
  char t_id[DATABASE_UUID_MAX_LENGTH] = {0};

  switch (p->side) {
  case POSITION_SIDE_BUY:
    if (t->id == NULL) {
      db_trade_bcreate(t_id, w_ctx->db, String_chars(w_ctx->ex->id),
                       String_chars(t->p_id), String_chars(t->b_id),
                       String_chars(t->q_id), String_chars(p->id), p->b_ordered,
                       p->price);
      t->id = String_new(t_id);
    } else
      db_trade_bupdate(w_ctx->db, String_chars(t->id), String_chars(p->id),
                       p->b_ordered, p->price);

    t->status = TRADE_STATUS_BUYING;
    break;
  case POSITION_SIDE_SELL:
    if (t->id == NULL) {
      db_trade_screate(t_id, w_ctx->db, String_chars(w_ctx->ex->id),
                       String_chars(t->p_id), String_chars(t->b_id),
                       String_chars(t->q_id), String_chars(p->id), p->b_ordered,
                       p->price);
      t->id = String_new(t_id);
    } else
      db_trade_supdate(w_ctx->db, String_chars(t->id), String_chars(p->id),
                       p->b_ordered, p->price);

    t->status = TRADE_STATUS_SELLING;
    break;
  default:
    werr("%s: %d: %s: Position neither buy nor sell\n", __FILE__, __LINE__,
         __func__);
    fatal();
  }

  p->done = false;
  p->filled = false;
  position_pricing(w_ctx, t, p, false);
  nanos_now(p->cnanos);
}

static void position_open(const struct worker_ctx *restrict const w_ctx,
                          struct Trade *restrict const t,
                          struct Position *restrict const p,
                          const struct Order *restrict const order) {
  const struct abag_tls *restrict const tls = abag_tls();
  struct Numeric *restrict const csecs = tls->position_open.csecs;

  Numeric_copy_to(order->cnanos, p->cnanos);
  Numeric_copy_to(order->b_filled, p->b_filled);
  Numeric_copy_to(order->q_filled, p->q_filled);
  Numeric_copy_to(order->q_fees, p->q_fees);
  p->filled = Numeric_cmp(zero, order->b_filled) != 0;

  Numeric_div_to(p->cnanos, second_nanos, csecs);
  Numeric_scale(csecs, 0);

  switch (p->side) {
  case POSITION_SIDE_BUY:
    db_trade_bopen(w_ctx->db, String_chars(order->id), csecs, order->b_filled,
                   order->q_filled, order->q_fees);
    break;
  case POSITION_SIDE_SELL:
    db_trade_sopen(w_ctx->db, String_chars(order->id), csecs, order->b_filled,
                   order->q_filled, order->q_fees);
    break;
  default:
    werr("%s: %d: %s: Position neither buy nor sell\n", __FILE__, __LINE__,
         __func__);
    fatal();
  }
}

static void position_fill(const struct worker_ctx *restrict const w_ctx,
                          struct Trade *restrict const t,
                          struct Position *restrict const p,
                          const struct Order *restrict const order) {
  const struct abag_tls *restrict const tls = abag_tls();
  struct Numeric *restrict const csecs = tls->position_fill.csecs;
  struct Numeric *restrict const dsecs = tls->position_fill.dsecs;

  Numeric_copy_to(order->cnanos, p->cnanos);
  Numeric_copy_to(order->b_filled, p->b_filled);
  Numeric_copy_to(order->q_filled, p->q_filled);
  Numeric_copy_to(order->q_fees, p->q_fees);
  p->filled = Numeric_cmp(zero, order->b_filled) != 0;

  if (order->settled) {
    p->done = true;
    Numeric_copy_to(order->dnanos, p->dnanos);
    Numeric_copy_to(one, p->cl_factor);
    trigger_reset(&p->sl_trg);
    trigger_reset(&p->tl_trg);
    trigger_reset(&p->tp_trg);
    t->a->position_done(w_ctx->db, w_ctx->ex, t, p);
  }

  const bool t_done = t->p_long.done && t->p_short.done;

  Numeric_div_to(p->cnanos, second_nanos, csecs);
  Numeric_div_to(p->dnanos, second_nanos, dsecs);
  Numeric_scale(csecs, 0);
  Numeric_scale(dsecs, 0);

  switch (p->side) {
  case POSITION_SIDE_BUY:
    db_trade_bfill(w_ctx->db, String_chars(order->id), csecs,
                   order->settled ? dsecs : NULL, order->b_filled,
                   order->q_filled, order->q_fees, t_done);

    if (order->settled)
      db_stats_bcl_factor(w_ctx->db, String_chars(w_ctx->ex->id),
                          String_chars(t->p_id), p->cl_factor);

    if (t_done)
      t->status = TRADE_STATUS_DONE;
    else if (order->settled)
      t->status = TRADE_STATUS_BOUGHT;

    break;
  case POSITION_SIDE_SELL:
    db_trade_sfill(w_ctx->db, String_chars(order->id), csecs,
                   order->settled ? dsecs : NULL, order->b_filled,
                   order->q_filled, order->q_fees, t_done);

    if (order->settled)
      db_stats_scl_factor(w_ctx->db, String_chars(w_ctx->ex->id),
                          String_chars(t->p_id), p->cl_factor);

    if (t_done)
      t->status = TRADE_STATUS_DONE;
    else if (order->settled)
      t->status = TRADE_STATUS_SOLD;

    break;
  default:
    werr("%s: %d: %s: Position neither buy nor sell\n", __FILE__, __LINE__,
         __func__);
    fatal();
  }
}

static void position_cancel(const struct worker_ctx *restrict const w_ctx,
                            struct Trade *restrict const t,
                            struct Position *restrict const p) {
  const struct abag_tls *restrict const tls = abag_tls();
  struct Numeric *restrict const r0 = tls->position_cancel.r0;

  Numeric_mul_to(twenty_five_percent_factor, p->cl_factor, r0);
  Numeric_copy_to(r0, p->cl_factor);

  switch (p->side) {
  case POSITION_SIDE_BUY:
    if (t->p_short.id != NULL) {
      db_trade_breset(w_ctx->db, String_chars(p->id));
      t->status = TRADE_STATUS_SOLD;
    }

    db_stats_bcl_factor(w_ctx->db, String_chars(w_ctx->ex->id),
                        String_chars(t->p_id), p->cl_factor);

    break;
  case POSITION_SIDE_SELL:
    if (t->p_long.id != NULL) {
      db_trade_sreset(w_ctx->db, String_chars(p->id));
      t->status = TRADE_STATUS_BOUGHT;
    }

    db_stats_scl_factor(w_ctx->db, String_chars(w_ctx->ex->id),
                        String_chars(t->p_id), p->cl_factor);

    break;
  default:
    werr("%s: %d: %s: Position neither buy nor sell\n", __FILE__, __LINE__,
         __func__);
    fatal();
  }

  position_reset(p);

  if (t->p_long.id == NULL && t->p_short.id == NULL) {
    db_trade_delete(w_ctx->db, String_chars(t->id));

    if (verbose)
      wout("%s: %s->%s: %s: Trade cancelled\n", String_chars(w_ctx->ex->nm),
           String_chars(t->q_id), String_chars(t->b_id), String_chars(t->id));

    String_delete(t->id);
    t->id = NULL;
    t->status = TRADE_STATUS_CANCELLED;
  }
}

static void position_timeout(const struct worker_ctx *restrict const w_ctx,
                             struct Trade *restrict t,
                             struct Position *restrict p,
                             const struct Array *restrict const samples,
                             const struct Sample *restrict const sample) {
  const struct abag_tls *restrict const tls = abag_tls();
  struct Numeric *restrict const age = tls->position_timeout.age;
  struct Numeric *restrict const stats_to = tls->position_timeout.stats_to;
  struct Numeric *restrict const factor_to = tls->position_timeout.factor_to;
  struct Numeric *restrict const total_to = tls->position_timeout.total_to;
  struct Numeric *restrict const sl_to = tls->position_timeout.sl_to;
  struct Numeric *restrict const n = tls->position_timeout.n;
  struct db_stats_res *restrict const stats = tls->position_timeout.stats;
  const bool db = db_stats(stats, w_ctx->db, String_chars(w_ctx->ex->id),
                           String_chars(t->p_id));

  switch (p->side) {
  case POSITION_SIDE_BUY:
    if (db && !stats->bd_avg_null)
      Numeric_copy_to(stats->bd_avg, stats_to);
    else
      Numeric_copy_to(w_ctx->p_cnf->wnanos, stats_to);

    if (w_ctx->p_cnf->bo_minnanos != NULL &&
        Numeric_cmp(w_ctx->p_cnf->bo_minnanos, stats_to) > 0)
      Numeric_copy_to(w_ctx->p_cnf->bo_minnanos, stats_to);

    if (w_ctx->p_cnf->bo_maxnanos != NULL &&
        Numeric_cmp(w_ctx->p_cnf->bo_maxnanos, stats_to) < 0)
      Numeric_copy_to(w_ctx->p_cnf->bo_maxnanos, stats_to);

    break;
  case POSITION_SIDE_SELL:
    if (db && !stats->sd_avg_null)
      Numeric_copy_to(stats->sd_avg, stats_to);
    else
      Numeric_copy_to(w_ctx->p_cnf->wnanos, stats_to);

    if (w_ctx->p_cnf->so_minnanos != NULL &&
        Numeric_cmp(w_ctx->p_cnf->so_minnanos, stats_to) > 0)
      Numeric_copy_to(w_ctx->p_cnf->so_minnanos, stats_to);

    if (w_ctx->p_cnf->so_maxnanos != NULL &&
        Numeric_cmp(w_ctx->p_cnf->so_maxnanos, stats_to) < 0)
      Numeric_copy_to(w_ctx->p_cnf->so_maxnanos, stats_to);

    break;
  default:
    werr("%s: %d: %s: Position neither buy nor sell\n", __FILE__, __LINE__,
         __func__);
    fatal();
  }

  Numeric_add_to(sample->nanos, order_reload_interval_nanos, p->rnanos);
  Numeric_sub_to(sample->nanos, p->cnanos, age);
  Numeric_mul_to(stats_to, p->cl_factor, factor_to);
  Numeric_sub_to(factor_to, age, total_to);
  samples_per_nano(n, samples);
  Numeric_mul_to(n, total_to, p->cl_samples);
  Numeric_sub_to(w_ctx->p_cnf->sl_dlnanos, age, sl_to);
  Numeric_mul_to(n, sl_to, p->sl_samples);
}

static void position_maintain(const struct worker_ctx *restrict const w_ctx,
                              struct Trade *restrict const t,
                              struct Position *restrict const p,
                              const struct Array *restrict const samples,
                              const struct Sample *restrict const sample,
                              struct Order *restrict order) {
  const struct abag_tls *restrict const tls = abag_tls();
  struct Numeric *restrict const m = tls->position_maintain.m;
  struct Numeric *restrict const r0 = tls->position_maintain.r0;
  bool cancel = false;
  const bool reload = Numeric_cmp(sample->nanos, p->rnanos) > 0;
  const bool free_order = order == NULL;

  Numeric_sub_to(p->cl_samples, one, r0);
  Numeric_copy_to(r0, p->cl_samples);

  if (!p->filled && !p->tl_trg.set) {
    if (Numeric_cmp(p->cl_samples, zero) <= 0) {
      if (verbose)
        wout("%s: %s->%s: %s: Order timed out\n", String_chars(w_ctx->ex->nm),
             String_chars(t->q_id), String_chars(t->b_id), String_chars(t->id));

      cancel = true;
    }
  }

  if (cancel || reload || order != NULL) {
    if (order == NULL)
      order = w_ctx->ex->order(p->id);

    if (order == NULL) {
      position_timeout(w_ctx, t, p, samples, sample);
      char *restrict const p_info = position_string(t, p);

      werr("%s: %s->%s: %s: Failure loading position\n",
           String_chars(w_ctx->ex->nm), String_chars(t->q_id),
           String_chars(t->b_id), String_chars(t->id));

      werr("%s: %s->%s: %s: %s\n", String_chars(w_ctx->ex->nm),
           String_chars(t->q_id), String_chars(t->b_id), String_chars(p->id),
           p_info);

      heap_free(p_info);
      return;
    }

    if (order->status == ORDER_STATUS_FILLED) {
      position_fill(w_ctx, t, p, order);
      position_timeout(w_ctx, t, p, samples, sample);

      if (verbose) {
        char *restrict const p_info = position_string(t, p);

        if (order->settled)
          wout("%s: %s->%s: %s: Position done\n", String_chars(w_ctx->ex->nm),
               String_chars(t->q_id), String_chars(t->b_id),
               String_chars(t->id));
        else
          wout("%s: %s->%s: %s: Position filled\n", String_chars(w_ctx->ex->nm),
               String_chars(t->q_id), String_chars(t->b_id),
               String_chars(t->id));

        wout("%s: %s->%s: %s: %s\n", String_chars(w_ctx->ex->nm),
             String_chars(t->q_id), String_chars(t->b_id), String_chars(p->id),
             p_info);

        if (order->msg && String_length(order->msg) > 0) {
          wout("%s: %s->%s: %s: %s\n", String_chars(w_ctx->ex->nm),
               String_chars(t->q_id), String_chars(t->b_id),
               String_chars(p->id), String_chars(order->msg));
        }

        heap_free(p_info);
      }
    } else if (order->status == ORDER_STATUS_OPEN) {
      position_open(w_ctx, t, p, order);
      position_timeout(w_ctx, t, p, samples, sample);

      if (verbose) {
        samples_per_minute(m, samples);
        char *restrict const f_asc = Numeric_to_char(p->cl_factor, 2);
        char *restrict const s_asc = Numeric_to_char(p->cl_samples, 0);
        char *restrict const m_asc = Numeric_to_char(m, 2);
        char *restrict const p_info = position_string(t, p);

        wout("%s: %s->%s: %s: Position open: timeout: %s %s %s\n",
             String_chars(w_ctx->ex->nm), String_chars(t->q_id),
             String_chars(t->b_id), String_chars(t->id), f_asc, s_asc, m_asc);

        wout("%s: %s->%s: %s: %s\n", String_chars(w_ctx->ex->nm),
             String_chars(t->q_id), String_chars(t->b_id), String_chars(p->id),
             p_info);

        if (order->msg && String_length(order->msg) > 0) {
          wout("%s: %s->%s: %s: %s\n", String_chars(w_ctx->ex->nm),
               String_chars(t->q_id), String_chars(t->b_id),
               String_chars(p->id), String_chars(order->msg));
        }

        Numeric_char_free(f_asc);
        Numeric_char_free(s_asc);
        Numeric_char_free(m_asc);
        heap_free(p_info);
      }
    } else if (order->status == ORDER_STATUS_FAILED ||
               order->status == ORDER_STATUS_CANCELLED ||
               order->status == ORDER_STATUS_EXPIRED) {
      if (verbose) {
        char *restrict const p_info = position_string(t, p);
        wout("%s: %s->%s: %s: Position failed, cancelled or expired\n",
             String_chars(w_ctx->ex->nm), String_chars(t->q_id),
             String_chars(t->b_id), String_chars(t->id));

        wout("%s: %s->%s: %s: %s\n", String_chars(w_ctx->ex->nm),
             String_chars(t->q_id), String_chars(t->b_id), String_chars(p->id),
             p_info);

        if (order->msg && String_length(order->msg) > 0) {
          wout("%s: %s->%s: %s: %s\n", String_chars(w_ctx->ex->nm),
               String_chars(t->q_id), String_chars(t->b_id),
               String_chars(p->id), String_chars(order->msg));
        }
        heap_free(p_info);
      }

      position_cancel(w_ctx, t, p);
      goto free;
    } else if (order->status == ORDER_STATUS_PENDING ||
               order->status == ORDER_STATUS_QUEUED ||
               order->status == ORDER_STATUS_CANCEL_QUEUED) {
      position_timeout(w_ctx, t, p, samples, sample);

      if (verbose) {
        char *restrict const p_info = position_string(t, p);
        wout("%s: %s->%s: %s: Position pending or queued\n",
             String_chars(w_ctx->ex->nm), String_chars(t->q_id),
             String_chars(t->b_id), String_chars(t->id));

        wout("%s: %s->%s: %s: %s\n", String_chars(w_ctx->ex->nm),
             String_chars(t->q_id), String_chars(t->b_id), String_chars(p->id),
             p_info);

        if (order->msg && String_length(order->msg) > 0) {
          wout("%s: %s->%s: %s: %s\n", String_chars(w_ctx->ex->nm),
               String_chars(t->q_id), String_chars(t->b_id),
               String_chars(t->id), String_chars(order->msg));
        }

        heap_free(p_info);
      }
      goto free;
    } else {
      werr("%s: %s->%s: %s: %u: Position status unknown\n",
           String_chars(w_ctx->ex->nm), String_chars(t->q_id),
           String_chars(t->b_id), String_chars(t->id), order->status);

      position_timeout(w_ctx, t, p, samples, sample);
      goto free;
    }

    // Recheck after reload.
    if (cancel && p->id != NULL && !(p->done || p->filled)) {
      const bool cancelled = w_ctx->ex->cancel(p->id);

      if (!cancelled) {
        char *restrict const p_info = position_string(t, p);

        werr("%s: %s->%s: %s: Failure cancelling position\n",
             String_chars(w_ctx->ex->nm), String_chars(t->q_id),
             String_chars(t->b_id), String_chars(t->id));

        werr("%s: %s->%s: %s: %s\n", String_chars(w_ctx->ex->nm),
             String_chars(t->q_id), String_chars(t->b_id), String_chars(p->id),
             p_info);

        heap_free(p_info);
      } else if (verbose) {
        char *restrict const p_info = position_string(t, p);

        wout("%s: %s->%s: %s: Position cancelled\n",
             String_chars(w_ctx->ex->nm), String_chars(t->q_id),
             String_chars(t->b_id), String_chars(t->id));

        wout("%s: %s->%s: %s: %s\n", String_chars(w_ctx->ex->nm),
             String_chars(t->q_id), String_chars(t->b_id), String_chars(p->id),
             p_info);

        heap_free(p_info);
      }

      position_timeout(w_ctx, t, p, samples, sample);
    }
  free:
    if (free_order)
      Order_delete(order);
  }
}

static void position_trigger(const struct worker_ctx *restrict const w_ctx,
                             struct Trade *restrict const t,
                             struct Position *restrict const p,
                             const struct Array *restrict const samples,
                             const struct Sample *restrict const sample) {
  const struct abag_tls *restrict const tls = abag_tls();
  struct Numeric *restrict const sr = tls->position_trigger.sr;
  struct Numeric *restrict const age = tls->position_trigger.age;
  bool cancel = false;
  bool sl = false;
  bool tp = false;

  switch (p->side) {
  case POSITION_SIDE_BUY:
    if (Numeric_cmp(sample->price, p->sl_price) >= 0)
      sl = true;

    if (Numeric_cmp(sample->price, p->tp_price) >= 0)
      tp = true;

    break;
  case POSITION_SIDE_SELL:
    if (Numeric_cmp(sample->price, p->sl_price) <= 0)
      sl = true;

    if (Numeric_cmp(sample->price, p->tp_price) <= 0)
      tp = true;

    break;
  default:
    werr("%s: %d: %s: Position neither buy nor sell\n", __FILE__, __LINE__,
         __func__);
    fatal();
  }

  Numeric_sub_to(sample->nanos, p->cnanos, age);
  samples_per_minute(sr, samples);

  if ((w_ctx->p_cnf->sr_min != NULL &&
       Numeric_cmp(sr, w_ctx->p_cnf->sr_min) < 0)) {
    cancel = true;

    if (verbose && !p->tl_trg.set) {
      char *restrict const sr_asc = Numeric_to_char(sr, 2);
      char *restrict const sr_min_asc =
          Numeric_to_char(w_ctx->p_cnf->sr_min, 2);

      wout("%s: %s->%s: %s: Tick rate %s ticks/minute lower than tick-rate-min "
           "%s ticks/minute\n",
           String_chars(w_ctx->ex->nm), String_chars(t->q_id),
           String_chars(t->b_id), String_chars(t->id), sr_asc, sr_min_asc);

      Numeric_char_free(sr_asc);
      Numeric_char_free(sr_min_asc);
    }
  }

  if ((w_ctx->p_cnf->sr_max != NULL &&
       Numeric_cmp(sr, w_ctx->p_cnf->sr_max) > 0)) {
    cancel = true;

    if (verbose && !p->tl_trg.set) {
      char *restrict const sr_asc = Numeric_to_char(sr, 2);
      char *restrict const sr_max_asc =
          Numeric_to_char(w_ctx->p_cnf->sr_max, 2);

      wout("%s: %s->%s: %s: Tick rate %s ticks/minute greater than "
           "tick-rate-max %s ticks/minute\n",
           String_chars(w_ctx->ex->nm), String_chars(t->q_id),
           String_chars(t->b_id), String_chars(t->id), sr_asc, sr_max_asc);

      Numeric_char_free(sr_asc);
      Numeric_char_free(sr_max_asc);
    }
  }

  if ((w_ctx->p_cnf->tl_dlnanos != NULL &&
       Numeric_cmp(age, w_ctx->p_cnf->tl_dlnanos) > 0)) {
    cancel = true;

    if (verbose && !p->tl_trg.set) {
      wout("%s: %s->%s: %s: Age greater than take-loss-delay\n",
           String_chars(w_ctx->ex->nm), String_chars(t->q_id),
           String_chars(t->b_id), String_chars(t->id));
    }
  }

  if (t->a->position_close(w_ctx->db, w_ctx->ex, t, p))
    cancel = true;

  if (sl) {
    if (!p->sl_trg.set) {
      p->sl_trg.set = true;
      p->sl_trg.cnt++;
      Numeric_copy_to(sample->nanos, p->sl_trg.nanos);

      if (verbose)
        wout("%s: %s->%s: %s: Entering stop loss(%" PRIuMAX ")\n",
             String_chars(w_ctx->ex->nm), String_chars(t->q_id),
             String_chars(t->b_id), String_chars(t->id), p->sl_trg.cnt);
    }
  } else if (p->sl_trg.set) {
    p->sl_trg.set = false;
    Numeric_copy_to(zero, p->sl_trg.nanos);

    if (verbose)
      wout("%s: %s->%s: %s: Leaving stop loss(%" PRIuMAX ")\n",
           String_chars(w_ctx->ex->nm), String_chars(t->q_id),
           String_chars(t->b_id), String_chars(t->id), p->sl_trg.cnt);
  }

  if (tp) {
    if (!p->tp_trg.set) {
      p->tp_trg.set = true;
      p->tp_trg.cnt++;
      Numeric_copy_to(sample->nanos, p->tp_trg.nanos);

      if (verbose)
        wout("%s: %s->%s: %s: Entering take profit(%" PRIuMAX ")\n",
             String_chars(w_ctx->ex->nm), String_chars(t->q_id),
             String_chars(t->b_id), String_chars(t->id), p->tp_trg.cnt);
    }
  } else if (p->tp_trg.set) {
    p->tp_trg.set = false;
    Numeric_copy_to(zero, p->tp_trg.nanos);

    if (p->sl_trg.set)
      Numeric_copy_to(sample->nanos, p->sl_trg.nanos);

    if (verbose)
      wout("%s: %s->%s: %s: Leaving take profit(%" PRIuMAX ")\n",
           String_chars(w_ctx->ex->nm), String_chars(t->q_id),
           String_chars(t->b_id), String_chars(t->id), p->tp_trg.cnt);
  }

  if (cancel) {
    if (!p->tl_trg.set) {
      p->tl_trg.set = true;
      p->tl_trg.cnt++;
      Numeric_copy_to(sample->nanos, p->tl_trg.nanos);

      if (verbose)
        wout("%s: %s->%s: %s: Entering take loss(%" PRIuMAX ")\n",
             String_chars(w_ctx->ex->nm), String_chars(t->q_id),
             String_chars(t->b_id), String_chars(t->id), p->tl_trg.cnt);
    }
  } else if (p->tl_trg.set) {
    p->tl_trg.set = false;
    Numeric_copy_to(zero, p->tl_trg.nanos);

    if (p->sl_trg.set)
      Numeric_copy_to(sample->nanos, p->sl_trg.nanos);

    if (p->tp_trg.set)
      Numeric_copy_to(sample->nanos, p->tp_trg.nanos);

    if (verbose)
      wout("%s: %s->%s: %s: Leaving take loss (%" PRIuMAX ")\n",
           String_chars(w_ctx->ex->nm), String_chars(t->q_id),
           String_chars(t->b_id), String_chars(t->id), p->tl_trg.cnt);
  }
}

static void position_trade(const struct worker_ctx *restrict const w_ctx,
                           struct Trade *restrict const t,
                           struct Position *restrict const p,
                           const struct Array *restrict const samples,
                           const struct Sample *restrict const sample) {
  const struct abag_tls *restrict const tls = abag_tls();
  struct Numeric *restrict const o_pr = tls->position_trade.o_pr;
  struct Numeric *restrict const r0 = tls->position_trade.r0;
  void **items;

  Numeric_copy_to(sample->price, o_pr);
  Numeric_sub_to(p->sl_samples, one, r0);
  Numeric_copy_to(r0, p->sl_samples);

  position_pricing(w_ctx, t, p, false);
  position_trigger(w_ctx, t, p, samples, sample);

  const char *restrict tr_info;
  const struct Numeric *restrict tr_nanos;
  if (p->tp_trg.set) {
    tr_info = "take profit";
    tr_nanos = p->tp_trg.nanos;
  } else if (p->sl_trg.set &&
             (Numeric_cmp(p->sl_samples, zero) <= 0 || p->tp_trg.cnt > 0)) {
    tr_info = "stop loss";
    tr_nanos = p->sl_trg.nanos;
  } else if (p->tl_trg.set) {
    tr_info = "take loss";
    tr_nanos = p->tl_trg.nanos;
  } else
    return;

  const char *restrict ac_info;
  switch (p->side) {
  case POSITION_SIDE_BUY:
    ac_info = "Selling";
    // Long: Sell at highest price since trigger.
    items = Array_items(samples);
    for (size_t i = Array_size(samples); i > 0; i--) {
      const struct Sample *restrict const s = items[i - 1];
      if (Numeric_cmp(s->nanos, tr_nanos) > 0 &&
          Numeric_cmp(s->price, o_pr) > 0)
        Numeric_copy_to(s->price, o_pr);
    }
    break;
  case POSITION_SIDE_SELL:
    ac_info = "Buying";
    // Short: Buy at lowest price since trigger.
    items = Array_items(samples);
    for (size_t i = Array_size(samples); i > 0; i--) {
      const struct Sample *restrict const s = items[i - 1];
      if (Numeric_cmp(s->nanos, tr_nanos) > 0 &&
          Numeric_cmp(s->price, o_pr) < 0)
        Numeric_copy_to(s->price, o_pr);
    }
    break;
  default:
    werr("%s: %d: %s: Position neither buy nor sell\n", __FILE__, __LINE__,
         __func__);
    fatal();
  }

  if (Numeric_cmp(sample->price, o_pr) == 0)
    return;

  Numeric_scale(o_pr, t->p_sc);

  struct Account *restrict const qa = w_ctx->ex->account(t->qa_id);

  if (qa == NULL) {
    werr("%s: %s->%s: %s: Failure syncing quote account\n",
         String_chars(w_ctx->ex->nm), String_chars(t->q_id),
         String_chars(t->b_id), String_chars(t->qa_id));

    return;
  }

  struct Account *restrict const ba = w_ctx->ex->account(t->ba_id);

  if (ba == NULL) {
    werr("%s: %s->%s: %s: Failure syncing base account\n",
         String_chars(w_ctx->ex->nm), String_chars(t->q_id),
         String_chars(t->b_id), String_chars(t->ba_id));

    Account_delete(qa);
    return;
  }

  if (!(qa->is_active && qa->is_ready) || !(ba->is_active && ba->is_ready)) {
    Account_delete(qa);
    Account_delete(ba);
    return;
  }

  Account_delete(qa);
  Account_delete(ba);

  char *restrict const b = Numeric_to_char(p->b_filled, t->b_sc);
  char *restrict const pr = Numeric_to_char(o_pr, t->p_sc);
  char *restrict const p_info = position_string(t, p);

  wout("%s: %s->%s: %s %s: %s%s@%s%s\n", String_chars(w_ctx->ex->nm),
       String_chars(t->q_id), String_chars(t->b_id), ac_info, tr_info, b,
       String_chars(t->b_id), pr, String_chars(t->q_id));

  wout("%s: %s->%s: %s\n", String_chars(w_ctx->ex->nm), String_chars(t->q_id),
       String_chars(t->b_id), p_info);

  struct String *restrict o_id;
  switch (p->side) {
  case POSITION_SIDE_BUY:
    o_id = w_ctx->ex->sell(t->p_id, b, pr);
    if (o_id == NULL) {
      werr("%s: %s->%s: %s: Failure posting position\n",
           String_chars(w_ctx->ex->nm), String_chars(t->q_id),
           String_chars(t->b_id), String_chars(t->id));

      goto ret;
    }
    t->p_short.id = o_id;
    Numeric_copy_to(o_pr, t->p_short.price);
    Numeric_copy_to(p->b_filled, t->p_short.b_ordered);
    position_create(w_ctx, t, &t->p_short);
    position_timeout(w_ctx, t, &t->p_short, samples, sample);
    break;
  case POSITION_SIDE_SELL:
    o_id = w_ctx->ex->buy(t->p_id, b, pr);
    if (o_id == NULL) {
      werr("%s: %s->%s: %s: Failure posting position\n",
           String_chars(w_ctx->ex->nm), String_chars(t->q_id),
           String_chars(t->b_id), String_chars(t->id));

      goto ret;
    }
    t->p_long.id = o_id;
    Numeric_copy_to(o_pr, t->p_long.price);
    Numeric_copy_to(p->b_filled, t->p_long.b_ordered);
    position_create(w_ctx, t, &t->p_long);
    position_timeout(w_ctx, t, &t->p_long, samples, sample);
    break;
  default:
    werr("%s: %d: %s: Position neither buy nor sell\n", __FILE__, __LINE__,
         __func__);
    fatal();
  }

ret:
  Numeric_char_free(b);
  Numeric_char_free(pr);
  heap_free(p_info);
}

static const struct {
  char *restrict const dbname;
  enum trade_status status;
} db_trade_status[] = {{"BUYING", TRADE_STATUS_BUYING},
                       {"BOUGHT", TRADE_STATUS_BOUGHT},
                       {"SELLING", TRADE_STATUS_SELLING},
                       {"SOLD", TRADE_STATUS_SOLD}};

static inline enum trade_status
trade_status(const char *restrict const dbname) {
  for (size_t i = nitems(db_trade_status); i > 0; i--)
    if (!strcmp(db_trade_status[i - 1].dbname, dbname))
      return db_trade_status[i - 1].status;

  werr("%s: %d: %s: %s: Unsupported trade status\n", __FILE__, __LINE__,
       __func__, dbname);
  fatal();
}

static void trade_create(const struct worker_ctx *restrict const w_ctx,
                         struct Trade *restrict const t,
                         const struct Array *restrict const samples,
                         const struct Sample *restrict const sample) {
  const struct abag_tls *restrict const tls = abag_tls();
  struct db_stats_res *restrict const stats = tls->trade_create.stats;

  if (db_stats(stats, w_ctx->db, String_chars(w_ctx->ex->id),
               String_chars(t->p_id))) {

    Numeric_copy_to(stats->bcl_factor, t->p_long.cl_factor);
    Numeric_copy_to(stats->scl_factor, t->p_short.cl_factor);
  } else {
    Numeric_copy_to(one, t->p_long.cl_factor);
    Numeric_copy_to(one, t->p_short.cl_factor);
  }

  // XXX: Calls db_stats(...) again.
  position_timeout(w_ctx, t, &t->p_long, samples, sample);
  position_timeout(w_ctx, t, &t->p_short, samples, sample);
}

static void trade_pricing(const struct worker_ctx *restrict const w_ctx,
                          struct Trade *restrict const t) {
  const struct abag_tls *restrict const tls = abag_tls();
  struct Numeric *restrict const r0 = tls->trade_pricing.r0;
  const struct Pricing *restrict const pricing = w_ctx->ex->pricing();

  if (Numeric_cmp(t->fee_pc, pricing->ef_pc) != 0) {
    if (verbose) {
      char *restrict const pr = Numeric_to_char(pricing->ef_pc, 2);
      wout("%s: %s->%s: Pricing: %s\n", String_chars(w_ctx->ex->nm),
           String_chars(t->q_id), String_chars(t->b_id), pr);

      Numeric_char_free(pr);
    }

    Numeric_copy_to(pricing->ef_pc, t->fee_pc);
    Numeric_div_to(t->fee_pc, hundred, r0);
    Numeric_add_to(r0, one, t->fee_pf);

    Numeric_copy_to(w_ctx->p_cnf->v_pc != NULL ? w_ctx->p_cnf->v_pc
                                               : pricing->ef_pc,
                    t->tp_pc);

    Numeric_div_to(t->tp_pc, hundred, r0);
    Numeric_add_to(r0, one, t->tp_pf);
  }
}

static void trade_plot(const struct worker_ctx *restrict const w_ctx,
                       struct Trade *restrict const t) {
  char plot_fn[4096] = {0};
  int r = snprintf(plot_fn, sizeof(plot_fn), "%s/%s.m",
                   String_chars(cnf->plts_dir), String_chars(w_ctx->p->nm));

  if (r < 0 || (size_t)r >= sizeof(plot_fn)) {
    werr("%s: %d: %s: Plot filename exceeds %zu characters\n", __FILE__,
         __LINE__, __func__, sizeof(plot_fn));
    fatal();
  }

  t->a->product_plot(plot_fn, w_ctx->db, w_ctx->ex, w_ctx->p);
}

static void trade_bet(const struct worker_ctx *restrict const w_ctx,
                      struct Trade *restrict const t,
                      const struct Array *restrict const samples,
                      const struct Sample *restrict const sample) {
  const struct abag_tls *restrict const tls = abag_tls();
  struct Numeric *restrict const b_avail = tls->trade_bet.b_avail;
  struct Numeric *restrict const q_avail = tls->trade_bet.q_avail;
  struct Numeric *restrict const q_costs = tls->trade_bet.q_costs;
  struct Numeric *restrict const r0 = tls->trade_bet.r0;
  struct db_balance_res *restrict const hold = tls->trade_bet.hold;
  bool pr_changed = false;

  Map_lock(product_prices);
  struct Numeric *restrict pr_last = Map_get(product_prices, w_ctx->p->id);
  if (pr_last == NULL) {
    pr_last = Numeric_copy(zero);
    Map_put(product_prices, w_ctx->p->id, pr_last);
  }
  if (Numeric_cmp(pr_last, sample->price)) {
    Numeric_copy_to(sample->price, pr_last);
    pr_changed = true;
  }
  Map_unlock(product_prices);

  if (!pr_changed)
    return;

  struct Position *restrict const p =
      t->a->position_select(w_ctx->db, w_ctx->ex, t, samples, sample);

  if (p != NULL) {
    if (!t->bet_trg.set) {
      t->bet_trg.set = true;
      t->bet_trg.cnt++;
      Numeric_copy_to(sample->nanos, t->bet_trg.nanos);

      if (verbose)
        wout("%s: %s->%s: Entering bet(%" PRIuMAX ")\n",
             String_chars(w_ctx->ex->nm), String_chars(t->q_id),
             String_chars(t->b_id), t->bet_trg.cnt);
    }
  } else if (t->bet_trg.set) {
    t->bet_trg.set = false;
    Numeric_copy_to(zero, t->bet_trg.nanos);

    if (verbose)
      wout("%s: %s->%s: Leaving bet(%" PRIuMAX ")\n",
           String_chars(w_ctx->ex->nm), String_chars(t->q_id),
           String_chars(t->b_id), t->bet_trg.cnt);
  }

  if (!t->bet_trg.set)
    return;

  if (cnf->plts_dir != NULL)
    trade_plot(w_ctx, t);

  struct Account *restrict const q_acct = w_ctx->ex->account(t->qa_id);

  if (q_acct == NULL) {
    werr("%s: %s->%s: %s: Failure syncing quote account\n",
         String_chars(w_ctx->ex->nm), String_chars(t->q_id),
         String_chars(t->b_id), String_chars(t->qa_id));

    return;
  }

  struct Account *restrict const b_acct = w_ctx->ex->account(t->ba_id);

  if (b_acct == NULL) {
    werr("%s: %s->%s: %s: Failure syncing base account\n",
         String_chars(w_ctx->ex->nm), String_chars(t->q_id),
         String_chars(t->b_id), String_chars(t->ba_id));

    Account_delete(q_acct);
    return;
  }

  if (!(q_acct->is_active && q_acct->is_ready) ||
      !(b_acct->is_active && b_acct->is_ready)) {

    if (verbose)
      wout("%s: %s->%s: Leaving bet(%" PRIuMAX ")\n",
           String_chars(w_ctx->ex->nm), String_chars(t->q_id),
           String_chars(t->b_id), t->bet_trg.cnt);

    Account_delete(q_acct);
    Account_delete(b_acct);
    trigger_reset(&t->bet_trg);
    return;
  }

  position_pricing(w_ctx, t, p, true);

  db_trades_hold(hold, w_ctx->db, String_chars(w_ctx->ex->id),
                 String_chars(t->q_id), String_chars(t->b_id));

  Numeric_sub_to(b_acct->avail, hold->b, b_avail);
  Numeric_sub_to(q_acct->avail, hold->q, q_avail);

  Account_delete(q_acct);
  Account_delete(b_acct);

  /*
   * Quote accounts are debited with fees charged for orders.
   * This makes it impossible to predict exact costs for open positions,
   * because the price the position will get closed at is not known. Reserve
   * two times the current fee + 10% of the currently available balance
   * to ensure fees can always get paid - that is - positions can always get
   * closed without running out of funds.
   */
  Numeric_mul_to(two, p->q_fees, q_costs);
  Numeric_sub_to(q_avail, q_costs, r0);
  Numeric_mul_to(r0, ninety_percent_factor, q_avail);

  char *restrict const c =
      candle_string(&t->bet_cd, String_chars(t->q_id), t->p_sc);

  char *restrict const b = Numeric_to_char(p->b_ordered, t->b_sc);
  char *restrict const pr = Numeric_to_char(p->price, t->p_sc);

  switch (p->side) {
  case POSITION_SIDE_BUY: {
    if (Numeric_cmp(q_avail, p->q_ordered) < 0) {
      if (verbose) {
        char *restrict const r = Numeric_to_char(p->q_ordered, t->q_sc);
        char *restrict const a = Numeric_to_char(q_avail, t->q_sc);

        wout("%s: %s->%s: Out of funds betting long: quote required: %s%s, "
             "quote available: %s%s, candle: %s\n",
             String_chars(w_ctx->ex->nm), String_chars(t->q_id),
             String_chars(t->b_id), r, String_chars(t->q_id), a,
             String_chars(t->q_id), c);

        Numeric_char_free(r);
        Numeric_char_free(a);

        wout("%s: %s->%s: Leaving bet(%" PRIuMAX ")\n",
             String_chars(w_ctx->ex->nm), String_chars(t->q_id),
             String_chars(t->b_id), t->bet_trg.cnt);
      }

      trigger_reset(&t->bet_trg);
      goto ret;
    }

    wout("%s: %s->%s: Requesting %s%s@%s%s: %s\n", String_chars(w_ctx->ex->nm),
         String_chars(t->q_id), String_chars(t->b_id), b, String_chars(t->b_id),
         pr, String_chars(t->q_id), c);

    struct String *restrict const o_id = w_ctx->ex->buy(t->p_id, b, pr);

    if (o_id == NULL) {
      werr("%s: %s->%s: Failure posting buy order\n",
           String_chars(w_ctx->ex->nm), String_chars(t->q_id),
           String_chars(t->b_id));

      goto ret;
    }

    p->id = o_id;
    break;
  }
  case POSITION_SIDE_SELL: {
    if (Numeric_cmp(q_avail, p->q_fees) < 0 ||
        Numeric_cmp(b_avail, p->b_ordered) < 0) {

      if (verbose) {
        char *restrict const qr = Numeric_to_char(p->q_fees, t->q_sc);
        char *restrict const qa = Numeric_to_char(q_avail, t->q_sc);
        char *restrict const ba = Numeric_to_char(b_avail, t->b_sc);

        wout("%s: %s->%s: Out of funds betting short: quote required: %s%s, "
             "quote available: %s%s, base required: %s%s, base available: "
             "%s%s, candle: %s\n",
             String_chars(w_ctx->ex->nm), String_chars(t->q_id),
             String_chars(t->b_id), qr, String_chars(t->q_id), qa,
             String_chars(t->q_id), b, String_chars(t->b_id), ba,
             String_chars(t->b_id), c);

        Numeric_char_free(qr);
        Numeric_char_free(qa);
        Numeric_char_free(ba);

        wout("%s: %s->%s: Leaving bet(%" PRIuMAX ")\n",
             String_chars(w_ctx->ex->nm), String_chars(t->q_id),
             String_chars(t->b_id), t->bet_trg.cnt);
      }

      trigger_reset(&t->bet_trg);
      goto ret;
    }

    wout("%s: %s->%s: Offering %s%s@%s%s: %s\n", String_chars(w_ctx->ex->nm),
         String_chars(t->q_id), String_chars(t->b_id), b, String_chars(t->b_id),
         pr, String_chars(t->q_id), c);

    struct String *restrict const o_id = w_ctx->ex->sell(t->p_id, b, pr);

    if (o_id == NULL) {
      werr("%s: %s->%s: Failure posting sell order\n",
           String_chars(w_ctx->ex->nm), String_chars(t->q_id),
           String_chars(t->b_id));

      goto ret;
    }

    p->id = o_id;
    break;
  }
  default:
    werr("%s: %d: %s: Position neither buy nor sell\n", __FILE__, __LINE__,
         __func__);
    fatal();
  }

  // Try longer or shorter than average based on angle (1rad approx. 57.3dec).
  Numeric_mul_to(p->cl_factor, t->bet_cd.a, r0);
  Numeric_copy_to(r0, p->cl_factor);
  position_create(w_ctx, t, p);
  position_timeout(w_ctx, t, p, samples, sample);
ret:
  Numeric_char_free(b);
  Numeric_char_free(pr);
  heap_free(c);
}

static void trade_maintain(const struct worker_ctx *restrict const w_ctx,
                           struct Trade *restrict const t,
                           const struct Array *restrict const samples,
                           const struct Sample *restrict const sample) {
  const struct abag_tls *restrict const tls = abag_tls();
  struct Numeric *restrict const q_delta = tls->trade_maintain.q_delta;
  struct Numeric *restrict const q_costs = tls->trade_maintain.q_costs;
  struct Numeric *restrict const q_profit = tls->trade_maintain.q_profit;
  const enum trade_status st = t->status;

  switch (t->status) {
  case TRADE_STATUS_BUYING:
    position_maintain(w_ctx, t, &t->p_long, samples, sample, NULL);
    break;
  case TRADE_STATUS_SELLING:
    position_maintain(w_ctx, t, &t->p_short, samples, sample, NULL);
    break;
  case TRADE_STATUS_BOUGHT:
    position_trade(w_ctx, t, &t->p_long, samples, sample);
    break;
  case TRADE_STATUS_SOLD:
    position_trade(w_ctx, t, &t->p_short, samples, sample);
    break;
  case TRADE_STATUS_NEW:
    trade_bet(w_ctx, t, samples, sample);
    break;
  case TRADE_STATUS_DONE:
    Numeric_add_to(t->p_long.q_fees, t->p_short.q_fees, q_costs);
    Numeric_sub_to(t->p_short.q_filled, t->p_long.q_filled, q_delta);
    Numeric_sub_to(q_delta, q_costs, q_profit);
    char *restrict const l_p = Numeric_to_char(t->p_long.price, t->p_sc);
    char *restrict const s_p = Numeric_to_char(t->p_short.price, t->p_sc);
    char *restrict const profit = Numeric_to_char(q_profit, t->q_sc);
    char *restrict const costs = Numeric_to_char(q_costs, t->q_sc);
    char *restrict const l_b = Numeric_to_char(t->p_long.b_filled, t->b_sc);
    char *restrict const s_b = Numeric_to_char(t->p_short.b_filled, t->b_sc);
    char *restrict const b_info = position_string(t, &t->p_long);
    char *restrict const q_info = position_string(t, &t->p_short);

    wout("%s: %s->%s: %s: Trade done: %s%s@%s%s -> %s%s@%s%s, profit: %s%s, "
         "costs: %s%s\n",
         String_chars(w_ctx->ex->nm), String_chars(t->q_id),
         String_chars(t->b_id), String_chars(t->id), l_b, String_chars(t->b_id),
         l_p, String_chars(t->q_id), s_b, String_chars(t->b_id), s_p,
         String_chars(t->q_id), profit, String_chars(t->q_id), costs,
         String_chars(t->q_id));

    wout("%s: %s->%s: %s: %s\n", String_chars(w_ctx->ex->nm),
         String_chars(t->q_id), String_chars(t->b_id),
         String_chars(t->p_long.id), b_info);

    wout("%s: %s->%s: %s: %s\n", String_chars(w_ctx->ex->nm),
         String_chars(t->q_id), String_chars(t->b_id),
         String_chars(t->p_short.id), q_info);

    Numeric_char_free(l_p);
    Numeric_char_free(l_b);
    Numeric_char_free(s_p);
    Numeric_char_free(s_b);
    Numeric_char_free(profit);
    Numeric_char_free(costs);
    heap_free(b_info);
    heap_free(q_info);
    break;
  case TRADE_STATUS_CANCELLED:
    break;
  default:
    werr("%s: %d: %s: %u: Unsupported trade status", __FILE__, __LINE__,
         __func__, t->status);
    fatal();
  }

  if (t->status != st)
    trade_maintain(w_ctx, t, samples, sample);
}

static struct Array *trades_load(const struct worker_ctx *w_ctx,
                                 const struct Array *restrict const samples,
                                 const struct Sample *restrict const sample) {
  const struct abag_tls *restrict const tls = abag_tls();
  struct db_trade_res *restrict const trade = tls->trades_load.trade;
  void **items;

  db_trades_open(w_ctx->db, String_chars(w_ctx->ex->id),
                 String_chars(w_ctx->p->id));

  struct Array *restrict const trades = Array_new(128);

  while (db_trades_next(trade, w_ctx->db)) {
    struct Trade *restrict const t =
        trade_new(String_copy(w_ctx->ex->id), String_copy(w_ctx->p->id),
                  String_copy(w_ctx->p->b_id), String_copy(w_ctx->p->q_id),
                  String_copy(w_ctx->p->ba_id), String_copy(w_ctx->p->qa_id),
                  w_ctx->p->b_sc, w_ctx->p->q_sc, w_ctx->p->p_sc);

    t->id = String_new(trade->id);

    if (verbose)
      wout("%s: %s->%s: %s: Resuming trade\n", String_chars(w_ctx->ex->nm),
           String_chars(w_ctx->p->q_id), String_chars(w_ctx->p->b_id),
           String_chars(t->id));

    t->p_long.id = trade->bo_id_null ? NULL : String_new(trade->bo_id);

    if (!trade->b_cnanos_null)
      Numeric_copy_to(trade->b_cnanos, t->p_long.cnanos);
    else
      nanos_now(t->p_long.cnanos);

    if (!trade->b_dnanos_null) {
      Numeric_copy_to(trade->b_dnanos, t->p_long.dnanos);
      t->p_long.done = true;
    } else
      t->p_long.done = false;

    if (!trade->b_price_null)
      Numeric_copy_to(trade->b_price, t->p_long.price);

    if (!trade->b_b_ordered_null)
      Numeric_copy_to(trade->b_b_ordered, t->p_long.b_ordered);

    if (!trade->b_b_filled_null) {
      Numeric_copy_to(trade->b_b_filled, t->p_long.b_filled);
      t->p_long.filled = Numeric_cmp(zero, t->p_long.b_filled) != 0;
    } else
      t->p_long.filled = false;

    if (!trade->b_q_fees_null)
      Numeric_copy_to(trade->b_q_fees, t->p_long.q_fees);

    if (!trade->b_q_filled_null)
      Numeric_copy_to(trade->b_q_filled, t->p_long.q_filled);

    t->p_short.id = trade->so_id_null ? NULL : String_new(trade->so_id);

    if (!trade->s_cnanos_null)
      Numeric_copy_to(trade->s_cnanos, t->p_short.cnanos);
    else
      nanos_now(t->p_short.cnanos);

    if (!trade->s_dnanos_null) {
      Numeric_copy_to(trade->s_dnanos, t->p_short.dnanos);
      t->p_short.done = true;
    } else
      t->p_short.done = false;

    if (!trade->s_price_null)
      Numeric_copy_to(trade->s_price, t->p_short.price);

    if (!trade->s_b_ordered_null)
      Numeric_copy_to(trade->s_b_ordered, t->p_short.b_ordered);

    if (!trade->s_b_filled_null) {
      Numeric_copy_to(trade->s_b_filled, t->p_short.b_filled);
      t->p_short.filled = Numeric_cmp(zero, t->p_short.b_filled) != 0;
    } else
      t->p_short.filled = false;

    if (!trade->s_q_fees_null)
      Numeric_copy_to(trade->s_q_fees, t->p_short.q_fees);

    if (!trade->s_q_filled_null)
      Numeric_copy_to(trade->s_q_filled, t->p_short.q_filled);

    t->status = trade_status(trade->status);

    trade_pricing(w_ctx, t);

    if (t->p_long.id != NULL) {
      position_pricing(w_ctx, t, &t->p_long, false);

      if (verbose) {
        char *restrict const p_info = position_string(t, &t->p_long);

        wout("%s: %s->%s: %s: %s\n", String_chars(w_ctx->ex->nm),
             String_chars(t->q_id), String_chars(t->b_id),
             String_chars(t->p_long.id), p_info);

        heap_free(p_info);
      }
    }

    if (t->p_short.id != NULL) {
      position_pricing(w_ctx, t, &t->p_short, false);

      if (verbose) {
        char *restrict const p_info = position_string(t, &t->p_short);

        wout("%s: %s->%s: %s: %s\n", String_chars(w_ctx->ex->nm),
             String_chars(t->q_id), String_chars(t->b_id),
             String_chars(t->p_short.id), p_info);

        heap_free(p_info);
      }
    }

    Array_add_tail(trades, t);
  }

  db_trades_close(w_ctx->db);

  items = Array_items(trades);
  for (size_t i = Array_size(trades); i > 0; i--) {
    struct Trade *restrict const t = items[i - 1];
    trade_create(w_ctx, t, samples, sample);
    Numeric_copy_to(zero, t->p_long.rnanos);
    Numeric_copy_to(zero, t->p_short.rnanos);
  }

  return trades;
}

static int orders_process(void *restrict const arg) {
  const struct abag_tls *restrict const tls = abag_tls();
  struct Numeric *restrict const tp = tls->orders_process.tp;
  struct worker_ctx *restrict const w_ctx = arg;
  struct Trade *restrict t = NULL;
  struct Position *restrict p = NULL;
  struct Array *restrict trades = NULL;
  struct Array *restrict samples = NULL;
  size_t i;
  void **items;

  while (!terminated) {
    struct Order *restrict const order = w_ctx->ex->order_await();

    if (order == NULL)
      continue;

    struct Product *restrict const product = w_ctx->ex->product(order->p_id);

    if (product == NULL) {
      werr("%s: %d: %s: %s: Product not found\n", __FILE__, __LINE__, __func__,
           String_chars(order->p_id));
      fatal();
    }

    w_ctx->a = NULL;
    w_ctx->p = product;
    w_ctx->p_cnf = NULL;
    w_ctx->q_tgt = tp;

    Map_lock(product_samples);
    samples = Map_get(product_samples, order->p_id);
    Map_unlock(product_samples);

    if (samples == NULL || Array_size(samples) < 2) {
      Order_delete(order);
      Product_delete(w_ctx->p);
      continue;
    }

    Array_lock(samples);
    worker_config(w_ctx, samples);
    Array_unlock(samples);

    if (!(w_ctx->p_cnf != NULL && w_ctx->q_tgt != NULL &&
          w_ctx->p->is_active)) {
      Product_delete(w_ctx->p);
      Order_delete(order);
      continue;
    }

    Map_lock(product_trades);
    trades = Map_get(product_trades, order->p_id);
    Map_unlock(product_trades);

    if (trades == NULL) {
      Order_delete(order);
      Product_delete(w_ctx->p);
      continue;
    }

    Array_lock(trades);
    items = Array_items(trades);
    for (i = Array_size(trades); i > 0; i--) {
      struct Trade *restrict const trade = items[i - 1];

      if (trade->p_long.id != NULL &&
          String_equals(trade->p_long.id, order->id)) {
        t = trade;
        p = &trade->p_long;
        break;
      }
      if (trade->p_short.id != NULL &&
          String_equals(trade->p_short.id, order->id)) {
        t = trade;
        p = &trade->p_short;
        break;
      }
    }

    if (t != NULL) {
      if (t->status == TRADE_STATUS_BUYING ||
          t->status == TRADE_STATUS_SELLING) {
        Array_lock(samples);

        if (Array_size(samples) < 2) {
          Array_unlock(samples);
          Array_unlock(trades);
          Order_delete(order);
          Product_delete(w_ctx->p);
          continue;
        }

        t->a = w_ctx->a;
        t->a->configure(w_ctx->db, w_ctx->ex, t);
        Numeric_copy_to(w_ctx->q_tgt, t->tp);
        trade_pricing(w_ctx, t);
        position_maintain(w_ctx, t, p, samples, Array_tail(samples), order);
        trade_maintain(w_ctx, t, samples, Array_tail(samples));
        Array_unlock(samples);
      }

      if (t->status == TRADE_STATUS_CANCELLED ||
          t->status == TRADE_STATUS_DONE) {
        trade_delete(t);
        Array_remove_idx(trades, i - 1);
      }

      Array_unlock(trades);
      Order_delete(order);
      Product_delete(w_ctx->p);
    }
  }

  db_disconnect(w_ctx->db);
  heap_free(w_ctx);
  thread_exit(EXIT_SUCCESS);
}

static int samples_process(void *restrict const arg) {
  const struct abag_tls *restrict const tls = abag_tls();
  struct Numeric *restrict const outdated_ns = tls->samples_process.outdated_ns;
  struct Numeric *restrict const tp = tls->samples_process.tp;
  struct Numeric *restrict const nanos = tls->samples_process.nanos;
  struct worker_ctx *restrict const ctx = arg;
  void **items;

  while (!terminated) {
    struct Sample *restrict sample = ctx->ex->sample_await();

    if (sample == NULL)
      continue;

    struct Product *restrict const p = ctx->ex->product(sample->p_id);

    if (p == NULL) {
      werr("%s: %d: %s: %s: Product not found\n", __FILE__, __LINE__, __func__,
           String_chars(sample->p_id));
      fatal();
    }

    ctx->a = NULL;
    ctx->p = p;
    ctx->p_cnf = NULL;
    ctx->q_tgt = tp;

    mutex_lock(&global_mtx);
    db_sample_create(ctx->db, String_chars(ctx->ex->id),
                     String_chars(ctx->p->id), sample->nanos, sample->price);

    if (!ctx->p->is_tradeable) {
      mutex_unlock(&global_mtx);
      Sample_delete(sample);
      Product_delete(ctx->p);
      continue;
    }
    mutex_unlock(&global_mtx);

    Map_lock(product_samples);
    struct Array *restrict samples = Map_get(product_samples, ctx->p->id);
    if (samples == NULL) {
      samples = samples_load(ctx);
      Map_put(product_samples, ctx->p->id, samples);
    }
    Map_unlock(product_samples);

    Array_lock(samples);
    Array_add_tail(samples, sample);

    const size_t s_size = Array_size(samples);
    items = Array_items(samples);
    if (s_size >= ABAG_WORKERS) {
      qsort(&items[s_size - ABAG_WORKERS], ABAG_WORKERS,
            sizeof(struct Sample *), sample_cmp);

    } else {
      qsort(&items[0], s_size, sizeof(struct Sample *), sample_cmp);
    }

    if (Array_size(samples) < 2) {
      Array_unlock(samples);
      Product_delete(ctx->p);
      continue;
    }

    worker_config(ctx, samples);

    if (ctx->p_cnf != NULL) {
      struct Sample *restrict const oldest = Array_head(samples);
      struct Sample *restrict const youngest = Array_tail(samples);

      Numeric_sub_to(youngest->nanos, oldest->nanos, nanos);
      if (Numeric_cmp(nanos, ctx->p_cnf->wnanos) < 0) {
        Array_unlock(samples);
        Product_delete(ctx->p);
        continue;
      }
    }

    sample = Array_tail(samples);
    Numeric_sub_to(sample->nanos,
                   ctx->p_cnf != NULL ? ctx->p_cnf->wnanos : cnf->wnanos_max,
                   outdated_ns);

    while (Array_size(samples) > 0 &&
           Numeric_cmp(((struct Sample *)Array_head(samples))->nanos,
                       outdated_ns) < 0)
      Sample_delete(Array_remove_head(samples));

    Array_shrink(samples);
    Array_unlock(samples);

    if (!(ctx->p_cnf != NULL && ctx->q_tgt != NULL && ctx->p->is_active)) {
      Product_delete(ctx->p);
      continue;
    }

    Map_lock(product_trades);
    struct Array *restrict trades = Map_get(product_trades, ctx->p->id);
    if (trades == NULL) {
      trades = trades_load(ctx, samples, sample);
      Map_put(product_trades, ctx->p->id, trades);
    }
    Map_unlock(product_trades);
    Array_lock(trades);
    bool betting = false;
    bool pending = false;
  again:
    items = Array_items(trades);
    for (size_t i = Array_size(trades); i > 0; i--) {
      struct Trade *restrict const t = items[i - 1];
      t->a = ctx->a;
      t->a->configure(ctx->db, ctx->ex, t);
      Numeric_copy_to(ctx->q_tgt, t->tp);
      trade_pricing(ctx, t);

      Array_lock(samples);

      if (Array_size(samples) < 2) {
        Array_unlock(samples);
        Array_unlock(trades);
        Product_delete(ctx->p);
        continue;
      }

      trade_maintain(ctx, t, samples, Array_tail(samples));
      Array_unlock(samples);

      if (t->status == TRADE_STATUS_CANCELLED ||
          t->status == TRADE_STATUS_DONE) {
        trade_delete(t);
        Array_remove_idx(trades, i - 1);
        goto again;
      } else if (t->status == TRADE_STATUS_BUYING ||
                 t->status == TRADE_STATUS_SELLING) {
        pending = true;
      } else if (t->status == TRADE_STATUS_NEW)
        betting = true;
    }

    if (!(betting || pending)) {
      struct Trade *t =
          trade_new(String_copy(ctx->ex->id), String_copy(ctx->p->id),
                    String_copy(ctx->p->b_id), String_copy(ctx->p->q_id),
                    String_copy(ctx->p->ba_id), String_copy(ctx->p->qa_id),
                    ctx->p->b_sc, ctx->p->q_sc, ctx->p->p_sc);

      trade_create(ctx, t, samples, sample);
      Array_add_tail(trades, t);
    }

    Array_unlock(trades);
    Product_delete(ctx->p);
  }

  db_disconnect(ctx->db);
  heap_free(arg);
  thread_exit(EXIT_SUCCESS);
}

int abagnale(int argc, char *argv[]) {
  void **items;
  twenty_five_percent_factor = Numeric_from_char("1.25");
  ninety_percent_factor = Numeric_from_char("0.9");

  order_reload_interval_nanos =
      Numeric_from_long(ABAG_ORDER_RELOAD_INTERVAL_NANOS);

  boot_delay_nanos = Numeric_from_long(ABAG_BOOT_DELAY_NANOS);
  product_samples = Map_new(ABAG_MAX_PRODUCTS);
  product_prices = Map_new(ABAG_MAX_PRODUCTS);
  product_trades = Map_new(ABAG_MAX_PRODUCTS);

  mutex_init(&global_mtx);
  tls_create(&abag_tls_key, abag_tls_dtor);

  // ABAG_WORKERS * Array_size(exchanges) <= SIZE_MAX
  // => Array_size(exchanges) <= SIZE_MAX / ABAG_WORKERS
  // => ABAG_WORKERS <= SIZE_MAX / Array_size(exchanges)
  if (Array_size(exchanges) > SIZE_MAX / ABAG_WORKERS) {
    werr("%s: %d: %s: Number of exchanges exceeds limit of %zu exchanges\n",
         __FILE__, __LINE__, __func__, SIZE_MAX / ABAG_WORKERS);
    fatal();
  }
  if (ABAG_WORKERS > SIZE_MAX / Array_size(exchanges)) {
    werr("%s: %d: %s: Number of worker threads exceeds limit of %zu workers\n",
         __FILE__, __LINE__, __func__, SIZE_MAX / Array_size(exchanges));
    fatal();
  }

  workers = heap_calloc(ABAG_WORKERS * Array_size(exchanges), sizeof(thrd_t));

  items = Array_items(exchanges);
  for (size_t i = Array_size(exchanges); i > 0 && !terminated; i--) {
    const struct Exchange *restrict const e = items[i - 1];
    for (int j = 0; j < ABAG_WORKERS && !terminated; j++) {
      struct worker_ctx *restrict const w_ctx =
          heap_calloc(1, sizeof(struct worker_ctx));

      w_ctx->ex = e;

      const int r = snprintf(w_ctx->db, sizeof(w_ctx->db), "%s-worker-%.3d",
                             String_chars(e->nm), j);

      if (r < 0 || (size_t)r >= sizeof(w_ctx->db)) {
        werr("%s: %d: %s: Database connection name exceeds %zu characters\n",
             __FILE__, __LINE__, __func__, sizeof(w_ctx->db));
        fatal();
      }

      db_connect(w_ctx->db);

      if (j == 0)
        thread_create(&workers[(i - 1) * ABAG_WORKERS + j], orders_process,
                      w_ctx);
      else
        thread_create(&workers[(i - 1) * ABAG_WORKERS + j], samples_process,
                      w_ctx);
    }

    if (!terminated)
      e->start();
  }

  if (!terminated)
    for (size_t i = ABAG_WORKERS * Array_size(exchanges); i > 0; i--)
      thread_join(workers[i - 1], NULL);

  Numeric_delete(twenty_five_percent_factor);
  Numeric_delete(ninety_percent_factor);
  Numeric_delete(order_reload_interval_nanos);
  Numeric_delete(boot_delay_nanos);
  heap_free(workers);
  Map_delete(product_samples, sample_array_delete);
  Map_delete(product_prices, Numeric_delete);
  Map_delete(product_trades, trade_array_delete);
  mutex_destroy(&global_mtx);
  tls_delete(abag_tls_key);

  return EXIT_SUCCESS;
}
