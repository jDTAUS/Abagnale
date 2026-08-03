/* $JDTAUS$ */

/*
 * Copyright (c) 2026 Christian Schulte <cs@schulte.it>
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

#include "database.h"
#include "exchange.h"
#include "heap.h"
#include "http.h"
#include "mongoose-ext.h"
#include "proc.h"
#include "queue.h"
#include "thread.h"
#include "time.h"
#include "version.h"

#include <inttypes.h>
#include <stdio.h>

#define BITVAVO_UUID "a92b69cd-7247-440d-ba97-65b47217b667"
#define BITVAVO_OPERATOR_ID L"65666572"
#define BITVAVO_OPERATOR_ID_LEN (size_t)8
#define BITVAVO_DBCON "bitvavo"

#define URI_MAX (size_t)512
#define JSON_BODY_MAX (size_t)32767

#ifndef DEFAULT_BITVAVO_REST_URI
#define DEFAULT_BITVAVO_REST_URI "https://api.bitvavo.com"
#endif

#ifndef DEFAULT_BITVAVO_WS_URI
#define DEFAULT_BITVAVO_WS_URI "wss://ws.bitvavo.com"
#endif

#ifndef DEFAULT_BITVAVO_WS_PATH
#define DEFAULT_BITVAVO_WS_PATH "/v2"
#endif

#ifndef DEFAULT_BITVAVO_WS_AUTHENTICATE_PATH
#define DEFAULT_BITVAVO_WS_AUTHENTICATE_PATH "/v2/websocket"
#endif

#ifndef DEFAULT_BITVAVO_ACCOUNTS_PATH
#define DEFAULT_BITVAVO_ACCOUNTS_PATH "/v2/balance"
#endif

#ifndef DEFAULT_BITVAVO_FEES_PATH
#define DEFAULT_BITVAVO_FEES_PATH "/v2/account/fees"
#endif

#ifndef DEFAULT_BITVAVO_MARKETS_PATH
#define DEFAULT_BITVAVO_MARKETS_PATH "/v2/markets"
#endif

#ifndef DEFAULT_BITVAVO_ORDER_PATH
#define DEFAULT_BITVAVO_ORDER_PATH "/v2/order"
#endif

#ifndef DEFAULT_BITVAVO_ORDER_CREATE_PATH
#define DEFAULT_BITVAVO_ORDER_CREATE_PATH "/v2/order"
#endif

#ifndef DEFAULT_BITVAVO_ORDER_CANCEL_PATH
#define DEFAULT_BITVAVO_ORDER_CANCEL_PATH "/v2/order"
#endif

#ifndef DEFAULT_BITVAVO_REQUESTS_PER_SECOND
// 1000 weight points per minute
#define DEFAULT_BITVAVO_REQUESTS_PER_SECOND 16
#endif

#ifndef DEFAULT_BITVAVO_WS_STALL_MILLIS
#define DEFAULT_BITVAVO_WS_STALL_MILLIS 3600000L
#endif

#ifndef DEFAULT_BITVAVO_WS_RETRY_SECONDS
#define DEFAULT_BITVAVO_WS_RETRY_SECONDS 3
#endif

#ifndef nitems
#define nitems(_a) (sizeof((_a)) / sizeof((_a)[0]))
#endif

extern const bool verbose;

extern const struct Numeric *restrict const zero;
extern const struct Numeric *restrict const one;
extern const struct Numeric *restrict const hundred;

static void bitvavo_init(void);
static void bitvavo_configure(const struct ExchangeConfig *restrict const);
static void bitvavo_destroy(void);
static void bitvavo_start(void);
static void bitvavo_stop(void);
static struct Array *bitvavo_markets(void);
static struct Market *bitvavo_market(const struct String *restrict const);
static struct Market *bitvavo_market_by_symbol(struct String *restrict const);
static struct Array *bitvavo_accounts(void);
static struct Account *bitvavo_account(const struct String *restrict const);
static struct Account *bitvavo_account_by_symbol(struct String *restrict const);
static struct Pricing *bitvavo_pricing(const struct Market *restrict const);
static struct Order *bitvavo_order(const struct Market *restrict const,
                                   const struct String *restrict const);
static struct Order *bitvavo_order_await(void);
static struct Sample *bitvavo_sample_await(void);
static bool bitvavo_order_cancel(const struct Market *restrict const,
                                 const struct String *restrict const);
static struct String *bitvavo_order_demand(const struct Market *restrict const,
                                           const char *restrict const,
                                           const char *restrict const);
static struct String *bitvavo_order_supply(const struct Market *restrict const,
                                           const char *restrict const,
                                           const char *restrict const);

struct Exchange exchange_bitvavo = {
    .id = NULL,
    .nm = NULL,
    .init = bitvavo_init,
    .configure = bitvavo_configure,
    .destroy = bitvavo_destroy,
    .start = bitvavo_start,
    .stop = bitvavo_stop,
    .markets = bitvavo_markets,
    .market = bitvavo_market,
    .accounts = bitvavo_accounts,
    .account = bitvavo_account,
    .order = bitvavo_order,
    .order_await = bitvavo_order_await,
    .pricing = bitvavo_pricing,
    .sample_await = bitvavo_sample_await,
    .order_cancel = bitvavo_order_cancel,
    .order_demand = bitvavo_order_demand,
    .order_supply = bitvavo_order_supply,
};

struct bitvavo_tls {
  struct bitvavo_query_accounts_vars {
    struct wcjson_document *restrict rsp_doc;
  } bitvavo_query_accounts;
  struct bitvavo_query_markets_vars {
    struct wcjson_document *restrict rsp_doc;
  } bitvavo_query_markets;
  struct bitvavo_pricing_vars {
    struct wcjson_document *restrict rsp_doc;
  } bitvavo_pricing;
  struct bitvavo_order_vars {
    struct wcjson_document *restrict rsp_doc;
  } bitvavo_order;
  struct bitvavo_order_post_vars {
    struct wcjson_document *restrict rsp_doc;
  } bitvavo_order_post;
  struct bitvavo_order_cancel_vars {
    struct wcjson_document *restrict rsp_doc;
  } bitvavo_order_cancel;
  struct bitvavo_ws_msg_handler_vars {
    struct wcjson_document *restrict msg_doc;
  } bitvavo_ws_msg_handler;
};

static const struct {
  const char *restrict json;
  const enum market_status status;
} market_status_map[] = {
    {"trading", MARKET_STATUS_ONLINE},
    {"halted", MARKET_STATUS_OFFLINE},
    {"auction", MARKET_STATUS_OFFLINE},
    {"auctionMatching", MARKET_STATUS_OFFLINE},
    {"cancelOnly", MARKET_STATUS_OFFLINE},
};

static const struct {
  const char *restrict json;
  const enum order_status status;
} order_status_map[] = {
    {"new", ORDER_STATUS_OPEN},
    {"awaitingTrigger", ORDER_STATUS_PENDING},
    {"canceled", ORDER_STATUS_CANCELLED},
    {"expired", ORDER_STATUS_EXPIRED},
    {"filled", ORDER_STATUS_FILLED},
    {"partiallyFilled", ORDER_STATUS_OPEN},
};

static int bitvavo_ws_worker_func(void *restrict const);
static void bitvavo_ws_evt_handler(struct mg_connection *, int, void *);

static int
bitvavo_ws_auth_evt_handler(struct mg_connection *restrict const,
                            const struct wcjson_document *restrict const,
                            const struct wcjson_value *restrict const);

static int
bitvavo_ws_ticker_evt_handler(struct mg_connection *restrict const,
                              const struct wcjson_document *restrict const,
                              const struct wcjson_value *restrict const);

static int
bitvavo_ws_account_evt_handler(struct mg_connection *restrict const,
                               const struct wcjson_document *restrict const,
                               const struct wcjson_value *restrict const);

static int
bitvavo_ws_subscribed_evt_handler(struct mg_connection *restrict const,
                                  const struct wcjson_document *restrict const,
                                  const struct wcjson_value *restrict const);

static struct {
  const char *restrict evt;
  uint64_t evt_ms;
  const bool may_stall;
  int (*evt_handler)(struct mg_connection *restrict const,
                     const struct wcjson_document *restrict const,
                     const struct wcjson_value *restrict const);
} bitvavo_ws_msg_handlers[] = {
    {
        .evt = "authenticate",
        .evt_ms = 0,
        .evt_handler = bitvavo_ws_auth_evt_handler,
        .may_stall = false,
    },
    {
        .evt = "ticker",
        .evt_ms = 0,
        .evt_handler = bitvavo_ws_ticker_evt_handler,
        .may_stall = true,
    },
    {
        .evt = "order",
        .evt_ms = 0,
        .evt_handler = bitvavo_ws_account_evt_handler,
        .may_stall = true,
    },
    {
        .evt = "fill",
        .evt_ms = 0,
        .evt_handler = bitvavo_ws_account_evt_handler,
        .may_stall = false,
    },
    {
        .evt = "subscribed",
        .evt_ms = 0,
        .evt_handler = bitvavo_ws_subscribed_evt_handler,
        .may_stall = false,
    },
};

static const struct ExchangeConfig *restrict bitvavo_cnf;
static void *restrict bitvavo_db;
static char bitvavo_rest_uri[URI_MAX + 1];
static char bitvavo_rest_accounts_path[URI_MAX + 1];
static char bitvavo_rest_fees_path[URI_MAX + 1];
static char bitvavo_rest_markets_path[URI_MAX + 1];
static char bitvavo_rest_order_path[URI_MAX + 1];
static char bitvavo_rest_order_create_path[URI_MAX + 1];
static char bitvavo_rest_order_cancel_path[URI_MAX + 1];
static struct timespec bitvavo_request_rate;
static char bitvavo_ws_uri[URI_MAX + 1];
static char bitvavo_ws_path[URI_MAX + 1];
static char bitvavo_ws_authenticate_path[URI_MAX + 1];
static unsigned long bitvavo_ws_stall_ms;
static struct timespec bitvavo_ws_retry_rate;

static struct String *restrict bitvavo_access_key;
static struct String *restrict bitvavo_access_timestamp;
static struct String *restrict bitvavo_access_signature;

static tss_t bitvavo_tls_key;

static struct Array *restrict markets;
static struct Map *restrict markets_by_id;
static struct Map *restrict markets_by_symbol;
static _Atomic bool markets_reload;

static struct Array *restrict accounts;
static struct Map *restrict accounts_by_id;
static struct Map *restrict accounts_by_symbol;
static _Atomic bool accounts_reload;

static struct Map *restrict pricings_by_id;

static _Atomic bool running;
static struct Queue *restrict orders;
static struct Queue *restrict samples;
static thrd_t mg_mgr_worker;

static inline void tls_doc_free(struct wcjson_document *restrict const wc_doc) {
  heap_free(wc_doc->values);
  heap_free(wc_doc->strings);
  heap_free(wc_doc->mbstrings);
  heap_free(wc_doc->esc);
  heap_free(wc_doc);
}

static struct bitvavo_tls *const bitvavo_tls(void) {
  struct bitvavo_tls *restrict tls = tss_get(bitvavo_tls_key);
  if (tls == NULL) {
    tls = heap_malloc(sizeof(struct bitvavo_tls));
    tls->bitvavo_query_accounts.rsp_doc =
        heap_calloc(1, sizeof(struct wcjson_document));
    tls->bitvavo_query_markets.rsp_doc =
        heap_calloc(1, sizeof(struct wcjson_document));
    tls->bitvavo_pricing.rsp_doc =
        heap_calloc(1, sizeof(struct wcjson_document));
    tls->bitvavo_order.rsp_doc = heap_calloc(1, sizeof(struct wcjson_document));
    tls->bitvavo_order_post.rsp_doc =
        heap_calloc(1, sizeof(struct wcjson_document));
    tls->bitvavo_order_cancel.rsp_doc =
        heap_calloc(1, sizeof(struct wcjson_document));
    tls->bitvavo_ws_msg_handler.msg_doc =
        heap_calloc(1, sizeof(struct wcjson_document));
    tss_set(bitvavo_tls_key, tls);
  }

  return tls;
}

static void bitvavo_tls_dtor(void *e) {
  struct bitvavo_tls *restrict const tls = e;
  tls_doc_free(tls->bitvavo_query_accounts.rsp_doc);
  tls_doc_free(tls->bitvavo_query_markets.rsp_doc);
  tls_doc_free(tls->bitvavo_pricing.rsp_doc);
  tls_doc_free(tls->bitvavo_order.rsp_doc);
  tls_doc_free(tls->bitvavo_order_post.rsp_doc);
  tls_doc_free(tls->bitvavo_order_cancel.rsp_doc);
  tls_doc_free(tls->bitvavo_ws_msg_handler.msg_doc);
  heap_free(tls);
  tss_set(bitvavo_tls_key, NULL);
}

static enum market_status market_status(const char *restrict const status) {
  for (int i = nitems(market_status_map); i-- > 0;)
    if (!strcmp(market_status_map[i].json, status))
      return market_status_map[i].status;

  return MARKET_STATUS_UNKNOWN;
}

static enum order_status order_status(const char *restrict const status) {
  for (int i = nitems(order_status_map); i-- > 0;)
    if (!strcmp(order_status_map[i].json, status))
      return order_status_map[i].status;

  return ORDER_STATUS_UNKNOWN;
}

inline static void envurl(char *restrict d, size_t len, const char *restrict nm,
                          const char *restrict dflt) {
  const char *restrict env = envuri(nm, dflt);

  while (len-- != 0 && *env)
    *d++ = *env++;

  if (len == SIZE_MAX && *env)
    fatal("%s: %s", nm, env);

  *d = '\0';
}

static void bitvavo_init(void) {
  exchange_bitvavo.id = String_cnew(BITVAVO_UUID);
  exchange_bitvavo.nm = String_cnew("bitvavo");

  bitvavo_access_key = String_cnew("Bitvavo-Access-Key");
  bitvavo_access_timestamp = String_cnew("Bitvavo-Access-Timestamp");
  bitvavo_access_signature = String_cnew("Bitvavo-Access-Signature");

  envurl(bitvavo_rest_uri, sizeof(bitvavo_rest_uri) - 1, "BITVAVO_REST_URI",
         DEFAULT_BITVAVO_REST_URI);

  envurl(bitvavo_ws_uri, sizeof(bitvavo_ws_uri) - 1, "BITVAVO_WS_URI",
         DEFAULT_BITVAVO_WS_URI);

  envurl(bitvavo_rest_accounts_path, sizeof(bitvavo_rest_accounts_path) - 1,
         "BITVAVO_ACCOUNTS_PATH", DEFAULT_BITVAVO_ACCOUNTS_PATH);

  envurl(bitvavo_rest_fees_path, sizeof(bitvavo_rest_fees_path) - 1,
         "BITVAVO_FEES_PATH", DEFAULT_BITVAVO_FEES_PATH);

  envurl(bitvavo_rest_markets_path, sizeof(bitvavo_rest_markets_path) - 1,
         "BITVAVO_MARKETS_PATH", DEFAULT_BITVAVO_MARKETS_PATH);

  envurl(bitvavo_rest_order_path, sizeof(bitvavo_rest_order_path) - 1,
         "BITVAVO_ORDER_PATH", DEFAULT_BITVAVO_ORDER_PATH);

  envurl(bitvavo_rest_order_create_path,
         sizeof(bitvavo_rest_order_create_path) - 1,
         "BITVAVO_ORDER_CREATE_PATH", DEFAULT_BITVAVO_ORDER_CREATE_PATH);

  envurl(bitvavo_rest_order_cancel_path,
         sizeof(bitvavo_rest_order_cancel_path) - 1,
         "BITVAVO_ORDER_CANCEL_PATH", DEFAULT_BITVAVO_ORDER_CANCEL_PATH);

  const unsigned long req_s =
      envul("BITVAVO_REQUESTS_PER_SECOND", DEFAULT_BITVAVO_REQUESTS_PER_SECOND);

  if (req_s == 0)
    fatal("%s == 0", "BITVAVO_REQUESTS_PER_SECOND");

  bitvavo_request_rate.tv_sec = 0;
  bitvavo_request_rate.tv_nsec = 1000000000L / req_s;

  envurl(bitvavo_ws_path, sizeof(bitvavo_ws_path) - 1, "BITVAVO_WS_PATH",
         DEFAULT_BITVAVO_WS_PATH);

  envurl(bitvavo_ws_authenticate_path, sizeof(bitvavo_ws_authenticate_path) - 1,
         "BITVAVO_WS_AUTHENTICATE_PATH", DEFAULT_BITVAVO_WS_AUTHENTICATE_PATH);

  bitvavo_ws_stall_ms =
      envul("BITVAVO_WS_STALL_MILLIS", DEFAULT_BITVAVO_WS_STALL_MILLIS);

  const unsigned long ret_s =
      envul("BITVAVO_WS_RETRY_SECONDS", DEFAULT_BITVAVO_WS_RETRY_SECONDS);

  bitvavo_ws_retry_rate.tv_sec = ret_s;
  bitvavo_ws_retry_rate.tv_nsec = 0;

  if (verbose) {
    wout("\tBITVAVO_REST_URI=%s\n", bitvavo_rest_uri);
    wout("\tBITVAVO_ACCOUNTS_PATH=%s\n", bitvavo_rest_accounts_path);
    wout("\tBITVAVO_FEES_PATH=%s\n", bitvavo_rest_fees_path);
    wout("\tBITVAVO_MARKETS_PATH=%s\n", bitvavo_rest_markets_path);
    wout("\tBITVAVO_ORDER_PATH=%s\n", bitvavo_rest_order_path);
    wout("\tBITVAVO_ORDER_CREATE_PATH=%s\n", bitvavo_rest_order_create_path);
    wout("\tBITVAVO_ORDER_CANCEL_PATH=%s\n", bitvavo_rest_order_cancel_path);
    wout("\tBITVAVO_REQUESTS_PER_SECOND=%lu\n", req_s);
    wout("\tBITVAVO_WS_URI=%s\n", bitvavo_ws_uri);
    wout("\tBITVAVO_WS_PATH=%s\n", bitvavo_ws_path);
    wout("\tBITVAVO_WS_AUTHENTICATE_PATH=%s\n", bitvavo_ws_authenticate_path);
    wout("\tBITVAVO_WS_STALL_MILLIS=%lu\n", bitvavo_ws_stall_ms);
    wout("\tBITVAVO_WS_RETRY_SECONDS=%lu\n", ret_s);
  }

  tss_create(&bitvavo_tls_key, bitvavo_tls_dtor);

  markets = Array_new(1024);
  markets_by_id = Map_new(StringMapOps, 1024);
  markets_by_symbol = Map_new(StringMapOps, 1024);
  markets_reload = true;

  accounts = Array_new(256);
  accounts_by_id = Map_new(StringMapOps, 256);
  accounts_by_symbol = Map_new(StringMapOps, 256);
  accounts_reload = true;

  pricings_by_id = Map_new(StringMapOps, 1024);

  orders = Queue_new(128, (time_t)0);
  samples = Queue_new((MG_MAX_RECV_SIZE) / sizeof(struct Sample *),
                      (time_t)(bitvavo_ws_stall_ms / 1000L));

  for (size_t i = nitems(bitvavo_ws_msg_handlers); i-- > 0;)
    bitvavo_ws_msg_handlers[i].evt_ms = mg_millis();

  running = false;
}

static void bitvavo_configure(const struct ExchangeConfig *restrict const c) {
  bitvavo_cnf = c;
  bitvavo_db = db_connect(BITVAVO_DBCON);
}

static void bitvavo_destroy(void) {
  if (bitvavo_db)
    db_disconnect(bitvavo_db);

  bitvavo_cnf = NULL;
  String_delete(exchange_bitvavo.id);
  String_delete(exchange_bitvavo.nm);
  String_delete(bitvavo_access_key);
  String_delete(bitvavo_access_timestamp);
  String_delete(bitvavo_access_signature);
  tss_delete(bitvavo_tls_key);
  Array_delete(markets, Market_delete);
  Map_delete(markets_by_id, NULL);
  Map_delete(markets_by_symbol, NULL);
  Array_delete(accounts, Account_delete);
  Map_delete(accounts_by_id, NULL);
  Map_delete(accounts_by_symbol, NULL);
  Map_delete(pricings_by_id, Pricing_delete);
  Queue_delete(orders, Order_delete);
  Queue_delete(samples, Sample_delete);
}

static void bitvavo_start(void) {
  char url[URI_MAX];
  int r = snprintf(url, sizeof(url), "%s%s", bitvavo_ws_uri, bitvavo_ws_path);

  if (r < 0 || (size_t)r >= sizeof(url))
    panic();

  struct mg_mgr *restrict const mgr = heap_calloc(1, sizeof(struct mg_mgr));
  mg_mgr_init(mgr);
  mg_mgr_config(mgr);
  mgr->userdata = String_cnew(url);

  Queue_start(orders);
  Queue_start(samples);

  running = true;

  struct mg_connection *restrict const c =
      mg_ws_connect(mgr, url, bitvavo_ws_evt_handler, NULL,
                    "User-Agent: Abagnale; %s\r\n", ABAG_REVISION);

  if (!c)
    fatal("%s: Failure starting websocket\n", url);

  thread_create(&mg_mgr_worker, bitvavo_ws_worker_func, mgr);
}

static void bitvavo_stop(void) {
  running = false;
  Queue_stop(orders);
  Queue_stop(samples);
  thread_join(mg_mgr_worker, NULL);
}

static void bitvavo_signature(char signature[65], const uintmax_t timestamp,
                              const char *restrict const method,
                              const char *restrict const path,
                              const char *restrict const body) {
  uint8_t digest[32];
  char data[URI_MAX];
  const char digits[] = "0123456789abcdef";
  int r = snprintf(data, sizeof(data), "%" PRIuMAX "%s%s%s", timestamp, method,
                   path, body != NULL ? body : "");

  if (r < 0 || (size_t)r >= sizeof(data))
    panic();

  mg_hmac_sha256(digest, (uint8_t *)String_chars(bitvavo_cnf->api_secret),
                 String_length(bitvavo_cnf->api_secret), (uint8_t *)data, r);

  char *restrict sp = signature;

  for (size_t i = 0; i < nitems(digest); i++) {
    *sp++ = digits[digest[i] >> 4];
    *sp++ = digits[digest[i] & 0x0f];
  }

  *sp = '\0';
}

static int bitvavo_rest_query(struct wcjson_document *restrict rsp_doc,
                              const char *restrict const url,
                              const char *restrict const method,
                              const char *restrict const path,
                              const char *restrict const body,
                              const size_t body_len) {
  char signature[65];
  char timestamp[32];
  const uintmax_t now = (uintmax_t)time(NULL) * 1000;
  struct Map *restrict const headers = Map_new(StringMapOps, 4);

  bitvavo_signature(signature, now, method, path, body);

  int r = snprintf(timestamp, sizeof(timestamp), "%" PRIuMAX, now);

  if (r < 0 || (size_t)r >= sizeof(timestamp))
    panic();

  Map_put(headers, bitvavo_access_key,
          (void *)String_chars(bitvavo_cnf->api_key));

  Map_put(headers, bitvavo_access_signature, signature);
  Map_put(headers, bitvavo_access_timestamp, timestamp);

  rsp_doc->v_next = 0;
  rsp_doc->s_next = 0;
  rsp_doc->mb_next = 0;

  r = http_request_json(rsp_doc, url, method, headers, body, body_len);

  Map_delete(headers, NULL);

  return r;
}

static void *
bitvavo_parse_account(const struct wcjson_document *restrict const doc,
                      const struct wcjson_value *restrict const acct) {
  const int saved_errno = errno;
  char a_id[DATABASE_UUID_MAX_LENGTH + 1] = {0};
  struct Account *restrict a = NULL;

  errno = 0;

  struct String *restrict const j_symbol =
      json_obj_get_string(doc, acct, L"symbol", 6);

  struct Numeric *restrict const j_available =
      json_obj_get_string_number(doc, acct, L"available", 9);

  if (errno)
    goto ret;

  db_symbol_to_id(a_id, bitvavo_db, BITVAVO_UUID, String_chars(j_symbol));

  a = Account_new();
  a->id = String_cnew(a_id);
  a->nm = String_copy(j_symbol);
  a->sym = j_symbol;
  a->type = ACCOUNT_TYPE_NONE;
  a->avail = j_available;
  a->is_active = true;
  a->is_ready = true;

  errno = 0;
ret:
  if (a == NULL) {
    String_delete(j_symbol);
    Numeric_delete(j_available);
  }

  if (errno)
    werr("%s: account: %s\n", bitvavo_rest_uri, strerror(errno));

  errno = saved_errno;
  return a;
}

static struct Pricing *
bitvavo_parse_fee(const struct wcjson_document *restrict const doc) {
  const int saved_errno = errno;
  struct Pricing *restrict p = NULL;

  errno = 0;

  struct String *restrict const j_tier =
      json_obj_get_string(doc, doc->values, L"tier", 4);

  struct Numeric *restrict const j_taker =
      json_obj_get_string_number(doc, doc->values, L"taker", 5);

  struct Numeric *restrict const j_maker =
      json_obj_get_string_number(doc, doc->values, L"maker", 5);

  if (errno)
    goto ret;

  p = Pricing_new();
  p->nm = j_tier;
  p->tf_pc = Numeric_mul(j_taker, hundred);
  p->mf_pc = Numeric_mul(j_maker, hundred);
  p->ef_pc = Numeric_cmp(p->tf_pc, p->mf_pc) > 0 ? Numeric_copy(p->tf_pc)
                                                 : Numeric_copy(p->mf_pc);

  errno = 0;
ret:
  if (p == NULL)
    String_delete(j_tier);

  Numeric_delete(j_taker);
  Numeric_delete(j_maker);

  if (errno)
    werr("%s: fee: %s\n", bitvavo_rest_uri, strerror(errno));

  errno = saved_errno;
  return p;
}

static void *
bitvavo_parse_market(const struct wcjson_document *restrict const doc,
                     const struct wcjson_value *restrict const market) {
  int r;
  const int saved_errno = errno;
  char m_id[DATABASE_UUID_MAX_LENGTH + 1] = {0};
  char inc[42] = {0};
  char nm[4096] = {0};
  char err[JSON_BODY_MAX + 1] = {0};
  size_t err_nitems = nitems(err);
  struct Market *restrict m = NULL;

  errno = 0;

  struct String *restrict const j_market =
      json_obj_get_string(doc, market, L"market", 6);

  struct String *restrict const j_status =
      json_obj_get_string(doc, market, L"status", 6);

  struct String *restrict const j_base =
      json_obj_get_string(doc, market, L"base", 4);

  struct String *restrict const j_quote =
      json_obj_get_string(doc, market, L"quote", 5);

  struct Numeric *restrict const j_quantityDecimals =
      json_obj_get_string_number(doc, market, L"quantityDecimals", 16);

  struct Numeric *restrict const j_notionalDecimals =
      json_obj_get_string_number(doc, market, L"notionalDecimals", 16);

  struct Numeric *restrict const j_tickSize =
      json_obj_get_string_number(doc, market, L"tickSize", 8);

  struct String *restrict const j_tickSize_s =
      json_obj_get_string(doc, market, L"tickSize", 8);

  struct Numeric *restrict const j_minOrderInBaseAsset =
      json_obj_get_string_number(doc, market, L"minOrderInBaseAsset", 19);

  struct Numeric *restrict const j_maxOrderInBaseAsset =
      json_obj_get_string_number(doc, market, L"maxOrderInBaseAsset", 19);

  struct Numeric *restrict const j_minOrderInQuoteAsset =
      json_obj_get_string_number(doc, market, L"minOrderInQuoteAsset", 20);

  struct Numeric *restrict const j_maxOrderInQuoteAsset =
      json_obj_get_string_number(doc, market, L"maxOrderInQuoteAsset", 20);

  if (errno)
    goto ret;

  db_symbol_to_id(m_id, bitvavo_db, BITVAVO_UUID, String_chars(j_market));

  // Extract price scale from tickSize
  const char *restrict const t_dot = strchr(String_chars(j_tickSize_s), '.');
  const uintmax_t p_sc = t_dot ? strlen(t_dot + 1) : 0;
  String_delete(j_tickSize_s);

  // Find matching accounts required for trading.
  const struct Account *restrict qa = bitvavo_account_by_symbol(j_quote);
  const struct Account *restrict ba = bitvavo_account_by_symbol(j_base);

  struct String *restrict qa_id = NULL;
  struct String *restrict ba_id = NULL;
  bool qa_active_and_ready = false;
  bool ba_active_and_ready = false;

  if (qa != NULL) {
    qa_id = String_copy(qa->id);
    qa_active_and_ready = qa->is_active && qa->is_ready;
    mutex_unlock(qa->mtx);
    qa = NULL;
  }

  if (ba != NULL) {
    ba_id = String_copy(ba->id);
    ba_active_and_ready = ba->is_active && ba->is_ready;
    mutex_unlock(ba->mtx);
    ba = NULL;
  }

  uintmax_t quoteScale = Numeric_to_long(j_notionalDecimals);
  uintmax_t baseScale = Numeric_to_long(j_quantityDecimals);
  struct Numeric *restrict quoteIncrement;
  struct Numeric *restrict baseIncrement;

  if (quoteScale > 0) {
    r = snprintf(inc, sizeof(inc), "0.%.*" PRIuMAX "1", (int)(quoteScale - 1),
                 (uintmax_t)0);

    if (r < 0 || (size_t)r >= sizeof(inc))
      panic();

    quoteIncrement = Numeric_from_char(inc);

    if (quoteIncrement == NULL)
      panic();

  } else
    quoteIncrement = Numeric_copy(one);

  if (baseScale > 0) {
    r = snprintf(inc, sizeof(inc), "0.%.*" PRIuMAX "1", (int)(baseScale - 1),
                 (uintmax_t)0);

    if (r < 0 || (size_t)r >= sizeof(inc))
      panic();

    baseIncrement = Numeric_from_char(inc);

    if (baseIncrement == NULL)
      panic();

  } else
    baseIncrement = Numeric_copy(one);

  r = snprintf(nm, sizeof(nm), "%s@%s", String_chars(j_base),
               String_chars(j_quote));

  if (r < 0 || (size_t)r >= sizeof(nm))
    panic();

  //  bool order_type_market = false;
  bool order_type_limit = false;
  //  bool order_type_sl = false;
  //  bool order_type_sll = false;
  //  bool order_type_tp = false;
  //  bool order_type_tpl = false;

  const struct wcjson_value *restrict const j_orderTypes =
      wcjson_object_get(doc, market, L"orderTypes", 10);

  if (j_orderTypes == NULL || !j_orderTypes->is_array) {
    if (json_mbsprint(err, &err_nitems, doc, market) < 0) {
      r = snprintf(err, err_nitems, "%s", strerror(errno));
      if (r < 0 || (size_t)r >= err_nitems)
        panic();
    }
    werr("%s: %s: market: No 'orderTypes' array item: %s %s\n",
         bitvavo_rest_uri, nm, m_id, err);
    goto ret;
  }

  const struct wcjson_value *restrict j_orderType = NULL;
  wcjson_value_foreach(j_orderType, doc, j_orderTypes) {
    if (!j_orderType->is_string) {
      if (json_mbsprint(err, &err_nitems, doc, market) < 0) {
        r = snprintf(err, err_nitems, "%s", strerror(errno));
        if (r < 0 || (size_t)r >= err_nitems)
          panic();
      }
      werr("%s: %s: market: No 'orderType' string item: %s %s\n",
           bitvavo_rest_uri, nm, m_id, err);
      goto ret;
    }

    if (strcmp(j_orderType->mbstring, "market") == 0) {
      //      order_type_market = true;
      continue;
    }
    if (strcmp(j_orderType->mbstring, "limit") == 0) {
      order_type_limit = true;
      continue;
    }
    if (strcmp(j_orderType->mbstring, "stopLoss") == 0) {
      //      order_type_sl = true;
      continue;
    }
    if (strcmp(j_orderType->mbstring, "stopLossLimit") == 0) {
      //      order_type_sll = true;
      continue;
    }
    if (strcmp(j_orderType->mbstring, "takeProfit") == 0) {
      //      order_type_tp = true;
      continue;
    }
    if (strcmp(j_orderType->mbstring, "takeProfitLimit") == 0) {
      //      order_type_tpl = true;
      continue;
    }

    werr("%s: %s: market: Unsupported type: %s %s\n", bitvavo_rest_uri, nm,
         m_id, j_orderType->mbstring);
  }

  m = Market_new();
  m->id = String_cnew(m_id);
  m->b_id = j_base;
  m->ba_id = ba_id;
  m->q_id = j_quote;
  m->qa_id = qa_id;
  m->nm = String_cnew(nm);
  m->sym = j_market;
  m->type = MARKET_TYPE_NONE;
  m->status = market_status(String_chars(j_status));
  m->p_sc = p_sc;
  m->p_inc = j_tickSize;
  m->b_sc = Numeric_to_long(j_quantityDecimals);
  m->b_inc = baseIncrement;
  m->q_sc = Numeric_to_long(j_notionalDecimals);
  m->q_inc = quoteIncrement;
  m->b_min_opt = j_minOrderInBaseAsset;
  m->b_max_opt = j_maxOrderInBaseAsset;
  m->q_min_opt = j_minOrderInQuoteAsset;
  m->q_max_opt = j_maxOrderInQuoteAsset;
  m->is_tradeable = qa_id != NULL && ba_id != NULL && order_type_limit;
  m->is_active = m->status == MARKET_STATUS_ONLINE;

  if (m->status == MARKET_STATUS_UNKNOWN)
    werr("%s: %s: market: Unsupported status: %s %s\n", bitvavo_rest_uri, nm,
         m_id, String_chars(j_status));

  m->is_tradeable = m->is_tradeable && qa_active_and_ready;
  m->is_tradeable = m->is_tradeable && ba_active_and_ready;

#ifdef ABAG_BITVAVO_DEBUG
  if (!m->is_active)
    wout("%s: %s: Market not active: %s\n", bitvavo_rest_uri, nm, m_id);
#endif

  errno = 0;
ret:
  if (m == NULL) {
    String_delete(j_market);
    String_delete(j_base);
    String_delete(j_quote);
    Numeric_delete(j_tickSize);
    Numeric_delete(j_minOrderInBaseAsset);
    Numeric_delete(j_maxOrderInBaseAsset);
    Numeric_delete(j_minOrderInQuoteAsset);
    Numeric_delete(j_maxOrderInQuoteAsset);
  }

  String_delete(j_status);
  Numeric_delete(j_quantityDecimals);
  Numeric_delete(j_notionalDecimals);

  if (errno)
    werr("%s: market: %s\n", bitvavo_rest_uri, strerror(errno));

  errno = saved_errno;
  return m;
}

static struct Order *
bitvavo_parse_order(const struct wcjson_document *restrict const doc,
                    const struct wcjson_value *restrict const order) {
  const int saved_errno = errno;
  struct Order *restrict o = NULL;
  struct Market *restrict m = NULL;

  errno = 0;

  struct String *restrict const j_orderId =
      json_obj_get_string(doc, order, L"orderId", 7);

  struct String *restrict const j_market =
      json_obj_get_string(doc, order, L"market", 6);

  struct String *restrict const j_status =
      json_obj_get_string(doc, order, L"status", 6);

  struct Numeric *restrict const j_amount =
      json_obj_get_string_number(doc, order, L"amount", 6);

  struct Numeric *restrict const j_filledAmount =
      json_obj_get_string_number(doc, order, L"filledAmount", 12);

  struct Numeric *restrict const j_filledAmountQuote =
      json_obj_get_string_number(doc, order, L"filledAmountQuote", 17);

  struct Numeric *restrict const j_price =
      json_obj_get_string_number(doc, order, L"price", 5);

  struct Numeric *restrict const j_feePaid =
      json_obj_get_string_number(doc, order, L"feePaid", 7);

  struct String *restrict const j_feeCurrency =
      json_obj_get_string(doc, order, L"feeCurrency", 11);

  struct Numeric *restrict const j_createdNs =
      json_obj_get_number(doc, order, L"createdNs", 9);

  struct Numeric *restrict const j_updatedNs =
      json_obj_get_number(doc, order, L"updatedNs", 9);

  if (errno)
    goto ret;

  m = bitvavo_market_by_symbol(j_market);

  if (m == NULL) {
    werr("%s: %s: order: Market not available: %s\n", bitvavo_rest_uri,
         String_chars(j_market), String_chars(j_orderId));
    goto ret;
  }

  if (!String_equals(j_feeCurrency, m->q_id)) {
    werr("%s: %s: order: Unsupported fee currency: %s %s\n", bitvavo_rest_uri,
         String_chars(j_market), String_chars(j_orderId),
         String_chars(j_feeCurrency));
    mutex_unlock(m->mtx);
    goto ret;
  }

  o = Order_new();
  o->id = j_orderId;
  o->m_id = String_copy(m->id);
  o->status = order_status(String_chars(j_status));
  o->settled = o->status == ORDER_STATUS_FILLED;
  o->cnanos = j_createdNs;
  o->dnanos = j_updatedNs;
  o->b_ordered = j_amount;
  o->p_ordered = j_price;
  o->b_filled = j_filledAmount;
  o->q_filled = j_filledAmountQuote;
  o->q_fees = j_feePaid;
  o->msg = NULL;

  mutex_unlock(m->mtx);

  if (o->status == ORDER_STATUS_UNKNOWN)
    werr("%s: %s: order: Unsupported status: %s %s\n", bitvavo_rest_uri,
         String_chars(j_market), String_chars(j_orderId),
         String_chars(j_status));

  errno = 0;
ret:
  if (o == NULL) {
    String_delete(j_orderId);
    Numeric_delete(j_amount);
    Numeric_delete(j_filledAmount);
    Numeric_delete(j_filledAmountQuote);
    Numeric_delete(j_price);
    Numeric_delete(j_feePaid);
    Numeric_delete(j_createdNs);
    Numeric_delete(j_updatedNs);
  }

  String_delete(j_market);
  String_delete(j_status);
  String_delete(j_feeCurrency);

  if (errno)
    werr("%s: order: %s\n", bitvavo_rest_uri, strerror(errno));

  errno = saved_errno;
  return o;
}

static int bitvavo_rest_parse_entities(
    struct Array *restrict const entities, const char *restrict const nm,
    const struct wcjson_document *restrict const doc,
    void *(*parse)(const struct wcjson_document *restrict const,
                   const struct wcjson_value *restrict const)) {
  const int saved_errno = errno;
  char err[JSON_BODY_MAX + 1] = {0};
  size_t err_nitems = nitems(err);
  int ret = -1;

  if (!doc->values->is_array) {
    if (json_mbsprint(err, &err_nitems, doc, doc->values)) {
      int r = snprintf(err, err_nitems, "%s", strerror(errno));
      if (r < 0 || (size_t)r >= err_nitems)
        panic();
    }
    werr("%s: %s: event: No array item: %s\n", bitvavo_rest_uri, nm, err);
    goto ret;
  }

  const struct wcjson_value *restrict j_entity = NULL;
  wcjson_value_foreach(j_entity, doc, doc->values) {
    void *restrict const entity = parse(doc, j_entity);

    if (entity == NULL)
      goto ret;

    Array_add_tail(entities, entity);
  }

  errno = 0;
  ret = 0;
ret:
  if (errno)
    werr("%s: %s: event: %s\n", bitvavo_rest_uri, nm, strerror(errno));

  errno = saved_errno;
  return ret;
}

static int bitvavo_rest_query_accounts(struct Array *restrict const a,
                                       const char *restrict const sym) {
  char url[URI_MAX];
  int r;
  const struct bitvavo_tls *restrict const tls = bitvavo_tls();
  struct wcjson_document *restrict rsp_doc =
      tls->bitvavo_query_accounts.rsp_doc;

  if (sym != NULL) {
    r = snprintf(url, sizeof(url), "%s%s?symbol=%s", bitvavo_rest_uri,
                 bitvavo_rest_accounts_path, sym);
  } else
    r = snprintf(url, sizeof(url), "%s%s", bitvavo_rest_uri,
                 bitvavo_rest_accounts_path);

  if (r < 0 || (size_t)r >= sizeof(url))
    panic();

  // Rate limit weight points: 5
  thread_sleep(&bitvavo_request_rate);
  thread_sleep(&bitvavo_request_rate);
  thread_sleep(&bitvavo_request_rate);
  thread_sleep(&bitvavo_request_rate);
  thread_sleep(&bitvavo_request_rate);

  if (bitvavo_rest_query(rsp_doc, url, "GET", mg_url_uri(url), NULL, 0) < 0)
    return -1;

  if (bitvavo_rest_parse_entities(a, "accounts", rsp_doc,
                                  bitvavo_parse_account) < 0)
    return -1;

  return 0;
}

static int bitvavo_rest_query_markets(struct Array *restrict const m) {
  const struct bitvavo_tls *restrict const tls = bitvavo_tls();
  struct wcjson_document *restrict rsp_doc = tls->bitvavo_query_markets.rsp_doc;
  char url[URI_MAX];
  int r = snprintf(url, sizeof(url), "%s%s", bitvavo_rest_uri,
                   bitvavo_rest_markets_path);

  if (r < 0 || (size_t)r >= sizeof(url))
    panic();

  // Rate limit weight points: 1
  thread_sleep(&bitvavo_request_rate);

  if (bitvavo_rest_query(rsp_doc, url, "GET", mg_url_uri(url), NULL, 0) < 0)
    return -1;

  if (bitvavo_rest_parse_entities(m, "markets", rsp_doc, bitvavo_parse_market) <
      0)
    return -1;

  return 0;
}

static struct Array *bitvavo_markets(void) {
  void *const *restrict items;

  Array_lock(markets);

  if (markets_reload) {
    accounts_reload = true;

    Array_clear(markets, Market_delete);

    if (bitvavo_rest_query_markets(markets) < 0)
      goto ret;

    Array_compact(markets);

    Map_delete(markets_by_symbol, NULL);
    Map_delete(markets_by_id, NULL);

    markets_by_symbol = Map_new(StringMapOps, Array_size(markets));
    markets_by_id = Map_new(StringMapOps, Array_size(markets));

    items = Array_items(markets);
    for (size_t i = Array_size(markets); i-- > 0;) {
      if (Map_put(markets_by_symbol, ((struct Market *)items[i])->sym,
                  items[i]))
        fatal("%s: markets: Market symbol uniqueness constraint: %s",
              bitvavo_rest_uri, String_chars(((struct Market *)items[i])->sym));

      if (Map_put(markets_by_id, ((struct Market *)items[i])->id, items[i]))
        fatal("%s: markets: Market id uniqueness constraint: %s",
              bitvavo_rest_uri, String_chars(((struct Market *)items[i])->id));
    }

    markets_reload = false;
  }
ret:
  return markets;
}

static struct Market *bitvavo_market(const struct String *restrict const m_id) {
  struct Array *restrict const m_array = bitvavo_markets();
  struct Market *restrict m = Map_get(markets_by_id, m_id);

  if (m != NULL) {
    m->mtx = Array_mutex(m_array);
    return m;
  }

  Array_unlock(m_array);
  return NULL;
}

static struct Market *
bitvavo_market_by_symbol(struct String *restrict const m_sym) {
  struct Array *restrict const m_array = bitvavo_markets();
  struct Market *restrict const m = Map_get(markets_by_symbol, m_sym);

  if (m != NULL) {
    m->mtx = Array_mutex(m_array);
    return m;
  }

  Array_unlock(m_array);
  return NULL;
}

static struct Array *bitvavo_accounts(void) {
  void *const *restrict items;

  Array_lock(accounts);

  if (accounts_reload) {
    Array_clear(accounts, Account_delete);

    if (bitvavo_rest_query_accounts(accounts, NULL) < 0)
      goto ret;

    Array_compact(accounts);

    Map_delete(accounts_by_id, NULL);
    Map_delete(accounts_by_symbol, NULL);

    accounts_by_id = Map_new(StringMapOps, Array_size(accounts));
    accounts_by_symbol = Map_new(StringMapOps, Array_size(accounts));

    items = Array_items(accounts);
    for (size_t i = Array_size(accounts); i-- > 0;) {
      if (Map_put(accounts_by_symbol, ((struct Account *)items[i])->sym,
                  items[i]) != NULL)
        fatal("%s: accounts: Account symbol uniqueness constraint: %s",
              bitvavo_rest_uri,
              String_chars(((struct Account *)items[i])->sym));

      if (Map_put(accounts_by_id, ((struct Account *)items[i])->id, items[i]) !=
          NULL)
        fatal("%s: accounts: Account id uniqueness constraint: %s",
              bitvavo_rest_uri, String_chars(((struct Account *)items[i])->id));
    }

    accounts_reload = false;
  }
ret:
  return accounts;
}

static struct Account *
bitvavo_account(const struct String *restrict const a_id) {
  struct Array *restrict const a_array = Array_new(2);
  struct Array *restrict const bitvavo_accts = bitvavo_accounts();
  struct Account *restrict const bitvavo_acct = Map_get(accounts_by_id, a_id);
  struct Account *restrict a = NULL;

  if (bitvavo_acct == NULL)
    goto ret;

  if (bitvavo_rest_query_accounts(a_array, String_chars(bitvavo_acct->sym)) < 0)
    goto ret;

  if (Array_size(a_array) > 1)
    panic();

  a = Array_head(a_array);

  // See comment in bitvavo_account_by_symbol
  if (a == NULL)
    a = Account_copy(bitvavo_acct);

ret:
  Array_unlock(bitvavo_accts);
  Array_delete(a_array, NULL);
  return a;
}

static struct Account *
bitvavo_account_by_symbol(struct String *restrict const sym) {
  char a_id[DATABASE_UUID_MAX_LENGTH + 1] = {0};
  struct Array *restrict const a_array = bitvavo_accounts();
  struct Account *restrict a = Map_get(accounts_by_symbol, sym);

  /*
   * There does not seem to be any endpoint providing a way to query an
   * account exists, is ready and active. The balance endpoint "returns the
   * balance for all assets above zero". An assumption is made that Bitvavo
   * never provides market symbols the user cannot trade due to account
   * restrictions.
   */

  if (a == NULL) {
    db_symbol_to_id(a_id, bitvavo_db, BITVAVO_UUID, String_chars(sym));

    a = Account_new();
    a->id = String_cnew(a_id);
    a->nm = String_copy(sym);
    a->sym = String_copy(sym);
    a->type = ACCOUNT_TYPE_NONE;
    a->avail = Numeric_copy(zero);
    a->is_active = true;
    a->is_ready = true;

    Array_add_tail(a_array, a);

    if (Map_put(accounts_by_symbol, a->sym, a) != NULL)
      panic();

    if (Map_put(accounts_by_id, a->id, a) != NULL)
      panic();
  }

  a->mtx = Array_mutex(a_array);
  return a;
}

