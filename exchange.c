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

struct Sample *Sample_new(void) {
  struct Sample *restrict const s = heap_calloc(1, sizeof(struct Sample));
  s->obj = Object_new();
  return s;
}

void Sample_delete(void *restrict const s) {
  if (s == NULL)
    return;

  struct Sample *restrict const sample = s;
  String_delete(sample->p_id);

  if (Object_delete(sample->obj)) {
    Numeric_delete(sample->price);
    Numeric_delete(sample->nanos);
    heap_free(sample);
  }
}

struct Product *Product_new(void) {
  struct Product *restrict const p = heap_calloc(1, sizeof(struct Product));
  p->obj = Object_new();
  return p;
}

struct Product *Product_copy(struct Product *restrict const p) {
  if (p == NULL)
    return NULL;

  String_copy(p->id);
  String_copy(p->nm);
  String_copy(p->b_id);
  String_copy(p->ba_id);
  String_copy(p->q_id);
  String_copy(p->qa_id);
  Object_copy(p->obj);
  return p;
}

void Product_delete(void *restrict const p) {
  if (p == NULL)
    return;

  struct Product *restrict const product = p;
  String_delete(product->id);
  String_delete(product->nm);
  String_delete(product->b_id);
  String_delete(product->ba_id);
  String_delete(product->q_id);
  String_delete(product->qa_id);

  if (Object_delete(product->obj))
    heap_free(product);
}

struct Account *Account_new(void) {
  struct Account *restrict const a = heap_calloc(1, sizeof(struct Account));
  a->obj = Object_new();
  return a;
}

void Account_delete(void *restrict const a) {
  if (a == NULL)
    return;

  struct Account *restrict const account = a;
  String_delete(account->id);
  String_delete(account->nm);
  String_delete(account->c_id);

  if (Object_delete(account->obj)) {
    Numeric_delete(account->avail);
    heap_free(account);
  }
}

struct Order *Order_new(void) {
  struct Order *restrict const o = heap_calloc(1, sizeof(struct Order));
  o->obj = Object_new();
  return o;
}

void Order_delete(void *restrict const o) {
  if (o == NULL)
    return;

  struct Order *restrict const order = o;
  String_delete(order->id);
  String_delete(order->p_id);
  String_delete(order->msg);

  if (Object_delete(order->obj)) {
    Numeric_delete(order->cnanos);
    Numeric_delete(order->dnanos);
    Numeric_delete(order->b_ordered);
    Numeric_delete(order->p_ordered);
    Numeric_delete(order->b_filled);
    Numeric_delete(order->q_filled);
    Numeric_delete(order->q_fees);
    heap_free(order);
  }
}

struct Pricing *Pricing_new(void) {
  struct Pricing *restrict const p = heap_calloc(1, sizeof(struct Pricing));
  p->obj = Object_new();
  return p;
}

void Pricing_delete(void *restrict const p) {
  if (p == NULL)
    return;

  struct Pricing *restrict const pricing = p;
  String_delete(pricing->nm);

  if (Object_delete(pricing->obj)) {
    Numeric_delete(pricing->tf_pc);
    Numeric_delete(pricing->mf_pc);
    Numeric_delete(pricing->ef_pc);
    heap_free(pricing);
  }
}

struct ExchangeConfig *ExchangeConfig_new(void) {
  struct ExchangeConfig *restrict const e =
      heap_calloc(1, sizeof(struct ExchangeConfig));

  e->obj = Object_new();
  return e;
}

void ExchangeConfig_delete(void *restrict const e) {
  if (e == NULL)
    return;

  struct ExchangeConfig *restrict const cnf = e;
  String_delete(cnf->jwt_kid);
  String_delete(cnf->jwt_key);

  if (Object_delete(cnf->obj))
    heap_free(cnf);
}
