/* $SchulteIT: abagnalectl.c 15262 2025-11-04 03:42:05Z schulte $ */
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
#include "array.h"
#include "database.h"
#include "heap.h"
#include "math.h"
#include "proc.h"
#include "thread.h"
#include "time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "optparse.h"

#ifndef nitems
#define nitems(_a) (sizeof((_a)) / sizeof((_a)[0]))
#endif

extern struct String *restrict const progname;
extern bool terminated;

extern const struct Algorithm *restrict const all_algorithms;
extern const size_t all_algorithms_nitems;
extern const struct Array *restrict const algorithms;

extern const struct Exchange *restrict const all_exchanges;
extern const size_t all_exchanges_nitems;
extern const struct Array *restrict const exchanges;

static const struct {
  const enum market_type type;
  const char *const name;
} market_types[] = {{MARKET_TYPE_UNKNOWN, "UNKNOWN"},
                    {MARKET_TYPE_SPOT, "SPOT"},
                    {MARKET_TYPE_FUTURE, "FUTURE"}};

static const struct {
  const enum market_status status;
  const char *const name;
} market_status[] = {{MARKET_STATUS_UNKNOWN, "UNKNOWN"},
                     {MARKET_STATUS_ONLINE, "ONLINE"},
                     {MARKET_STATUS_DELISTED, "DELISTED"}};

static const struct {
  const enum account_type type;
  const char *const name;
} account_types[] = {{ACCOUNT_TYPE_UNSPECIFIED, "UNSPECIFIED"},
                     {ACCOUNT_TYPE_CRYPTO, "CRYPTO"},
                     {ACCOUNT_TYPE_FIAT, "FIAT"},
                     {ACCOUNT_TYPE_VAULT, "VAULT"},
                     {ACCOUNT_TYPE_PERP_FUTURES, "PERP_FUTURES"}};

static const struct {
  const enum order_status status;
  const char *const name;
} order_status[] = {{ORDER_STATUS_PENDING, "PENDING"},
                    {ORDER_STATUS_OPEN, "OPEN"},
                    {ORDER_STATUS_FILLED, "FILLED"},
                    {ORDER_STATUS_CANCELLED, "CANCELLED"},
                    {ORDER_STATUS_EXPIRED, "EXPIRED"},
                    {ORDER_STATUS_FAILED, "FAILED"},
                    {ORDER_STATUS_UNKNOWN, "UNKNOWN"},
                    {ORDER_STATUS_QUEUED, "QUEUED"},
                    {ORDER_STATUS_CANCEL_QUEUED, "CANCEL_QUEUED"}};

int abagnalectl(int argc, char *argv[]);
static int cmd_vacuum(int, char *[]);
static int cmd_algorithms(int, char *[]);
static int cmd_exchanges(int, char *[]);
static int cmd_markets(int, char *[]);
static int cmd_market(int, char *[]);
static int cmd_accounts(int, char *[]);
static int cmd_account(int, char *[]);
static int cmd_order(int, char *[]);
static int cmd_plot(int, char *[]);

static const struct {
  const char *restrict name;
  const char *restrict usage;
  int (*cmd)(int, char *[]);
} cmd_tab[] = {
    {"vacuum", "-o file", cmd_vacuum},
    {"algorithms", "", cmd_algorithms},
    {"exchanges", "", cmd_exchanges},
    {"markets",
     "-e exchange [-s UNKNOWN|ONLINE|DELISTED] [-t UNKNOWN|SPOT|FUTURE]",
     cmd_markets},
    {"market", "-e exchange -i id", cmd_market},
    {"accounts", "-e exchange [-t UNSPECIFIED|CRYPTO|FIAT|VAULT|PERP_FUTURES]",
     cmd_accounts},
    {"account", "-e exchange -i id", cmd_account},
    {"order", "-e exchange -i id", cmd_order},
    {"plot", "-e exchange -m market -a algorithm [-f file]", cmd_plot},
};