static struct Pricing *bitvavo_pricing(const struct Market *restrict const m) {

  Map_lock(pricings_by_id);
  struct Pricing *restrict p = Map_get(pricings_by_id, m->id);

  if (p != NULL) {
    p->mtx = Map_mutex(pricings_by_id);
    return p;
  }

  const struct bitvavo_tls *restrict const tls = bitvavo_tls();
  struct wcjson_document *restrict rsp_doc = tls->bitvavo_pricing.rsp_doc;
  char url[URI_MAX];
  int r = snprintf(url, sizeof(url), "%s%s?market=%s", bitvavo_rest_uri,
                   bitvavo_rest_fees_path, String_chars(m->sym));

  if (r < 0 || (size_t)r >= sizeof(url))
    panic();

  // Rate limit weight points: 1
  thread_sleep(&bitvavo_request_rate);

  if (bitvavo_rest_query(rsp_doc, url, "GET", mg_url_uri(url), NULL, 0) < 0)
    goto ret;

  p = bitvavo_parse_fee(rsp_doc);
ret:
  if (p == NULL) {
    p = Pricing_new();
    p->nm = String_cnew("fallback");
    p->tf_pc = Numeric_from_char("0.25");
    p->mf_pc = Numeric_from_char("0.25");
    p->ef_pc = Numeric_from_char("0.25");
  }

