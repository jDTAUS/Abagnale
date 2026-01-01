/* $SchulteIT: exchange.c 15189 2025-10-27 05:41:45Z schulte $ */
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
  String_delete(sample->p_id);
  Numeric_delete(sample->price);
  Numeric_delete(sample->nanos);
  heap_free(sample);
}

inline struct Product *Product_new(void) {
  return heap_calloc(1, sizeof(struct Product));
}

inline struct Product *Product_copy(const struct Product *restrict const p) {
  struct Product *restrict pd = Product_new();
  pd->id = String_copy(p->id);
  pd->nm = String_copy(p->nm);
  pd->b_id = String_copy(p->b_id);
  pd->ba_id = String_copy(p->ba_id);
  pd->q_id = String_copy(p->q_id);
  pd->qa_id = String_copy(p->qa_id);
  pd->mtx = NULL;
  pd->type = p->type;
  pd->status = p->status;
  pd->p_sc = p->p_sc;
  pd->b_sc = p->b_sc;
  pd->q_sc = p->q_sc;
  pd->is_tradeable = p->is_tradeable;
  pd->is_active = p->is_active;
  return pd;
}

inline void Product_delete(void *restrict const p) {
  if (p == NULL)
    return;

  struct Product *restrict const product = p;
  String_delete(product->id);
  String_delete(product->nm);
  String_delete(product->b_id);
  String_delete(product->ba_id);
  String_delete(product->q_id);
  String_delete(product->qa_id);
  heap_free(product);
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
  String_delete(order->p_id);
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
