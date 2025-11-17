/* $SchulteIT: main.c 15252 2025-11-03 01:36:59Z schulte $ */
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
#include "array.h"
#include "config.h"
#include "database.h"
#include "heap.h"
#include "proc.h"
#include "time.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#define OPTPARSE_IMPLEMENTATION
#include "optparse.h"

#define ABAGNALE "abagnale"
#define ABAGNALECTL "abagnalectl"

#define nitems(a) (sizeof((a)) / sizeof((a)[0]))

extern char *__progname;

extern struct Exchange exchange_coinbase;
struct Exchange *all_exchanges[] = {
    &exchange_coinbase,
};
const size_t all_exchanges_nitems = nitems(all_exchanges);

extern struct Algorithm algorithm_trend;
struct Algorithm *all_algorithms[] = {
    &algorithm_trend,
};
const size_t all_algorithms_nitems = nitems(all_algorithms);

_Atomic bool terminated;

struct Numeric *restrict zero;
struct Numeric *restrict one;
struct Numeric *restrict n_one;
struct Numeric *restrict two;
struct Numeric *restrict seven;
struct Numeric *restrict ten;
struct Numeric *restrict twenty_four;
struct Numeric *restrict sixty;
struct Numeric *restrict hundred;
struct Numeric *restrict thousand;
struct Numeric *restrict mikro_nanos;
struct Numeric *restrict milli_nanos;
struct Numeric *restrict second_nanos;
struct Numeric *restrict minute_nanos;
struct Numeric *restrict hour_nanos;
struct Numeric *restrict day_nanos;
struct Numeric *restrict week_nanos;
struct Numeric *restrict boot_nanos;

struct Config *restrict cnf;
bool verbose;

struct Array *restrict exchanges;
struct Array *restrict algorithms;

int abagnale(int argc, char *argv[]);
int abagnalectl(int argc, char *argv[]);

static void terminate(int signum) {
  terminated = true;
  void **items = Array_items(exchanges);

  for (size_t i = Array_size(exchanges); i > 0; i--)
    ((struct Exchange *)items[i - 1])->stop();
}

int main(int argc, char *argv[]) {
  int c, r = EXIT_FAILURE;
  const char *conffile = ABAG_CONFIG_FILE;
  verbose = false;
  struct optparse options = {0};
  void **items;

  proc_init();
  time_init();
  config_init();

  if (signal(SIGTERM, terminate) == SIG_ERR) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strerror(errno));
    fatal();
  }

  if (signal(SIGINT, terminate) == SIG_ERR) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strerror(errno));
    fatal();
  }

  zero = Numeric_from_int(0);
  one = Numeric_from_int(1);
  n_one = Numeric_from_int(-1);
  two = Numeric_from_int(2);
  seven = Numeric_from_int(7);
  ten = Numeric_from_int(10);
  twenty_four = Numeric_from_int(24);
  sixty = Numeric_from_int(60);
  hundred = Numeric_from_int(100);
  thousand = Numeric_from_int(1000);
  mikro_nanos = Numeric_from_long(1000000000L);
  second_nanos = Numeric_from_long(1000000000L);
  mikro_nanos = Numeric_div(second_nanos, thousand);
  milli_nanos = Numeric_div(mikro_nanos, thousand);
  minute_nanos = Numeric_mul(second_nanos, sixty);
  hour_nanos = Numeric_mul(minute_nanos, sixty);
  day_nanos = Numeric_mul(hour_nanos, twenty_four);
  week_nanos = Numeric_mul(day_nanos, seven);
  boot_nanos = Numeric_new();
  nanos_now(boot_nanos);

  optparse_init(&options, argv);
  options.permute = 0;

  while ((c = optparse(&options, "D:f:v")) != -1) {
    switch (c) {
    case 'D':
      if (config_symset(options.optarg) < 0) {
        werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, options.optarg);
        fatal();
      }
      break;
    case 'f':
      conffile = options.optarg;
      break;
    case 'v':
      verbose = true;
      break;
    case '?':
      werr("%s: %s\n", __progname, options.errmsg);
      fatal();
    }
  }

  argv += options.optind;

  for (size_t i = nitems(all_algorithms); i > 0; i--)
    all_algorithms[i - 1]->init();

  for (size_t i = nitems(all_exchanges); i > 0; i--)
    all_exchanges[i - 1]->init();

  cnf = Config_new();

  if (config_fparse(cnf, conffile))
    fatal();

  exchanges = Array_new(nitems(all_exchanges));
  algorithms = Array_new(nitems(all_algorithms));

  items = Array_items(cnf->p_cnf);
  for (size_t i = nitems(all_algorithms); i > 0; i--) {
    bool a_found = false;

    for (size_t j = Array_size(cnf->p_cnf); j > 0; j--)
      if (String_equals(all_algorithms[i - 1]->nm,
                        ((struct ProductConfig *)items[j - 1])->a_nm)) {
        a_found = true;
        break;
      }

    if (a_found)
      Array_add_tail(algorithms, all_algorithms[i - 1]);
    else
      all_algorithms[i - 1]->terminate();
  }

  for (size_t i = nitems(all_exchanges); i > 0; i--) {
    const struct ExchangeConfig *restrict const e_cnf =
        Map_get(cnf->e_cnf, all_exchanges[i - 1]->nm);

    if (e_cnf != NULL) {
      all_exchanges[i - 1]->configure(e_cnf);
      Array_add_tail(exchanges, all_exchanges[i - 1]);
    } else
      all_exchanges[i - 1]->terminate();
  }

  if (!strcmp(ABAGNALE, __progname))
    r = abagnale(argc - options.optind, argv);
  else if (!strcmp(ABAGNALECTL, __progname))
    r = abagnalectl(argc - options.optind, argv);

  items = Array_items(exchanges);
  for (size_t i = Array_size(exchanges); i > 0; i--)
    ((struct Exchange *)items[i - 1])->terminate();

  items = Array_items(algorithms);
  for (size_t i = Array_size(algorithms); i > 0; i--)
    ((struct Algorithm *)items[i - 1])->terminate();

  Array_delete(algorithms, NULL);
  Array_delete(exchanges, NULL);
  Config_delete(cnf);
  Numeric_delete(zero);
  Numeric_delete(one);
  Numeric_delete(n_one);
  Numeric_delete(two);
  Numeric_delete(seven);
  Numeric_delete(ten);
  Numeric_delete(twenty_four);
  Numeric_delete(sixty);
  Numeric_delete(hundred);
  Numeric_delete(thousand);
  Numeric_delete(mikro_nanos);
  Numeric_delete(milli_nanos);
  Numeric_delete(second_nanos);
  Numeric_delete(minute_nanos);
  Numeric_delete(hour_nanos);
  Numeric_delete(day_nanos);
  Numeric_delete(week_nanos);
  Numeric_delete(boot_nanos);
  time_terminate();
  proc_terminate();
  config_terminate();
  return r;
}