  Map_put(pricings_by_id, m->id, p);
  p->mtx = Map_mutex(pricings_by_id);
  return p;
}

static struct Order *bitvavo_order(const struct Market *restrict const m,
                                   const struct String *restrict const o_id) {
  const struct bitvavo_tls *restrict const tls = bitvavo_tls();
  struct wcjson_document *restrict rsp_doc = tls->bitvavo_order.rsp_doc;
  char url[URI_MAX];
  int r = snprintf(url, sizeof(url), "%s%s?market=%s&orderId=%s",
                   bitvavo_rest_uri, bitvavo_rest_order_path,
                   String_chars(m->sym), String_chars(o_id));

  if (r < 0 || (size_t)r >= sizeof(url))
    panic();

  // Rate limit weight points: 1
  thread_sleep(&bitvavo_request_rate);

  if (bitvavo_rest_query(rsp_doc, url, "GET", mg_url_uri(url), NULL, 0) < 0)
    return NULL;

  return bitvavo_parse_order(rsp_doc, rsp_doc->values);
}

static int bitvavo_order_create_request(
    char *restrict const mb, size_t *restrict const mb_len,
    const char *restrict const url, const struct String *restrict const m_sym,
    const struct String *restrict const side,
    const char *restrict const base_amount, const size_t b_len,
    const char *restrict const price, const size_t p_len) {
  const int saved_errno = errno;
  struct wcjson wc_json = WCJSON_INITIALIZER;
  int r = -1;

  struct wcjson_document doc = {
      .values = (struct wcjson_value[24]){{0}},
      .v_nitems = 24,
  };

  errno = 0;

  struct wcjson_value *restrict const j_req = wcjson_value_object(&doc);
  struct wcjson_value *restrict const j_market =
      wcjson_value_mbstring(&doc, String_chars(m_sym), String_length(m_sym));

  struct wcjson_value *restrict const j_side =
      wcjson_value_mbstring(&doc, String_chars(side), String_length(side));

  struct wcjson_value *restrict const j_orderType =
      wcjson_value_string(&doc, L"limit", 5);

  struct wcjson_value *restrict const j_amount =
      wcjson_value_mbstring(&doc, base_amount, b_len);

  struct wcjson_value *restrict const j_price =
      wcjson_value_mbstring(&doc, price, p_len);

  struct wcjson_value *restrict const j_operatorId =
      wcjson_value_number(&doc, BITVAVO_OPERATOR_ID, BITVAVO_OPERATOR_ID_LEN);

  wcjson_object_add_tail(&doc, j_req, L"market", 6, j_market);
  wcjson_object_add_tail(&doc, j_req, L"side", 4, j_side);
  wcjson_object_add_tail(&doc, j_req, L"orderType", 9, j_orderType);
  wcjson_object_add_tail(&doc, j_req, L"amount", 6, j_amount);
  wcjson_object_add_tail(&doc, j_req, L"price", 5, j_price);
  wcjson_object_add_tail(&doc, j_req, L"operatorId", 10, j_operatorId);

  if (errno)
    goto ret;

  if (wcjson_document_build(&wc_json, &doc) < 0)
    goto ret;

  if (json_mbsprint(mb, mb_len, &doc, doc.values) < 0)
    goto ret;

  r = 0;
  errno = 0;
ret:
  heap_free(doc.strings);
  heap_free(doc.mbstrings);
  heap_free(doc.esc);

  if (errno)
    werr("%s: create: %s\n", url, strerror(errno));

  errno = saved_errno;
  return r;
}

