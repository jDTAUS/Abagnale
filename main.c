/* $SchulteIT: main.c 15252 2025-11-03 01:36:59Z schulte $ */
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
#include "array.h"
#include "config.h"
#include "database.h"
#include "heap.h"
#include "http.h"
#include "mongoose.h"
#include "proc.h"
#include "thread.h"
#include "time.h"
#include "version.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#define OPTPARSE_IMPLEMENTATION
#include "optparse.h"

#define ABAGNALE "abagnale"
#define ABAGNALECTL "abagnalectl"

#ifndef DEFAULT_ABAG_THREAD_TIMEOUT_MILLIS
#define DEFAULT_ABAG_THREAD_TIMEOUT_MILLIS 120500
#endif

#ifndef nitems
#define nitems(a) (sizeof((a)) / sizeof((a)[0]))
#endif

// mongoose.c
extern uint64_t mg_boot_timestamp_ms;

struct String *restrict app_nm;
struct String *restrict app_abagnale;
struct String *restrict app_abagnalectl;

volatile sig_atomic_t terminated = 0;

extern struct Exchange exchange_bitvavo;
extern struct Exchange exchange_coinbase;
struct Exchange *restrict all_exchanges[] = {
    &exchange_coinbase,
    &exchange_bitvavo,
    NULL,
};

extern struct Algorithm algorithm_trend;
struct Algorithm *restrict all_algorithms[] = {
    &algorithm_trend,
    NULL,
};

struct Numeric *restrict Numeric_0;
struct Numeric *restrict Numeric_1;
struct Numeric *restrict Numeric_neg1;
struct Numeric *restrict Numeric_2;
struct Numeric *restrict Numeric_neg2;
struct Numeric *restrict Numeric_4;
struct Numeric *restrict Numeric_5;
struct Numeric *restrict Numeric_7;
struct Numeric *restrict Numeric_10;
struct Numeric *restrict Numeric_24;
struct Numeric *restrict Numeric_60;
struct Numeric *restrict Numeric_100;
struct Numeric *restrict Numeric_1000;

struct Numeric *restrict Numeric_usec_ns;
struct Numeric *restrict Numeric_msec_ns;
struct Numeric *restrict Numeric_sec_ns;
struct Numeric *restrict Numeric_min_ns;
struct Numeric *restrict Numeric_hour_ns;
struct Numeric *restrict Numeric_36hours_ns;
struct Numeric *restrict Numeric_day_ns;
struct Numeric *restrict Numeric_week_ns;

struct Config *restrict cnf;
struct String *restrict proc_id;
bool ticker_exporter;
bool verbose;
struct timespec thread_timeout;

struct Array *restrict exchanges;
struct Array *restrict algorithms;
struct Array *restrict volatility_windows;

int abagnale(int argc, char *argv[]);
int abagnalectl(int argc, char *argv[]);

static void terminate(int signum) { terminated = 1; }