static const char *market_type_name(const enum market_type type) {
  for (size_t i = nitems(market_types); i > 0; i--)
    if (market_types[i - 1].type == type)
      return market_types[i - 1].name;

  return "NOT FOUND";
}

static const enum market_type market_type_value(const char *restrict type) {
  for (size_t i = nitems(market_types); i > 0; i--)
    if (!strcmp(type, market_types[i - 1].name))
      return market_types[i - 1].type;

  return 0;
}

static const char *market_status_name(const enum market_status status) {
  for (size_t i = nitems(market_status); i > 0; i--)
    if (market_status[i - 1].status == status)
      return market_status[i - 1].name;

  return "NOT FOUND";
}

static const enum market_status
market_status_value(const char *restrict status) {
  for (size_t i = nitems(market_status); i > 0; i--)
    if (!strcmp(status, market_status[i - 1].name))
      return market_status[i - 1].status;

  return 0;
}

static const char *account_type_name(const enum account_type type) {
  for (size_t i = nitems(account_types); i > 0; i--)
    if (account_types[i - 1].type == type)
      return account_types[i - 1].name;

  return "NOT FOUND";
}

static const enum account_type account_type_value(const char *restrict type) {
  for (size_t i = nitems(account_types); i > 0; i--)
    if (!strcmp(type, account_types[i - 1].name))
      return account_types[i - 1].type;

  return 0;
}

static const char *order_status_name(const enum order_status status) {
  for (size_t i = nitems(order_status); i > 0; i--)
    if (status == order_status[i - 1].status)
      return order_status[i - 1].name;

  return "NOT FOUND";
}

static void print_account(const struct Account *restrict const a) {
  printf("%s\t%s\t%s\t%s\t%lf\t%d\t%d\n", String_chars(a->id),
         String_chars(a->nm), String_chars(a->c_id), account_type_name(a->type),
         Numeric_to_double(a->avail), a->is_active ? 1 : 0,
         a->is_ready ? 1 : 0);
}

static void print_market(const struct Market *restrict const m) {
  char *restrict const b_inc = Numeric_to_char(m->b_inc, m->b_sc);
  char *restrict const p_inc = Numeric_to_char(m->p_inc, m->p_sc);
  char *restrict const q_inc = Numeric_to_char(m->q_inc, m->q_sc);

  printf("%s\t%s\t%s\t%s\t%s\t%s\t%zu\t%s\t%zu\t%s\t%zu\t%s\t%d\t%d\n",
         String_chars(m->id), String_chars(m->nm), market_type_name(m->type),
         market_status_name(m->status), String_chars(m->b_id),
         String_chars(m->q_id), m->b_sc, b_inc, m->p_sc, p_inc, m->q_sc, q_inc,
         m->is_tradeable ? 1 : 0, m->is_active ? 1 : 0);

  Numeric_char_free(b_inc);
  Numeric_char_free(p_inc);
  Numeric_char_free(q_inc);
}

static _Noreturn void usage(void) {
  werr("usage: %s\n", String_chars(progname));

  for (size_t i = nitems(cmd_tab); i > 0; i--)
    werr("       %s %s %s\n", String_chars(progname), cmd_tab[i - 1].name,
         cmd_tab[i - 1].usage);

  fatal();
}

static int cmd_vacuum(int argc, char *argv[]) {
  int ch;
  const char *restrict file = NULL;
  struct optparse options = {0};
  optparse_init(&options, argv);

  while ((ch = optparse(&options, "o:")) != -1) {
    switch (ch) {
    case 'o':
      file = options.optarg;
      break;
    default:
      usage();
    }
  }
  argc -= options.optind;

  if (argc > 0 || file == NULL)
    usage();

  db_connect(String_chars(progname));
  db_vacuum(String_chars(progname), file);
  db_disconnect(String_chars(progname));

  return EXIT_SUCCESS;
}