static struct String *bitvavo_order_post(const char *restrict const m_sym,
                                         const char *restrict const side,
                                         const char *restrict const base_amount,
                                         const char *restrict const price) {
  const int saved_errno = errno;
  const struct bitvavo_tls *restrict const tls = bitvavo_tls();
  struct wcjson_document *restrict rsp_doc = tls->bitvavo_order_post.rsp_doc;
  struct String *restrict sym = String_cnew(m_sym);
  struct String *restrict sd = String_cnew(side);
  char url[URI_MAX + 1] = {0};
  char mb[JSON_BODY_MAX + 1] = {0};
  size_t mb_len = nitems(mb);
  struct String *restrict j_orderId = NULL;
  int r = snprintf(url, sizeof(url), "%s%s", bitvavo_rest_uri,
                   bitvavo_rest_order_create_path);

  if (r < 0 || (size_t)r >= sizeof(url))
    panic();

  errno = 0;

  if (bitvavo_order_create_request(mb, &mb_len, url, sym, sd, base_amount,
                                   strlen(base_amount), price,
                                   strlen(price)) < 0)
    goto ret;

  // Rate limit weight points: 1
  thread_sleep(&bitvavo_request_rate);

  errno = 0;

  if (bitvavo_rest_query(rsp_doc, url, "POST", mg_url_uri(url), mb, mb_len) < 0)
    goto ret;

  j_orderId = json_obj_get_string(rsp_doc, rsp_doc->values, L"orderId", 7);

  if (errno)
    goto ret;

  errno = 0;
ret:
  String_delete(sym);
  String_delete(sd);

  if (errno)
    werr("%s: create: %s\n", url, strerror(errno));

  errno = saved_errno;
  return j_orderId;
}