static _Noreturn void usage(void) {
  werr("Usage: %s [-Dmacro=value ... ] [-f config-file] [-I entity] [-i "
       "process-uuid] [-n] [-p plots-dir] [-v]\n",
       String_chars(app_nm));

  exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
  int c, r = EXIT_FAILURE;
  char *plotsdir = NULL;
  ticker_exporter = true;
  verbose = false;
  struct timespec ts;
  struct optparse options = {0};
  void *const *restrict items;
  bool configtest = false;
  const uint64_t mg_ms = mg_millis();

  thread_init();
  proc_init();

  const char *conffile = envs("ABAG_CONFIG_FILE", DEFAULT_ABAG_CONFIG_FILE);
  const unsigned long thread_timeout_millis =
      envul("ABAG_THREAD_TIMEOUT_MILLIS", DEFAULT_ABAG_THREAD_TIMEOUT_MILLIS);

  thread_timeout.tv_sec = thread_timeout_millis / 1000;
  thread_timeout.tv_nsec = thread_timeout_millis % 1000L * 1000000L;

  string_init();
  time_init();

  time_now(&ts);

  const uint64_t epoch_ms =
      (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;

  mg_boot_timestamp_ms = (uint64_t)(epoch_ms - mg_ms);
  mg_log_set(MG_LL_NONE); // NONE, ERROR, INFO, DEBUG, VERBOSE

  proc_id = String_cnew("1fef4eab-66e8-4f49-8697-7132308b52f2");

  if (argv[0] != NULL) {
    char *restrict p_nm = strrchr(argv[0], '/');

    if (p_nm == NULL)
      p_nm = strrchr(argv[0], '\\');

    char *restrict p_dot = strrchr(argv[0], '.');

    if (p_dot != NULL && strcmp(p_dot, ".exe") != 0)
      p_dot = NULL;

    if (p_dot != NULL)
      app_nm =
          String_cnnew(p_nm != NULL ? p_nm + 1 : argv[0],
                       p_nm != NULL ? p_dot - (p_nm + 1) : p_dot - argv[0]);
    else
      app_nm = String_cnew(p_nm != NULL ? p_nm + 1 : argv[0]);

  } else
    app_nm = String_cnew(".");

  config_init();

  optparse_init(&options, argv);
  options.permute = 0;

  while ((c = optparse(&options, "D:f:I:i:p:vn")) != -1) {
    switch (c) {
    case 'D':
      if (config_symset(options.optarg) < 0)
        usage();
      break;
    case 'f':
      conffile = options.optarg;
      break;
    case 'I':
      if (strcmp("SAMPLES", options.optarg)) {
        werr("%s: Unsupported entity: -I %s\n", String_chars(app_nm),
             options.optarg);
        return (EXIT_FAILURE);
      }

      ticker_exporter = false;
      break;
    case 'i':
      String_delete(proc_id);
      proc_id = String_cnew(options.optarg);
      break;
    case 'n':
      configtest = true;
      break;
    case 'p':
      plotsdir = options.optarg;
      break;
    case 'v':
      verbose = true;
      break;
    case '?':
      usage();
    }
  }

  argv += options.optind;

  if (configtest)
    verbose = false;

  if (verbose) {
    wout("%s\n", ABAG_REVISION);
    wout("\tABAG_CONFIG_FILE=%s\n", conffile);
    wout("\tABAG_THREAD_TIMEOUT_MILLIS=%lu\n", thread_timeout_millis);
  }

  app_abagnale = String_cnew(ABAGNALE);
  app_abagnalectl = String_cnew(ABAGNALECTL);

  Numeric_0 = Numeric_from_int(0);
  Numeric_1 = Numeric_from_int(1);
  Numeric_neg1 = Numeric_from_int(-1);
  Numeric_2 = Numeric_from_int(2);
  Numeric_neg2 = Numeric_from_int(-2);
  Numeric_4 = Numeric_from_int(4);
  Numeric_5 = Numeric_from_int(5);
  Numeric_7 = Numeric_from_int(7);
  Numeric_10 = Numeric_from_int(10);
  Numeric_24 = Numeric_from_int(24);
  Numeric_60 = Numeric_from_int(60);
  Numeric_100 = Numeric_from_int(100);
  Numeric_1000 = Numeric_from_int(1000);

  Numeric_sec_ns = Numeric_from_long(1000000000L);
  Numeric_usec_ns = Numeric_div(Numeric_sec_ns, Numeric_1000);
  Numeric_msec_ns = Numeric_div(Numeric_usec_ns, Numeric_1000);
  Numeric_min_ns = Numeric_mul(Numeric_sec_ns, Numeric_60);
  Numeric_hour_ns = Numeric_mul(Numeric_min_ns, Numeric_60);
  Numeric_36hours_ns = Numeric_from_long(36 * 60 * 60 * 1000000000L);
  Numeric_day_ns = Numeric_mul(Numeric_hour_ns, Numeric_24);
  Numeric_week_ns = Numeric_mul(Numeric_day_ns, Numeric_7);

  volatility_windows = Array_new(64);
  Array_add_tail(volatility_windows, Numeric_from_long(1));
  Array_add_tail(volatility_windows, Numeric_from_long(500));
  Array_add_tail(volatility_windows, Numeric_from_long(1e3));
  Array_add_tail(volatility_windows, Numeric_from_long(5e3));
  Array_add_tail(volatility_windows, Numeric_from_long(1e6));
  Array_add_tail(volatility_windows, Numeric_from_long(5e6));

  for (size_t i = 1; i < 10; i++)
    Array_add_tail(volatility_windows, Numeric_from_long(i * 1e8));

  for (size_t i = 2; i < 120; i++)
    Array_add_tail(volatility_windows, Numeric_from_long(i * .5e9));

  for (size_t i = 1; i < 120; i++)
    Array_add_tail(volatility_windows, Numeric_from_long(i * 30e9));

  for (size_t i = 1; i < 48; i++)
    Array_add_tail(volatility_windows, Numeric_from_long(i * 18e11));

  Array_add_tail(volatility_windows, Numeric_from_long(24 * 36e11));

  Array_compact(volatility_windows);

  http_init();
  json_init();
  abagnale_init();

  for (size_t i = 0; all_algorithms[i] != NULL; i++)
    all_algorithms[i]->init();

  for (size_t i = 0; all_exchanges[i] != NULL; i++)
    all_exchanges[i]->init();

  cnf = Config_new();
  if (plotsdir != NULL)
    cnf->plts_dir = String_cnew(plotsdir);

  if (config_fparse(cnf, conffile))
    exit(EXIT_FAILURE);

  exchanges = Array_new(nitems(all_exchanges));
  algorithms = Array_new(nitems(all_algorithms));

  items = Array_items(cnf->m_cnf);
  for (size_t i = 0; all_algorithms[i] != NULL; i++) {
    bool a_found = false;

    for (size_t j = Array_size(cnf->m_cnf); j-- > 0;)
      if (String_equals(all_algorithms[i]->nm,
                        ((struct MarketConfig *)items[j])->a_nm)) {
        a_found = true;
        break;
      }

    if (a_found)
      Array_add_tail(algorithms, all_algorithms[i]);
  }

  for (size_t i = 0; all_exchanges[i] != NULL; i++) {
    const struct ExchangeConfig *restrict const e_cnf =
        Map_get(cnf->e_cnf, all_exchanges[i]->nm);

    if (e_cnf != NULL) {
      all_exchanges[i]->configure(e_cnf);
      Array_add_tail(exchanges, all_exchanges[i]);
    }
  }

  Array_compact(algorithms);
  Array_compact(exchanges);

  if (signal(SIGTERM, terminate) == SIG_ERR)
    fatal("%s", strerror(errno));

  if (signal(SIGINT, terminate) == SIG_ERR)
    fatal("%s", strerror(errno));

  if (!configtest) {
    if (String_equals(app_nm, app_abagnale)) {
      r = abagnale(argc - options.optind, argv);
    } else if (String_equals(app_nm, app_abagnalectl)) {
      r = abagnalectl(argc - options.optind, argv);
    } else
      fatal("%s", String_chars(app_nm));
  } else
    r = EXIT_SUCCESS;

  for (size_t i = 0; all_algorithms[i] != NULL; i++)
    all_algorithms[i]->destroy();

  for (size_t i = 0; all_exchanges[i] != NULL; i++)
    all_exchanges[i]->destroy();

  Array_delete(algorithms, NULL);
  Array_delete(exchanges, NULL);
  Array_delete(volatility_windows, Numeric_delete);
  Config_delete(cnf);

  Numeric_delete(Numeric_0);
  Numeric_delete(Numeric_1);
  Numeric_delete(Numeric_neg1);
  Numeric_delete(Numeric_2);
  Numeric_delete(Numeric_neg2);
  Numeric_delete(Numeric_4);
  Numeric_delete(Numeric_5);
  Numeric_delete(Numeric_7);
  Numeric_delete(Numeric_10);
  Numeric_delete(Numeric_24);
  Numeric_delete(Numeric_60);
  Numeric_delete(Numeric_100);
  Numeric_delete(Numeric_1000);

  Numeric_delete(Numeric_sec_ns);
  Numeric_delete(Numeric_usec_ns);
  Numeric_delete(Numeric_msec_ns);
  Numeric_delete(Numeric_min_ns);
  Numeric_delete(Numeric_hour_ns);
  Numeric_delete(Numeric_36hours_ns);
  Numeric_delete(Numeric_day_ns);
  Numeric_delete(Numeric_week_ns);

  String_delete(app_nm);
  String_delete(app_abagnale);
  String_delete(app_abagnalectl);

  abagnale_destroy();
  json_destroy();
  http_destroy();
  config_destroy();
  time_destroy();
  string_destroy();
  proc_destroy();
  thread_destroy();

  return r;
}