static int cmd_algorithms(int argc, char *argv[]) {
  if (argc > 1)
    usage();

  for (size_t i = all_algorithms_nitems; i > 0; i--)
    printf("%s\n", String_chars(all_algorithms[i - 1].nm));

  return EXIT_SUCCESS;
}

static int cmd_exchanges(int argc, char *argv[]) {
  if (argc > 1)
    usage();

  for (size_t i = all_exchanges_nitems; i > 0; i--)
    printf("%s\n", String_chars(all_exchanges[i - 1].nm));

  return EXIT_SUCCESS;
}

static int cmd_markets(int argc, char *argv[]) {
  int ch, r = EXIT_FAILURE;
  struct String *restrict e_nm = NULL;
  enum market_status status = 0;
  enum market_type type = 0;
  struct optparse options = {0};
  void **items;

  optparse_init(&options, argv);

  while ((ch = optparse(&options, "e:s:t:")) != -1) {
    switch (ch) {
    case 'e':
      e_nm = String_cnew(options.optarg);
      break;
    case 's':
      status = market_status_value(options.optarg);
      if (!status)
        usage();
      break;
    case 't':
      type = market_type_value(options.optarg);
      if (!type)
        usage();
      break;
    default:
      usage();
    }
  }
  argc -= options.optind;

  if (argc > 0 || e_nm == NULL)
    usage();

  const struct Exchange *restrict const e = exchange(e_nm);

  if (e == NULL) {
    werr("%s: %s: Exchange not found\n", String_chars(progname),
         String_chars(e_nm));
    goto ret;
  }

  struct Array *restrict const markets = e->markets();

  items = Array_items(markets);
  for (size_t i = Array_size(markets); i > 0; i--) {
    const struct Market *restrict const m = items[i - 1];

    if ((status && m->status == status) || (type && m->type == type) ||
        !(status || type))
      print_market(m);
  }

  Array_unlock(markets);
  r = EXIT_SUCCESS;
ret:
  String_delete(e_nm);
  return r;
}

static int cmd_market(int argc, char *argv[]) {
  int ch, r = EXIT_FAILURE;
  struct String *restrict e_nm = NULL;
  struct String *restrict m_id = NULL;
  struct Market *restrict m = NULL;
  struct optparse options = {0};
  optparse_init(&options, argv);

  while ((ch = optparse(&options, "e:i:")) != -1) {
    switch (ch) {
    case 'e':
      e_nm = String_cnew(options.optarg);
      break;
    case 'i':
      m_id = String_cnew(options.optarg);
      break;
    default:
      usage();
    }
  }
  argc -= options.optind;

  if (argc > 0 || e_nm == NULL || m_id == NULL)
    usage();

  const struct Exchange *restrict const e = exchange(e_nm);

  if (e == NULL) {
    werr("%s: %s: Exchange not found\n", String_chars(progname),
         String_chars(e_nm));
    goto ret;
  }

  m = e->market(m_id);

  if (m != NULL) {
    print_market(m);
    mutex_unlock(m->mtx);
  }

  r = EXIT_SUCCESS;
ret:
  String_delete(e_nm);
  String_delete(m_id);
  Market_delete(m);
  return r;
}

static int cmd_accounts(int argc, char *argv[]) {
  int ch, r = EXIT_FAILURE;
  struct String *restrict e_nm = NULL;
  enum account_type type = 0;
  struct optparse options = {0};
  void **items;

  optparse_init(&options, argv);

  while ((ch = optparse(&options, "e:t:")) != -1) {
    switch (ch) {
    case 'e':
      e_nm = String_cnew(options.optarg);
      break;
    case 't':
      type = account_type_value(options.optarg);
      if (!type)
        usage();
      break;
    default:
      usage();
    }
  }
  argc -= options.optind;

  if (argc > 0 || e_nm == NULL)
    usage();

  const struct Exchange *restrict const e = exchange(e_nm);

  if (e == NULL) {
    werr("%s: %s: Exchange not found\n", String_chars(progname),
         String_chars(e_nm));
    goto ret;
  }

  struct Array *restrict const accounts = e->accounts();

  items = Array_items(accounts);
  for (size_t i = Array_size(accounts); i > 0; i--) {
    const struct Account *restrict const a = items[i - 1];

    if ((type && a->type == type) || !type)
      print_account(a);
  }

  Array_unlock(accounts);
  r = EXIT_SUCCESS;
ret:
  String_delete(e_nm);
  return r;
}