static struct String *
bitvavo_order_demand(const struct Market *restrict const m,
                     const char *restrict const base,
                     const char *restrict const price) {
  return bitvavo_order_post(String_chars(m->sym), "buy", base, price);
}

static struct String *
bitvavo_order_supply(const struct Market *restrict const m,
                     const char *restrict const base,
                     const char *restrict const price) {
  return bitvavo_order_post(String_chars(m->sym), "sell", base, price);
}

static bool bitvavo_order_cancel(const struct Market *restrict const m,
                                 const struct String *restrict const o_id) {
  const struct bitvavo_tls *restrict const tls = bitvavo_tls();
  const int saved_errno = errno;
  bool ret = false;
  char url[URI_MAX + 1] = {0};
  struct String *restrict j_orderId = NULL;
  struct wcjson_document *restrict const rsp_doc =
      tls->bitvavo_order_cancel.rsp_doc;

  int r =
      snprintf(url, sizeof(url), "%s%s?market=%s&orderId=%s&operatorId=%ls",
               bitvavo_rest_uri, bitvavo_rest_order_cancel_path,
               String_chars(m->sym), String_chars(o_id), BITVAVO_OPERATOR_ID);

  if (r < 0 || (size_t)r >= sizeof(url))
    panic();

  if (bitvavo_rest_query(rsp_doc, url, "DELETE", mg_url_uri(url), NULL, 0) < 0)
    goto ret;

  errno = 0;
  j_orderId = json_obj_get_string(rsp_doc, rsp_doc->values, L"orderId", 9);

  if (!String_equals(o_id, j_orderId))
    panic();

  if (errno)
    goto ret;

  errno = 0;
  ret = true;
ret:
  String_delete(j_orderId);

  if (errno)
    werr("%s: cancel: %s\n", url, strerror(errno));

  errno = saved_errno;
  return ret;
}

