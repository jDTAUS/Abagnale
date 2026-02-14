/* $SchulteIT: exchange.h 15189 2025-10-27 05:41:45Z schulte $ */
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

#ifndef ABAG_EXCHANGE_H
#define ABAG_EXCHANGE_H

#include "array.h"
#include "math.h"
#include "string.h"

#include <stdbool.h>
#include <stdint.h>

enum market_type {
  MARKET_TYPE_UNKNOWN = 1,
  MARKET_TYPE_SPOT,
  MARKET_TYPE_FUTURE,
};

enum market_status {
  MARKET_STATUS_UNKNOWN = 1,
  MARKET_STATUS_ONLINE,
  MARKET_STATUS_DELISTED,
};

struct Market {
  struct String *restrict id;
  struct String *restrict b_id;
  struct String *restrict ba_id;
  struct String *restrict q_id;
  struct String *restrict qa_id;
  struct String *restrict nm;
  struct String *restrict sym;
  struct Numeric *restrict b_inc;
  struct Numeric *restrict p_inc;
  struct Numeric *restrict q_inc;
  mtx_t *restrict mtx;
  enum market_type type;
  enum market_status status;
  size_t p_sc;
  size_t b_sc;
  size_t q_sc;
  bool is_tradeable;
  bool is_active;
};

enum account_type {
  ACCOUNT_TYPE_UNSPECIFIED = 1,
  ACCOUNT_TYPE_CRYPTO,
  ACCOUNT_TYPE_FIAT,
  ACCOUNT_TYPE_VAULT,
  ACCOUNT_TYPE_PERP_FUTURES,
};

struct Account {
  struct String *restrict id;
  struct String *restrict nm;
  struct String *restrict c_id;
  struct Numeric *restrict avail;
  mtx_t *restrict mtx;
  enum account_type type;
  bool is_active;
  bool is_ready;
};

struct Sample {
  struct String *restrict m_id;
  struct Numeric *restrict nanos;
  struct Numeric *restrict price;
};

enum order_status {
  ORDER_STATUS_PENDING = 1,
  ORDER_STATUS_OPEN,
  ORDER_STATUS_FILLED,
  ORDER_STATUS_CANCELLED,
  ORDER_STATUS_EXPIRED,
  ORDER_STATUS_FAILED,
  ORDER_STATUS_UNKNOWN,
  ORDER_STATUS_QUEUED,
  ORDER_STATUS_CANCEL_QUEUED,
};

struct Order {
  struct String *restrict id;
  struct String *restrict m_id;
  struct Numeric *restrict cnanos;
  struct Numeric *restrict dnanos;
  struct Numeric *restrict b_ordered;
  struct Numeric *restrict p_ordered;
  struct Numeric *restrict b_filled;
  struct Numeric *restrict q_filled;
  struct Numeric *restrict q_fees;
  struct String *restrict msg;
  enum order_status status;
  bool settled;
};

struct Pricing {
  struct String *restrict nm;
  struct Numeric *restrict tf_pc;
  struct Numeric *restrict mf_pc;
  struct Numeric *restrict ef_pc;
  mtx_t *restrict mtx;
};

struct ExchangeConfig {
  struct String *restrict jwt_kid;
  struct String *restrict jwt_key;
};

struct Exchange {
  struct String *restrict id;
  struct String *restrict nm;
  void (*init)(void);
  void (*configure)(const struct ExchangeConfig *restrict const);
  void (*destroy)(void);
  void (*start)(void);
  void (*stop)(void);
  struct Array *(*markets)(void);
  struct Market *(*market)(const struct String *restrict const);
  struct Array *(*accounts)(void);
  struct Account *(*account)(const struct String *restrict const);
  struct Order *(*order)(const struct String *restrict const);
  struct Pricing *(*pricing)(void);
  struct Order *(*order_await)(void);
  struct Sample *(*sample_await)(void);
  struct String *(*buy)(const struct String *restrict const,
                        const char *restrict const, const char *restrict const);
  struct String *(*sell)(const struct String *restrict const,
                         const char *restrict const,
                         const char *restrict const);
  bool (*cancel)(const struct String *restrict const);
};

struct Market *Market_new(void);
struct Market *Market_copy(const struct Market *restrict const);
void Market_delete(void *restrict const);

struct Account *Account_new(void);
void Account_delete(void *restrict const);

struct Sample *Sample_new(void);
void Sample_delete(void *restrict const);

struct Order *Order_new(void);
void Order_delete(void *restrict const);

struct Pricing *Pricing_new(void);
void Pricing_delete(void *restrict const);

struct ExchangeConfig *ExchangeConfig_new(void);
void ExchangeConfig_delete(void *restrict const);
#endif