static int cmd_account(int argc, char *argv[]) {
  int ch, r = EXIT_FAILURE;
  struct String *restrict e_nm = NULL;
  struct String *restrict a_id = NULL;
  struct Account *restrict a = NULL;
  struct optparse options = {0};
  optparse_init(&options, argv);

  while ((ch = optparse(&options, "e:i:")) != -1) {
    switch (ch) {
    case 'e':
      e_nm = String_cnew(options.optarg);
      break;
    case 'i':
      a_id = String_cnew(options.optarg);
      break;
    default:
      usage();
    }
  }
  argc -= options.optind;

  if (argc > 0 || e_nm == NULL || a_id == NULL)
    usage();

  const struct Exchange *restrict const e = exchange(e_nm);

  if (e == NULL) {
    werr("%s: %s: Exchange not found\n", String_chars(progname),
         String_chars(e_nm));
    goto ret;
  }

  a = e->account(a_id);

  if (a != NULL) {
    print_account(a);
    mutex_unlock(a->mtx);
  }

  r = EXIT_SUCCESS;
ret:
  String_delete(e_nm);
  String_delete(a_id);
  Account_delete(a);
  return r;
}

static int cmd_order(int argc, char *argv[]) {
  int ch, r = EXIT_FAILURE;
  struct String *restrict e_nm = NULL;
  struct String *restrict o_id = NULL;
  struct Order *restrict o = NULL;
  struct Market *restrict m = NULL;
  char *restrict c_iso8601 = NULL;
  char *restrict d_iso8601 = NULL;
  char *restrict b_ordered = NULL;
  char *restrict p_ordered = NULL;
  char *restrict b_filled = NULL;
  char *restrict q_filled = NULL;
  char *restrict q_fees = NULL;
  struct optparse options = {0};
  optparse_init(&options, argv);

  while ((ch = optparse(&options, "e:i:")) != -1) {
    switch (ch) {
    case 'e':
      e_nm = String_cnew(options.optarg);
      break;
    case 'i':
      o_id = String_cnew(options.optarg);
      break;
    default:
      usage();
    }
  }
  argc -= options.optind;

  if (argc > 0 || e_nm == NULL || o_id == NULL)
    usage();

  const struct Exchange *restrict const e = exchange(e_nm);

  if (e == NULL) {
    werr("%s: %s: Exchange not found\n", String_chars(progname),
         String_chars(e_nm));
    goto ret;
  }

  o = e->order(o_id);

  if (o == NULL) {
    werr("%s: %s: Order not found\n", String_chars(progname),
         String_chars(o_id));
    goto ret;
  }

  m = e->market(o->m_id);

  if (m == NULL) {
    werr("%s: %s: Market not found\n", String_chars(progname),
         String_chars(o->m_id));
    goto ret;
  }

  c_iso8601 = nanos_to_iso8601(o->cnanos);
  d_iso8601 = nanos_to_iso8601(o->dnanos);
  b_ordered = Numeric_to_char(o->b_ordered, m->b_sc);
  p_ordered = Numeric_to_char(o->p_ordered, m->p_sc);
  b_filled = Numeric_to_char(o->b_filled, m->b_sc);
  q_filled = Numeric_to_char(o->q_filled, m->q_sc);
  q_fees = Numeric_to_char(o->q_fees, m->q_sc);

  printf("%s\t%s\t%s\t%d\t%s\t%s\t%s%s@%s%s\t%s%s\t%s%s\t%s%s\t%s\n",
         String_chars(o->id), String_chars(m->id), order_status_name(o->status),
         o->settled ? 1 : 0, c_iso8601, d_iso8601, b_ordered,
         String_chars(m->b_id), p_ordered, String_chars(m->q_id), b_filled,
         String_chars(m->b_id), q_filled, String_chars(m->q_id), q_fees,
         String_chars(m->q_id), o->msg ? String_chars(o->msg) : "");

  mutex_unlock(m->mtx);
  r = EXIT_SUCCESS;
ret:
  String_delete(e_nm);
  String_delete(o_id);
  Order_delete(o);
  Market_delete(m);
  Numeric_char_free(b_ordered);
  Numeric_char_free(p_ordered);
  Numeric_char_free(b_filled);
  Numeric_char_free(q_filled);
  Numeric_char_free(q_fees);
  heap_free(c_iso8601);
  heap_free(d_iso8601);
  return r;
}