static struct Sample *bitvavo_sample_await(void) {
  struct Sample *restrict const s = Queue_dequeue_await(samples);

  if (Queue_dequeue_timedout(samples))
    werr("%s: Dequeuing ticker timed out after %" PRIdMAX " seconds\n",
         bitvavo_ws_uri, (intmax_t)(bitvavo_ws_stall_ms / 1000L));

  return s;
}

static struct Order *bitvavo_order_await(void) {
  return Queue_dequeue_await(orders);
}

static int bitvavo_ws_worker_func(void *restrict const arg) {
  struct mg_mgr *restrict const mgr = arg;

  while (running)
    mg_mgr_poll(mgr, bitvavo_ws_stall_ms / 4);

  String_delete(mgr->userdata);
  mg_mgr_free(mgr);
  heap_free(mgr);
  thread_exit(EXIT_SUCCESS);
}

static int bitvavo_ws_authenticate(struct mg_connection *restrict const c) {
  char signature[65];
  char timestamp[32];
  const int saved_errno = errno;
  int ret = -1;
  struct wcjson wc_json = WCJSON_INITIALIZER;
  struct wcjson_document req_doc = WCJSON_DOCUMENT_INITIALIZER;
  const uintmax_t now = (uintmax_t)time(NULL) * 1000;
  int r = snprintf(timestamp, sizeof(timestamp), "%" PRIuMAX, now);

  if (r < 0 || (size_t)r >= sizeof(timestamp))
    panic();

  bitvavo_signature(signature, now, "GET", bitvavo_ws_authenticate_path, NULL);

  req_doc.v_nitems = 16;
  req_doc.values = heap_reallocarray(req_doc.values, req_doc.v_nitems,
                                     sizeof(struct wcjson_value));

  errno = 0;

  struct wcjson_value *restrict const j_action = wcjson_value_object(&req_doc);

  wcjson_object_add_tail(&req_doc, j_action, L"action", 6,
                         wcjson_value_string(&req_doc, L"authenticate", 12));

  wcjson_object_add_tail(
      &req_doc, j_action, L"key", 3,
      wcjson_value_mbstring(&req_doc, String_chars(bitvavo_cnf->api_key),
                            String_length(bitvavo_cnf->api_key)));

  wcjson_object_add_tail(&req_doc, j_action, L"signature", 9,
                         wcjson_value_mbstring(&req_doc, signature, 65));

  wcjson_object_add_tail(&req_doc, j_action, L"timestamp", 9,
                         wcjson_value_mbnumber(&req_doc, timestamp, r));

  if (errno)
    goto ret;

  if (wcjson_document_build(&wc_json, &req_doc) < 0) {
    werr("%s: authenticate: %lu %s\n", String_chars(c->mgr->userdata), c->id,
         json_mbserror(&wc_json));
    goto ret;
  }

  char req_body[JSON_BODY_MAX + 1] = {0};
  size_t req_len = sizeof(req_body);

  json_mbsprint(req_body, &req_len, &req_doc, j_action);

  if (errno)
    goto ret;

  if (!mg_ws_send(c, req_body, req_len, WEBSOCKET_OP_TEXT))
    goto ret;
#ifdef ABAG_BITVAVO_DEBUG
  wout("%s: %.*s\n", String_chars(c->mgr->userdata), (int)req_len, req_body);
#endif
  errno = 0;
  ret = 0;
ret:
  heap_free(req_doc.values);
  heap_free(req_doc.strings);
  heap_free(req_doc.mbstrings);
  heap_free(req_doc.esc);

  if (errno)
    werr("%s: authenticate: %lu %s\n", String_chars(c->mgr->userdata), c->id,
         strerror(errno));

  errno = saved_errno;
  return ret;
}

