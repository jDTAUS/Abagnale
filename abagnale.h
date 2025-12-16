/* $SchulteIT: abagnale.h 15260 2025-11-04 03:03:57Z schulte $ */
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

#ifndef ABAG_ABAGNALE_H
#define ABAG_ABAGNALE_H

#include "exchange.h"

#include <stdbool.h>
#include <stdint.h>

struct Algorithm;
struct Trade;

struct Trigger {
  bool set;
  uintmax_t cnt;
  struct Numeric *restrict nanos;
};

enum candle_trend {
  CANDLE_NONE = 1,
  CANDLE_UP,
  CANDLE_DOWN,
};

struct Candle {
  enum candle_trend t;
  struct Numeric *restrict o;
  struct Numeric *restrict h;
  struct Numeric *restrict l;
  struct Numeric *restrict c;
  struct Numeric *restrict pc;
  struct Numeric *restrict a;
  struct Numeric *restrict onanos;
  struct Numeric *restrict hnanos;
  struct Numeric *restrict lnanos;
  struct Numeric *restrict cnanos;
};

enum position_type {
  POSITION_TYPE_LONG = 1,
  POSITION_TYPE_SHORT,
};

struct Position {
  bool done;
  bool filled;
  enum position_type type;
  struct String *restrict id;
  struct Numeric *restrict cnanos;
  struct Numeric *restrict dnanos;
  struct Numeric *restrict rnanos;
  struct Numeric *restrict price;
  struct Numeric *restrict sl_price;
  struct Numeric *restrict tp_price;
  struct Numeric *restrict b_ordered;
  struct Numeric *restrict b_filled;
  struct Numeric *restrict q_fees;
  struct Numeric *restrict q_filled;
  struct Numeric *restrict cl_samples;
  struct Numeric *restrict cl_factor;
  struct Numeric *restrict sl_samples;
  struct Trigger sl_trg;
  struct Trigger tl_trg;
  struct Trigger tp_trg;
};

enum trade_status {
  TRADE_STATUS_NEW = 1,
  TRADE_STATUS_BUYING,
  TRADE_STATUS_BOUGHT,
  TRADE_STATUS_SELLING,
  TRADE_STATUS_SOLD,
  TRADE_STATUS_CANCELLED,
  TRADE_STATUS_DONE,
};

struct Trade {
  struct String *restrict id;
  struct String *restrict e_id;
  struct String *restrict p_id;
  struct String *restrict b_id;
  struct String *restrict q_id;
  struct String *restrict ba_id;
  struct String *restrict qa_id;
  size_t b_sc;
  size_t q_sc;
  size_t p_sc;
  enum trade_status status;
  struct Numeric *restrict tp;
  struct Numeric *restrict fee_pc;
  struct Numeric *restrict fee_pf;
  struct Numeric *restrict tp_pc;
  struct Numeric *restrict tp_pf;
  struct Position p_long;
  struct Position p_short;
  struct Candle open_cd;
  struct Trigger open_trg;
  const struct Algorithm *restrict a;
};

struct Algorithm {
  struct String *restrict id;
  struct String *restrict nm;
  void (*init)(void);
  void (*destroy)(void);
  struct Position *(*position_open)(const char *restrict const,
                                    const struct Exchange *restrict const,
                                    struct Trade *restrict const,
                                    const struct Array *restrict const,
                                    const struct Sample *restrict const);
  bool (*position_close)(const char *restrict const,
                         const struct Exchange *restrict const,
                         const struct Trade *restrict const,
                         const struct Position *restrict const);
  bool (*product_plot)(const char *restrict const, const char *restrict const,
                       const struct Exchange *restrict const,
                       const struct Product *restrict const);
};

struct Candle *Candle_new(void);
void Candle_delete(void *restrict const);
void Candle_copy_to(const struct Candle *restrict const,
                    struct Candle *restrict const);
void Candle_reset(struct Candle *restrict const);

const struct Algorithm *algorithm(const struct String *);
const struct Exchange *exchange(const struct String *);

void samples_per_nano(struct Numeric *restrict const,
                      const struct Array *restrict const);
void samples_per_second(struct Numeric *restrict const,
                        const struct Array *restrict const);
void samples_per_minute(struct Numeric *restrict const,
                        const struct Array *restrict const);

#endif