static int cmd_plot(int argc, char *argv[]) {
  int ch, r = EXIT_FAILURE;
  struct String *restrict e_nm = NULL;
  struct String *restrict m_nm = NULL;
  struct String *restrict a_nm = NULL;
  char *restrict f_nm = NULL;
  struct Market *restrict m = NULL;
  const struct Algorithm *restrict a = NULL;
  struct optparse options = {0};
  void **items;

  optparse_init(&options, argv);

  while ((ch = optparse(&options, "e:p:a:f:")) != -1) {
    switch (ch) {
    case 'e':
      e_nm = String_cnew(options.optarg);
      break;
    case 'm':
      m_nm = String_cnew(options.optarg);
      break;
    case 'a':
      a_nm = String_cnew(options.optarg);
      break;
    case 'f':
      f_nm = options.optarg;
      break;
    default:
      usage();
    }
  }
  argc -= options.optind;

  if (argc > 0 || e_nm == NULL || m_nm == NULL || a_nm == NULL || f_nm == NULL)
    usage();

  const struct Exchange *restrict const e = exchange(e_nm);

  if (e == NULL) {
    werr("%s: %s: Exchange not found\n", String_chars(progname),
         String_chars(e_nm));
    goto ret;
  }

  struct Array *restrict const markets = e->markets();

  items = Array_items(markets);
  for (size_t i = Array_size(markets); i > 0; i--) {
    struct Market *restrict const needle = items[i - 1];
    if (String_equals(needle->nm, m_nm)) {
      m = needle;
      break;
    }
  }

  if (m == NULL) {
    werr("%s: %s: Market not found\n", String_chars(progname),
         String_chars(m_nm));
    goto unlock;
  }

  a = algorithm(a_nm);

  if (a == NULL) {
    werr("%s: %s: Algorithm not found\n", String_chars(progname),
         String_chars(a_nm));
    goto unlock;
  }

  db_connect(String_chars(progname));

  if (!a->market_plot(f_nm, String_chars(progname), e, m)) {
    werr("%s: %s: Algortihm does not provide plots\n", String_chars(progname),
         String_chars(a_nm));
    goto disconnect;
  }

  r = EXIT_SUCCESS;
disconnect:
  db_disconnect(String_chars(progname));
unlock:
  Array_unlock(markets);
ret:
  String_delete(e_nm);
  String_delete(m_nm);
  String_delete(a_nm);
  return r;
}

int abagnalectl(int argc, char *argv[]) {
  int (*cmd)(int, char *[]) = NULL;

  if (argv[0] != NULL)
    for (size_t i = nitems(cmd_tab); i > 0; i--)
      if (!strcmp(argv[0], cmd_tab[i - 1].name)) {
        cmd = cmd_tab[i - 1].cmd;
        break;
      }

  if (cmd == NULL)
    usage();

  return cmd(argc, argv);
}