static int bitvavo_ws_subscribe(struct mg_connection *restrict const c) {
  void *const *restrict items;
  const int saved_errno = errno;
  struct wcjson wc_json = WCJSON_INITIALIZER;
  struct wcjson_document req_doc = WCJSON_DOCUMENT_INITIALIZER;
  int ret = -1;

  markets_reload = true;
  struct Array *restrict const m_array = bitvavo_markets();

  req_doc.v_nitems = 16;

  if (req_doc.v_nitems > SIZE_MAX - Array_size(m_array))
    panic();

  req_doc.v_nitems += Array_size(m_array);
  req_doc.values = heap_reallocarray(req_doc.values, req_doc.v_nitems,
                                     sizeof(struct wcjson_value));

  errno = 0;

  struct wcjson_value *restrict const j_action = wcjson_value_object(&req_doc);
  struct wcjson_value *restrict const j_markets = wcjson_value_array(&req_doc);
  struct wcjson_value *restrict const j_channels = wcjson_value_array(&req_doc);

  items = Array_items(m_array);
  for (size_t i = Array_size(m_array); i-- > 0;) {
    const struct Market *restrict const m = items[i];
    wcjson_array_add_tail(&req_doc, j_markets,
                          wcjson_value_mbstring(&req_doc, String_chars(m->sym),
                                                String_length(m->sym)));
  }
  Array_unlock(m_array);

  struct wcjson_value *restrict const j_ticker = wcjson_value_object(&req_doc);
  struct wcjson_value *restrict const j_account = wcjson_value_object(&req_doc);

  wcjson_object_add_tail(&req_doc, j_ticker, L"name", 4,
                         wcjson_value_string(&req_doc, L"ticker", 6));

  wcjson_object_add_tail(&req_doc, j_ticker, L"markets", 7, j_markets);

  wcjson_object_add_tail(&req_doc, j_account, L"name", 4,
                         wcjson_value_string(&req_doc, L"account", 7));

  wcjson_object_add_tail(&req_doc, j_account, L"markets", 7, j_markets);

  wcjson_array_add_tail(&req_doc, j_channels, j_ticker);
  wcjson_array_add_tail(&req_doc, j_channels, j_account);

  wcjson_object_add_tail(&req_doc, j_action, L"action", 6,
                         wcjson_value_string(&req_doc, L"subscribe", 9));

  wcjson_object_add_tail(&req_doc, j_action, L"channels", 8, j_channels);

  if (errno)
    goto ret;

  if (wcjson_document_build(&wc_json, &req_doc)) {
    werr("%s: subscribe: %lu %s\n", String_chars(c->mgr->userdata), c->id,
         json_mbserror(&wc_json));
    goto ret;
  }

  char req_body[JSON_BODY_MAX + 1] = {0};
  size_t req_len = sizeof(req_body);

  json_mbsprint(req_body, &req_len, &req_doc, j_action);

  if (errno)
    goto ret;

  if (!mg_ws_send(c, req_body, req_len, WEBSOCKET_OP_TEXT))
    goto ret;
#ifdef ABAG_BITVAVO_DEBUG
  wout("%s: %.*s\n", String_chars(c->mgr->userdata), (int)req_len, req_body);
#endif
  errno = 0;
  ret = 0;
ret:
  heap_free(req_doc.values);
  heap_free(req_doc.strings);
  heap_free(req_doc.mbstrings);
  heap_free(req_doc.esc);

  if (errno)
    werr("%s: subscribe: %lu %s\n", String_chars(c->mgr->userdata), c->id,
         strerror(errno));

  errno = saved_errno;
  return ret;
}

static int
bitvavo_ws_msg_handler(struct mg_connection *restrict const c,
                       const struct mg_ws_message *restrict const msg) {
  const struct bitvavo_tls *restrict const tls = bitvavo_tls();
  struct wcjson_document *restrict const msg_doc =
      tls->bitvavo_ws_msg_handler.msg_doc;

  const int saved_errno = errno;
  int ret = -1;
  struct String *restrict j_event = NULL;

  msg_doc->v_next = 0;
  msg_doc->s_next = 0;
  msg_doc->mb_next = 0;

  errno = 0;

  if (json_mbparse(msg_doc, msg->data.buf, msg->data.len) < 0)
    goto ret;

  j_event = json_obj_get_string(msg_doc, msg_doc->values, L"event", 5);

  if (errno)
    goto ret;

  bool handled = false;
  const char *restrict const evt = String_chars(j_event);
  for (size_t i = nitems(bitvavo_ws_msg_handlers); i-- > 0;)
    if (strcmp(evt, bitvavo_ws_msg_handlers[i].evt) == 0) {
      handled = true;
      bitvavo_ws_msg_handlers[i].evt_ms = mg_millis();
      if (bitvavo_ws_msg_handlers[i].evt_handler(c, msg_doc, msg_doc->values) <
          0)
        goto ret;
    }

  if (!handled)
    werr("%s: event: %lu %.*s\n", String_chars(c->mgr->userdata), c->id,
         (int)msg->data.len, msg->data.buf);

  errno = 0;
  ret = 0;
ret:
  String_delete(j_event);

  if (errno)
    werr("%s: event: %lu %s\n", String_chars(c->mgr->userdata), c->id,
         strerror(errno));

  errno = saved_errno;
  return ret;
}

