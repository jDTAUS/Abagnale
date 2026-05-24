/* $SchulteIT: exchange.c 15189 2025-10-27 05:41:45Z schulte $ */
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

#include "exchange.h"
#include "heap.h"
#include "math.h"

inline struct Sample *Sample_new(void) {
  return heap_calloc(1, sizeof(struct Sample));
}

inline void Sample_delete(void *restrict const s) {
  if (s == NULL)
    return;

  struct Sample *restrict const sample = s;
  String_delete(sample->m_id);
  Numeric_delete(sample->price);
  Numeric_delete(sample->nanos);
  heap_free(sample);
}

inline struct Market *Market_new(void) {
  return heap_calloc(1, sizeof(struct Market));
}

inline struct Market *Market_copy(const struct Market *restrict const m) {
  struct Market *restrict mc = Market_new();
  mc->id = String_copy(m->id);
  mc->b_id = String_copy(m->b_id);
  mc->ba_id = String_copy(m->ba_id);
  mc->q_id = String_copy(m->q_id);
  mc->qa_id = String_copy(m->qa_id);
  mc->nm = String_copy(m->nm);
  mc->sym = String_copy(m->sym);
  mc->mtx = NULL;
  mc->type = m->type;
  mc->status = m->status;
  mc->p_sc = m->p_sc;
  mc->b_sc = m->b_sc;
  mc->q_sc = m->q_sc;
  mc->b_inc = Numeric_copy(m->b_inc);
  mc->p_inc = Numeric_copy(m->p_inc);
  mc->q_inc = Numeric_copy(m->q_inc);
  mc->is_tradeable = m->is_tradeable;
  mc->is_active = m->is_active;
  return mc;
}

inline void Market_delete(void *restrict const m) {
  if (m == NULL)
    return;

  struct Market *restrict const market = m;
  String_delete(market->id);
  String_delete(market->b_id);
  String_delete(market->ba_id);
  String_delete(market->q_id);
  String_delete(market->qa_id);
  String_delete(market->nm);
  String_delete(market->sym);
  Numeric_delete(market->b_inc);
  Numeric_delete(market->p_inc);
  Numeric_delete(market->q_inc);
  heap_free(market);
}

inline struct Account *Account_new(void) {
  return heap_calloc(1, sizeof(struct Account));
}

inline void Account_delete(void *restrict const a) {
  if (a == NULL)
    return;

  struct Account *restrict const account = a;
  String_delete(account->id);
  String_delete(account->nm);
  String_delete(account->c_id);
  Numeric_delete(account->avail);
  heap_free(account);
}

inline struct Order *Order_new(void) {
  return heap_calloc(1, sizeof(struct Order));
}

inline void Order_delete(void *restrict const o) {
  if (o == NULL)
    return;

  struct Order *restrict const order = o;
  String_delete(order->id);
  String_delete(order->m_id);
  String_delete(order->msg);
  Numeric_delete(order->cnanos);
  Numeric_delete(order->dnanos);
  Numeric_delete(order->b_ordered);
  Numeric_delete(order->p_ordered);
  Numeric_delete(order->b_filled);
  Numeric_delete(order->q_filled);
  Numeric_delete(order->q_fees);
  heap_free(order);
}

inline struct Pricing *Pricing_new(void) {
  return heap_calloc(1, sizeof(struct Pricing));
}

inline void Pricing_delete(void *restrict const p) {
  if (p == NULL)
    return;

  struct Pricing *restrict const pricing = p;
  String_delete(pricing->nm);
  Numeric_delete(pricing->tf_pc);
  Numeric_delete(pricing->mf_pc);
  Numeric_delete(pricing->ef_pc);
  heap_free(pricing);
}

inline struct ExchangeConfig *ExchangeConfig_new(void) {
  return heap_calloc(1, sizeof(struct ExchangeConfig));
}

inline void ExchangeConfig_delete(void *restrict const e) {
  if (e == NULL)
    return;

  struct ExchangeConfig *restrict const cnf = e;
  String_delete(cnf->jwt_kid);
  String_delete(cnf->jwt_key);
  heap_free(cnf);
}