static void bitvavo_ws_evt_handler(struct mg_connection *c, int ev,
                                   void *ev_data) {
  switch (ev) {
  case MG_EV_CONNECT: {
#ifdef ABAG_BITVAVO_DEBUG
    wout("%s: %lu MG_EV_CONNECT\n", String_chars(c->mgr->userdata), c->id);
#endif
    struct mg_tls_opts ws_tls_opts = {0};
    ws_tls_opts.name = mg_url_host(bitvavo_ws_uri);
    mg_tls_init(c, &ws_tls_opts);
    break;
  }
  case MG_EV_ERROR: {
    werr("%s: event: %lu %s\n", String_chars(c->mgr->userdata), c->id,
         (char *)ev_data);
    c->is_closing = 1;
    break;
  }
  case MG_EV_WS_OPEN: {
#ifdef ABAG_BITVAVO_DEBUG
    wout("%s: %lu MG_EV_WS_OPEN\n", String_chars(c->mgr->userdata), c->id);
#endif
    if (running) {
      if (bitvavo_ws_authenticate(c) < 0)
        c->is_closing = 1;
    } else
      c->is_closing = 1;

    break;
  }
  case MG_EV_WS_MSG: {
    const struct mg_ws_message *restrict const msg = ev_data;
    const uint8_t type = msg->flags & 0x0F;

    if (running) {
      if (type == WEBSOCKET_OP_TEXT) {
        if (bitvavo_ws_msg_handler(c, msg) < 0)
          c->is_closing = 1;

      } else if (type == WEBSOCKET_OP_CLOSE) {
#ifdef ABAG_BITVAVO_DEBUG
        wout("%s: %lu WEBSOCKET_OP_CLOSE\n", String_chars(c->mgr->userdata),
             c->id);
#endif
        c->is_closing = 1;
      } else
        werr("%s: event: %lu %d\n", String_chars(c->mgr->userdata), c->id,
             type);

    } else
      c->is_closing = 1;

    break;
  }
  case MG_EV_CLOSE: {
#ifdef ABAG_BITVAVO_DEBUG
    wout("%s: %lu MG_EV_CLOSE\n", String_chars(c->mgr->userdata), c->id);
#endif
    heap_free(c->fn_data);

    if (running) {
      struct mg_mgr *restrict const mgr = c->mgr;

      do {
        thread_sleep(&bitvavo_ws_retry_rate);
        c = mg_ws_connect(mgr, String_chars(mgr->userdata),
                          bitvavo_ws_evt_handler, NULL,
                          "User-Agent: Abagnale; %s\r\n", ABAG_REVISION);
        if (!c)
          werr("%s: Failure reconnecting\n", String_chars(mgr->userdata));

      } while (!c);
    }
    break;
  }
  }

  const uint64_t now = mg_millis();
  for (size_t i = nitems(bitvavo_ws_msg_handlers); i-- > 0;)
    if (bitvavo_ws_msg_handlers[i].may_stall &&
        bitvavo_ws_msg_handlers[i].evt_ms &&
        now - bitvavo_ws_msg_handlers[i].evt_ms > bitvavo_ws_stall_ms) {
      c->is_closing = 1;
      bitvavo_ws_msg_handlers[i].evt_ms = now;

      if (verbose)
        wout("%s: %s: No events\n", String_chars(c->mgr->userdata),
             bitvavo_ws_msg_handlers[i].evt);

      break;
    }
}

static int
bitvavo_ws_auth_evt_handler(struct mg_connection *restrict const c,
                            const struct wcjson_document *restrict const doc,
                            const struct wcjson_value *restrict const evt) {
  const int saved_errno = errno;
  int ret = -1;

  errno = 0;

  const bool j_authenticated =
      json_obj_get_bool(doc, evt, L"authenticated", 13);

  if (errno)
    goto ret;

  if (!j_authenticated) {
    c->is_closing = 1;
    werr("%s: Failure authenticating\n", String_chars(c->mgr->userdata));
    goto ret;
  }

  if (bitvavo_ws_subscribe(c) < 0)
    goto ret;

  errno = 0;
  ret = 0;
ret:
  if (errno)
    werr("%s: authenticate: %s\n", String_chars(c->mgr->userdata),
         strerror(errno));

  errno = saved_errno;
  return ret;
}

static int
bitvavo_ws_ticker_evt_handler(struct mg_connection *restrict const c,
                              const struct wcjson_document *restrict const doc,
                              const struct wcjson_value *restrict const evt) {
  struct Sample *restrict s = NULL;
  const int saved_errno = errno;
  int ret = -1;

  errno = 0;

  struct String *restrict const j_market =
      json_obj_get_string(doc, evt, L"market", 6);

  struct Numeric *restrict const j_lastPrice =
      json_obj_get_optional_string_number(doc, evt, L"lastPrice", 9);

  if (errno)
    goto ret;

  if (j_lastPrice == NULL || Numeric_cmp(j_lastPrice, zero) == 0)
    goto ok;

  struct Market *restrict const m = bitvavo_market_by_symbol(j_market);

  if (m == NULL) {
    werr("%s: %s: ticker: Market not available\n",
         String_chars(c->mgr->userdata), String_chars(j_market));
    goto ret;
  }

  s = Sample_new();
  s->m_id = String_copy(m->id);
  s->price = j_lastPrice;
  s->nanos = Numeric_new();
  nanos_now(s->nanos);

  mutex_unlock(m->mtx);

  Queue_enqueue_await(samples, s);

  if (Queue_enqueue_timedout(samples)) {
    werr("%s: Enqueuing ticker timed out after %" PRIdMAX " seconds\n",
         String_chars(c->mgr->userdata),
         (intmax_t)(bitvavo_ws_stall_ms / 1000L));

    Sample_delete(s);
    goto ret;
  }

ok:
  errno = 0;
  ret = 0;
ret:
  if (s == NULL)
    Numeric_delete(j_lastPrice);

  String_delete(j_market);

  if (errno)
    werr("%s: ticker: %s\n", String_chars(c->mgr->userdata), strerror(errno));

  errno = saved_errno;
  return ret;
}

static int
bitvavo_ws_account_evt_handler(struct mg_connection *restrict const c,
                               const struct wcjson_document *restrict const doc,
                               const struct wcjson_value *restrict const evt) {
  const int saved_errno = errno;
  struct Order *restrict o = NULL;
  struct Market *restrict m = NULL;
  int ret = -1;

#ifdef ABAG_BITVAVO_DEBUG
  char buf[JSON_BODY_MAX + 1] = {0};
  size_t buf_nitems = nitems(buf);

  if (json_mbsprint(buf, &buf_nitems, doc, evt) < 0) {
    int r = snprintf(buf, buf_nitems, "%s", strerror(errno));
    if (r < 0 || (size_t)r >= buf_nitems)
      panic();
  }

  wout("%s: account: %s\n", String_chars(c->mgr->userdata), buf);
#endif

  errno = 0;

  struct String *restrict const j_orderId =
      json_obj_get_string(doc, evt, L"orderId", 7);

  struct String *restrict const j_market =
      json_obj_get_string(doc, evt, L"market", 6);

  struct String *restrict const j_restatementReason =
      json_obj_get_optional_string(doc, evt, L"restatementReason", 17);

  if (errno)
    goto ret;

  m = bitvavo_market_by_symbol(j_market);

  if (m == NULL) {
    werr("%s: %s: account: Market not available: %s\n",
         String_chars(c->mgr->userdata), String_chars(j_market),
         String_chars(j_orderId));
    goto ret;
  }

  o = bitvavo_order(m, j_orderId);

  mutex_unlock(m->mtx);

  if (o == NULL)
    goto ret;

  o->msg = j_restatementReason;

  Queue_enqueue_await(orders, o);

  errno = 0;
  ret = 0;
ret:
  if (o == NULL)
    String_delete(j_restatementReason);

  String_delete(j_orderId);
  String_delete(j_market);

  if (errno)
    werr("%s: account: %s\n", String_chars(c->mgr->userdata), strerror(errno));

  errno = saved_errno;
  return ret;
}

static int bitvavo_ws_subscribed_evt_handler(
    struct mg_connection *restrict const c,
    const struct wcjson_document *restrict const doc,
    const struct wcjson_value *restrict const evt) {
  char err[JSON_BODY_MAX + 1] = {0};
  size_t err_nitems = nitems(err);
  int ret = -1;

  const struct wcjson_value *restrict const j_subscriptions =
      wcjson_object_get(doc, evt, L"subscriptions", 13);

  if (j_subscriptions == NULL || !j_subscriptions->is_object) {
    if (json_mbsprint(err, &err_nitems, doc, evt) < 0) {
      int r = snprintf(err, err_nitems, "%s", strerror(errno));
      if (r < 0 || (size_t)r >= err_nitems)
        panic();
    }
    werr("%s: subscribed: No 'subscriptions' object item: %s\n",
         String_chars(c->mgr->userdata), err);
    goto ret;
  }

  if (verbose) {
    struct wcjson_value *restrict j_subscription = NULL;
    wcjson_value_foreach(j_subscription, doc, j_subscriptions) {
      wout("%s: Subscription: %ls\n", String_chars(c->mgr->userdata),
           j_subscription->string);
    }
  }

  ret = 0;
ret:
  return ret;
}
