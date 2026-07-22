/* $SchulteIT: exchange-coinbase.c 15282 2025-11-05 22:54:21Z schulte $ */
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

#include "charset.h"
#include "config.h"
#include "database.h"
#include "exchange.h"
#include "heap.h"
#include "http.h"
#include "mongoose-ext.h"
#include "proc.h"
#include "queue.h"
#include "thread.h"
#include "time.h"

#include <errno.h>
#include <inttypes.h>
#include <string.h>

#ifndef DEFAULT_CDP_WS_URI
#define DEFAULT_CDP_WS_URI "wss://advanced-trade-ws.coinbase.com"
#endif

#ifndef DEFAULT_CDP_REST_URI
#define DEFAULT_CDP_REST_URI "https://api.coinbase.com"
#endif

#ifndef DEFAULT_CDP_ACCOUNT_PATH
#define DEFAULT_CDP_ACCOUNT_PATH "/api/v3/brokerage/accounts/"
#endif

#ifndef DEFAULT_CDP_ACCOUNTS_PATH
#define DEFAULT_CDP_ACCOUNTS_PATH "/api/v3/brokerage/accounts"
#endif

#ifndef DEFAULT_CDP_FEES_PATH
#define DEFAULT_CDP_FEES_PATH "/api/v3/brokerage/transaction_summary"
#endif

#ifndef DEFAULT_CDP_ORDER_PATH
#define DEFAULT_CDP_ORDER_PATH "/api/v3/brokerage/orders/historical/"
#endif

#ifndef DEFAULT_CDP_ORDER_CANCEL_PATH
#define DEFAULT_CDP_ORDER_CANCEL_PATH "/api/v3/brokerage/orders/batch_cancel"
#endif

#ifndef DEFAULT_CDP_ORDER_CREATE_PATH
#define DEFAULT_CDP_ORDER_CREATE_PATH "/api/v3/brokerage/orders"
#endif

#ifndef DEFAULT_CDP_PRODUCTS_PATH
#define DEFAULT_CDP_PRODUCTS_PATH "/api/v3/brokerage/products"
#endif

#ifndef DEFAULT_CDP_HTTP_REQUESTS_PER_SECOND
#define DEFAULT_CDP_HTTP_REQUESTS_PER_SECOND 30
#endif

#ifndef DEFAULT_CDP_HTTP_RETRY_SECONDS
#define DEFAULT_CDP_HTTP_RETRY_SECONDS 3
#endif

#ifndef DEFAULT_CDP_HTTP_STALL_MILLIS
#define DEFAULT_CDP_HTTP_STALL_MILLIS 3600000L
#endif

#ifndef DEFAULT_CDP_HTTP_TIMEOUT_MILLIS
#define DEFAULT_CDP_HTTP_TIMEOUT_MILLIS 60000L
#endif

#define COINBASE_UUID "74cc13c5-4835-491b-95f2-6af672ad141a"
#define COINBASE_DBCON "coinbase"

#define URL_MAX_LENGTH (size_t)512
#define JSON_BODY_MAX (size_t)32767
#define HTTP_RESPONSE_MAX_WCHARS (size_t)5242880

#ifndef nitems
#define nitems(_a) (sizeof((_a)) / sizeof((_a)[0]))
#endif

struct coinbase_tls {
  struct ws_handle_message_vars {
    struct wcjson_document *restrict ws_doc;
  } ws_handle_message;
  struct coinbase_markets_vars {
    struct wcjson_document *restrict rsp_doc;
  } coinbase_markets;
  struct accounts_with_cursor_vars {
    struct wcjson_document *restrict rsp_doc;
  } accounts_with_cursor;
  struct coinbase_account_vars {
    struct wcjson_document *restrict rsp_doc;
  } coinbase_account;
  struct coinbase_order_vars {
    struct wcjson_document *restrict rsp_doc;
  } coinbase_order;
  struct coinbase_order_cancel_vars {
    struct wcjson_document *restrict rsp_doc;
  } coinbase_order_cancel;
  struct coinbase_order_post_vars {
    struct wcjson_document *restrict rsp_doc;
  } coinbase_order_post;
  struct coinbase_pricing_vars {
    struct wcjson_document *restrict rsp_doc;
  } coinbase_pricing;
};

static const struct {
  const char *restrict json;
  const enum order_status status;
} order_status_map[] = {
    {"PENDING", ORDER_STATUS_PENDING},
    {"OPEN", ORDER_STATUS_OPEN},
    {"FILLED", ORDER_STATUS_FILLED},
    {"CANCELLED", ORDER_STATUS_CANCELLED},
    {"EXPIRED", ORDER_STATUS_EXPIRED},
    {"FAILED", ORDER_STATUS_FAILED},
    {"UNKNOWN_ORDER_STATUS", ORDER_STATUS_UNKNOWN},
    {"QUEUED", ORDER_STATUS_QUEUED},
    {"CANCEL_QUEUED", ORDER_STATUS_CANCEL_QUEUED},
};

static const struct {
  const char *restrict json;
  const enum account_type type;
} account_type_map[] = {
    {"ACCOUNT_TYPE_UNSPECIFIED", ACCOUNT_TYPE_UNSPECIFIED},
    {"ACCOUNT_TYPE_CRYPTO", ACCOUNT_TYPE_CRYPTO},
    {"ACCOUNT_TYPE_FIAT", ACCOUNT_TYPE_FIAT},
    {"ACCOUNT_TYPE_VAULT", ACCOUNT_TYPE_VAULT},
    {"ACCOUNT_TYPE_PERP_FUTURES", ACCOUNT_TYPE_PERP_FUTURES},
};

static const struct {
  const char *restrict json;
  const enum market_type type;
} market_type_map[] = {
    {"UNKNOWN_PRODUCT_TYPE", MARKET_TYPE_UNKNOWN},
    {"SPOT", MARKET_TYPE_SPOT},
    {"FUTURE", MARKET_TYPE_FUTURE},
};

static const struct {
  const char *restrict json;
  const enum market_status status;
} market_status_map[] = {
    {"online", MARKET_STATUS_ONLINE},
    {"delisted", MARKET_STATUS_DELISTED},
};

extern const struct Numeric *restrict const zero;
extern const struct Numeric *restrict const hundred;
extern const bool verbose;

static const struct ExchangeConfig *restrict coinbase_cnf;
static char coinbase_ws_uri[URL_MAX_LENGTH + 1];
static char coinbase_rest_uri[URL_MAX_LENGTH + 1];
static struct timespec coinbase_request_rate;
static struct timespec coinbase_retry_rate;
static char coinbase_account_path[URL_MAX_LENGTH + 1];
static char coinbase_accounts_path[URL_MAX_LENGTH + 1];
static char coinbase_fees_path[URL_MAX_LENGTH + 1];
static char coinbase_order_path[URL_MAX_LENGTH + 1];
static char coinbase_order_cancel_path[URL_MAX_LENGTH + 1];
static char coinbase_order_create_path[URL_MAX_LENGTH + 1];
static char coinbase_products_path[URL_MAX_LENGTH + 1];
static unsigned long coinbase_stall_ms;
static void *restrict coinbase_db;

static struct Array *restrict markets;
static struct Map *restrict markets_by_symbol;
static struct Map *restrict markets_by_id;
static _Atomic bool markets_reload;

static struct Array *restrict accounts;
static struct Map *restrict accounts_by_id;
static struct Map *restrict accounts_by_currency;
static _Atomic bool accounts_reload;

static struct Pricing *restrict pricing;
static mtx_t pricing_mutex;

static _Atomic bool running;
static struct Queue *restrict orders;
static struct Queue *restrict samples;
static tss_t coinbase_tls_key;
static thrd_t mg_mgr_worker;

static void coinbase_init(void);
static void coinbase_configure(const struct ExchangeConfig *restrict const);
static void coinbase_destroy(void);
static void coinbase_start(void);
static void coinbase_stop(void);
static struct Order *coinbase_order_await(void);
static struct Sample *coinbase_sample_await(void);
static struct Pricing *coinbase_pricing(void);
static struct Array *coinbase_markets(void);
static struct Market *coinbase_market(const struct String *restrict const);
static struct Market *
coinbase_market_by_symbol(const struct String *restrict const);
static struct Array *coinbase_accounts(void);
static struct Account *
coinbase_account_currency(const struct String *restrict const);
static struct Account *coinbase_account(const struct String *restrict const);
static struct Order *coinbase_order(const struct String *restrict const);
static bool coinbase_order_cancel(const struct String *restrict const);
static struct String *coinbase_order_demand(const char *restrict const,
                                            const char *restrict const,
                                            const char *restrict const);
static struct String *coinbase_order_supply(const char *restrict const,
                                            const char *restrict const,
                                            const char *restrict const);

static void ws_status_update(const struct wcjson_document *restrict const,
                             const struct wcjson_value *restrict const,
                             const struct Numeric *restrict const);
static void ws_ticker_update(const struct wcjson_document *restrict const,
                             const struct wcjson_value *restrict const,
                             const struct Numeric *restrict const);
static void ws_user_update(const struct wcjson_document *restrict const,
                           const struct wcjson_value *restrict const,
                           const struct Numeric *restrict const);

struct Exchange exchange_coinbase = {
    .id = NULL,
    .nm = NULL,
    .init = coinbase_init,
    .configure = coinbase_configure,
    .destroy = coinbase_destroy,
    .start = coinbase_start,
    .stop = coinbase_stop,
    .order_await = coinbase_order_await,
    .order_cancel = coinbase_order_cancel,
    .sample_await = coinbase_sample_await,
    .pricing = coinbase_pricing,
    .markets = coinbase_markets,
    .market = coinbase_market,
    .accounts = coinbase_accounts,
    .account = coinbase_account,
    .order = coinbase_order,
    .order_demand = coinbase_order_demand,
    .order_supply = coinbase_order_supply,
};

static struct ws_channel {
  const char *restrict const name;
  const wchar_t *restrict const items;
  const size_t items_len;
  uint64_t last_message;
  bool debug;
  bool _Atomic reconnect;
  void (*update)(const struct wcjson_document *restrict const,
                 const struct wcjson_value *restrict const,
                 const struct Numeric *restrict const);
  void (*snapshot)(const struct wcjson_document *restrict const,
                   const struct wcjson_value *restrict const,
                   const struct Numeric *restrict const);
} ws_channels[] = {
    {
        .name = "heartbeats",
        .items = NULL,
        .items_len = 0,
        .last_message = 0,
        .debug = false,
        .reconnect = false,
        .update = NULL,
        .snapshot = NULL,
    },
    {
        .name = "status",
        .items = L"products",
        .items_len = 8,
        .last_message = 0,
        .debug = true,
        .reconnect = false,
        .update = ws_status_update,
        .snapshot = NULL,
    },
    {
        .name = "subscriptions",
        .items = NULL,
        .items_len = 0,
        .last_message = 0,
        .debug = true,
        .reconnect = false,
        .update = NULL,
        .snapshot = NULL,
    },
    {
        .name = "ticker",
        .items = L"tickers",
        .items_len = 7,
        .last_message = 0,
        .debug = false,
        .reconnect = false,
        .update = ws_ticker_update,
        .snapshot = NULL,
    },
    {
        .name = "user",
        .items = L"orders",
        .items_len = 6,
        .last_message = 0,
        .debug = true,
        .reconnect = false,
        .update = ws_user_update,
        .snapshot = NULL,
    },
};

static inline void tls_doc_free(struct wcjson_document *restrict const wc_doc) {
  heap_free(wc_doc->values);
  heap_free(wc_doc->strings);
  heap_free(wc_doc->mbstrings);
  heap_free(wc_doc->esc);
  heap_free(wc_doc);
}

static struct coinbase_tls *const coinbase_tls(void) {
  struct coinbase_tls *restrict tls = tls_get(coinbase_tls_key);
  if (tls == NULL) {
    tls = heap_malloc(sizeof(struct coinbase_tls));
    tls->ws_handle_message.ws_doc =
        heap_calloc(1, sizeof(struct wcjson_document));
    tls->coinbase_markets.rsp_doc =
        heap_calloc(1, sizeof(struct wcjson_document));
    tls->accounts_with_cursor.rsp_doc =
        heap_calloc(1, sizeof(struct wcjson_document));
    tls->coinbase_account.rsp_doc =
        heap_calloc(1, sizeof(struct wcjson_document));
    tls->coinbase_order.rsp_doc =
        heap_calloc(1, sizeof(struct wcjson_document));
    tls->coinbase_order_cancel.rsp_doc =
        heap_calloc(1, sizeof(struct wcjson_document));
    tls->coinbase_order_post.rsp_doc =
        heap_calloc(1, sizeof(struct wcjson_document));
    tls->coinbase_pricing.rsp_doc =
        heap_calloc(1, sizeof(struct wcjson_document));
    tls_set(coinbase_tls_key, tls);
  }

  return tls;
}

static void coinbase_tls_dtor(void *e) {
  struct coinbase_tls *restrict const tls = e;
  tls_doc_free(tls->ws_handle_message.ws_doc);
  tls_doc_free(tls->coinbase_markets.rsp_doc);
  tls_doc_free(tls->accounts_with_cursor.rsp_doc);
  tls_doc_free(tls->coinbase_account.rsp_doc);
  tls_doc_free(tls->coinbase_order.rsp_doc);
  tls_doc_free(tls->coinbase_order_cancel.rsp_doc);
  tls_doc_free(tls->coinbase_order_post.rsp_doc);
  tls_doc_free(tls->coinbase_pricing.rsp_doc);
  heap_free(tls);
  tls_set(coinbase_tls_key, NULL);
}

static enum order_status order_status(const char *restrict status) {
  for (int i = nitems(order_status_map); i-- > 0;)
    if (!strcmp(order_status_map[i].json, status))
      return order_status_map[i].status;

  return ORDER_STATUS_UNKNOWN;
}

static enum market_type market_type(const char *restrict const type) {
  for (int i = nitems(market_type_map); i-- > 0;)
    if (!strcmp(market_type_map[i].json, type))
      return market_type_map[i].type;

  return MARKET_TYPE_UNKNOWN;
}

static enum market_status market_status(const char *restrict const status) {
  for (int i = nitems(market_status_map); i-- > 0;)
    if (!strcmp(market_status_map[i].json, status))
      return market_status_map[i].status;

  return MARKET_STATUS_UNKNOWN;
}

static enum account_type account_type(const char *restrict const type) {
  for (int i = nitems(account_type_map); i-- > 0;)
    if (!strcmp(account_type_map[i].json, type))
      return account_type_map[i].type;

  return ACCOUNT_TYPE_UNSPECIFIED;
}

static struct ws_channel *ws_channel(const char *restrict const name) {
  for (size_t i = nitems(ws_channels); i-- > 0;)
    if (!strcmp(ws_channels[i].name, name))
      return &ws_channels[i];

  return NULL;
}

static int jwt_encode_cdp(char *restrict const jwt, size_t *restrict jwt_lenp,
                          const char *restrict const uri) {
  int r;
  uint8_t ec_key[32];
  char claims[JSON_BODY_MAX + 1] = {0};
  const struct mg_str cnf_key = mg_str(String_chars(coinbase_cnf->jwt_key));
  const size_t key_len =
      mg_uecc_parse_private_key(cnf_key, ec_key, sizeof(ec_key));

  if (key_len == 0) {
    werr("coinbase: cdp-api-key: Unsupported private key\n");
    return -1;
  }

  const time_t now = time(NULL);

  if (uri) {
    r = snprintf(
        claims, sizeof(claims),
        "{\"iss\":\"cdp\",\"sub\":\"%s\",\"uri\":\"%s\",\"nbf\":%" PRIdMAX
        ",\"exp\":%" PRIdMAX "}",
        String_chars(coinbase_cnf->jwt_kid), uri, (intmax_t)now - 1,
        (intmax_t)now + 120);

  } else {
    r = snprintf(claims, sizeof(claims),
                 "{\"iss\":\"cdp\",\"sub\":\"%s\",\"nbf\":%" PRIdMAX
                 ",\"exp\":%" PRIdMAX "}",
                 String_chars(coinbase_cnf->jwt_kid), (intmax_t)now - 1,
                 (intmax_t)now + 120);
  }

  if (r < 0 || (size_t)r >= sizeof(claims))
    panic();

  struct mg_jwt_opts jwt_opts = {0};
  jwt_opts.claims = mg_str(claims);
  jwt_opts.private_key = ec_key;
  jwt_opts.kid = mg_str(String_chars(coinbase_cnf->jwt_kid));

  const size_t jwt_len = mg_jwt_sign_es256(&jwt_opts, jwt, *jwt_lenp);

  if (jwt_len == 0)
    goto err_range;

  *jwt_lenp = jwt_len;
  return 0;
err_range:
  errno = ERANGE;
  return -1;
}

static int mg_mgr_worker_func(void *restrict const arg) {
  struct mg_mgr *restrict const mgr = arg;

  while (running)
    mg_mgr_poll(mgr, coinbase_stall_ms / 4);

  mg_mgr_free(mgr);
  heap_free(mgr);
  thread_exit(EXIT_SUCCESS);
}

static void ws_ticker_update(const struct wcjson_document *restrict const doc,
                             const struct wcjson_value *restrict const ticker,
                             const struct Numeric *restrict const nanos) {
  const int saved_errno = errno;
  struct Sample *restrict s = NULL;
  struct Market *restrict m = NULL;

  errno = 0;

  struct String *restrict const j_product_id =
      json_obj_get_optional_string(doc, ticker, L"product_id", 10);

  struct Numeric *restrict const j_price =
      json_obj_get_optional_string_number(doc, ticker, L"price", 5);

  if (errno || j_product_id == NULL || j_price == NULL)
    goto ret;

  m = coinbase_market_by_symbol(j_product_id);

  if (m == NULL) {
    for (size_t i = nitems(ws_channels); i-- > 0;)
      ws_channels[i].reconnect = true;
    goto ret;
  }

  s = Sample_new();
  s->m_id = String_copy(m->id);
  s->nanos = Numeric_copy(nanos);
  s->price = j_price;

  mutex_unlock(m->mtx);

  Queue_enqueue_await(samples, s);

  if (Queue_enqueue_timedout(samples)) {
    werr("%s: Enqueuing ticker timed out after %" PRIdMAX " seconds\n",
         coinbase_ws_uri, (intmax_t)(coinbase_stall_ms / 1000L));

    Sample_delete(s);
    goto ret;
  }

  errno = 0;
ret:
  if (s == NULL)
    Numeric_delete(j_price);

  String_delete(j_product_id);

  if (errno)
    werr("%s: user: %s\n", coinbase_ws_uri, strerror(errno));

  errno = saved_errno;
}

static void ws_status_update(const struct wcjson_document *restrict const doc,
                             const struct wcjson_value *restrict const product,
                             const struct Numeric *restrict const nanos) {
  for (size_t i = nitems(ws_channels); i-- > 0;)
    ws_channels[i].reconnect = true;
}

static void ws_user_update(const struct wcjson_document *restrict const doc,
                           const struct wcjson_value *restrict const order,
                           const struct Numeric *restrict const nanos) {
  const int saved_errno = errno;
  struct Order *restrict o = NULL;
  struct Market *restrict m = NULL;
  struct String *restrict m_id = NULL;

  errno = 0;

  struct String *restrict const j_order_id =
      json_obj_get_string(doc, order, L"order_id", 8);

  struct String *restrict const j_product_id =
      json_obj_get_string(doc, order, L"product_id", 10);

  struct Numeric *restrict const j_cumulative_quantity =
      json_obj_get_string_number(doc, order, L"cumulative_quantity", 19);

  struct Numeric *restrict const j_leaves_quantity =
      json_obj_get_string_number(doc, order, L"leaves_quantity", 15);

  struct Numeric *restrict const j_filled_value =
      json_obj_get_string_number(doc, order, L"filled_value", 12);

  struct Numeric *restrict const j_total_fees =
      json_obj_get_string_number(doc, order, L"total_fees", 10);

  struct Numeric *restrict const j_outstanding_hold_amount =
      json_obj_get_string_number(doc, order, L"outstanding_hold_amount", 23);

  struct Numeric *restrict const j_creation_time =
      json_obj_get_string_iso8601(doc, order, L"creation_time", 13);

  struct String *restrict const j_status =
      json_obj_get_string(doc, order, L"status", 6);

  struct Numeric *restrict const j_limit_price =
      json_obj_get_string_number(doc, order, L"limit_price", 11);

  struct String *restrict const j_reject_reason =
      json_obj_get_optional_string(doc, order, L"reject_Reason", 13);

  struct String *restrict const j_cancel_reason =
      json_obj_get_optional_string(doc, order, L"cancel_reason", 13);

  if (errno)
    goto ret;

  const enum order_status status = order_status(String_chars(j_status));

  if ((status == ORDER_STATUS_CANCELLED || status == ORDER_STATUS_FAILED ||
       status == ORDER_STATUS_EXPIRED) &&
      Numeric_cmp(j_outstanding_hold_amount, zero) != 0)
    goto ret;

  m = coinbase_market_by_symbol(j_product_id);

  if (m == NULL) {
    werr("%s: user: %s: Market not available: %s\n", coinbase_ws_uri,
         String_chars(j_order_id), String_chars(j_product_id));

    for (size_t i = nitems(ws_channels); i-- > 0;)
      ws_channels[i].reconnect = true;

    goto ret;
  }

  m_id = String_copy(m->id);
  mutex_unlock(m->mtx);

  struct String *restrict msg = NULL;

  if (j_reject_reason != NULL && String_length(j_reject_reason) != 0) {
    msg = j_reject_reason;
    String_delete(j_cancel_reason);
  } else if (j_cancel_reason != NULL && String_length(j_cancel_reason) != 0) {
    msg = j_cancel_reason;
    String_delete(j_reject_reason);
  }

  if (status == ORDER_STATUS_UNKNOWN) {
    werr("%s: user: %s: Order status unknown: %s\n", coinbase_ws_uri,
         String_chars(j_order_id), String_chars(j_status));
  }

  o = Order_new();
  o->id = j_order_id;
  o->m_id = m_id;
  o->status = status;
  o->cnanos = j_creation_time;
  o->b_ordered = Numeric_add(j_cumulative_quantity, j_leaves_quantity);
  o->p_ordered = j_limit_price;
  o->b_filled = j_cumulative_quantity;
  o->q_filled = j_filled_value;
  o->q_fees = j_total_fees;
  o->msg = msg;
  o->settled = Numeric_cmp(j_leaves_quantity, zero) == 0 &&
               Numeric_cmp(j_outstanding_hold_amount, zero) == 0 &&
               Numeric_cmp(o->b_ordered, o->b_filled) == 0;
  o->dnanos = o->settled ? Numeric_copy(nanos) : NULL;

  Queue_enqueue_await(orders, o);
  errno = 0;
ret:
  if (o == NULL) {
    String_delete(m_id);
    String_delete(j_order_id);
    Numeric_delete(j_filled_value);
    Numeric_delete(j_total_fees);
    Numeric_delete(j_creation_time);
    Numeric_delete(j_limit_price);
    String_delete(j_reject_reason);
    String_delete(j_cancel_reason);
  }

  String_delete(j_product_id);
  String_delete(j_status);
  Numeric_delete(j_leaves_quantity);
  Numeric_delete(j_outstanding_hold_amount);

  if (errno)
    werr("%s: user: %s\n", coinbase_ws_uri, strerror(errno));

  errno = saved_errno;
}

static void ws_handle_message(const struct mg_ws_message *restrict const msg) {
  const struct coinbase_tls *restrict const tls = coinbase_tls();
  struct wcjson_document *restrict ws_doc = tls->ws_handle_message.ws_doc;
  const int saved_errno = errno;

  ws_doc->v_next = 0;
  ws_doc->s_next = 0;
  ws_doc->mb_next = 0;

  if (json_mbparse(ws_doc, msg->data.buf, msg->data.len) < 0)
    goto ret;

  errno = 0;

  struct String *restrict const j_type =
      json_obj_get_optional_string(ws_doc, ws_doc->values, L"type", 4);

  if (errno)
    goto ret;

  if (j_type != NULL) {
    werr("%s: %.*s\n", coinbase_ws_uri, (int)msg->data.len, msg->data.buf);
    String_delete(j_type);
    goto ret;
  }

  struct String *restrict const j_channel =
      json_obj_get_string(ws_doc, ws_doc->values, L"channel", 7);

  if (j_channel == NULL)
    goto ret;

  struct ws_channel *restrict const channel =
      ws_channel(String_chars(j_channel));

  if (channel == NULL) {
    werr("%s: %s: %.*s\n", coinbase_ws_uri, String_chars(j_channel),
         (int)msg->data.len, msg->data.buf);

    String_delete(j_channel);
    goto ret;
  }

  String_delete(j_channel);

#ifdef ABAG_COINBASE_DEBUG
  if (channel->debug)
    wout("%s: %s: %.*s\n", coinbase_ws_uri, channel->name, (int)msg->data.len,
         msg->data.buf);

  errno = 0;
#endif

  if (channel->items == NULL)
    goto ret;

  struct Numeric *restrict const j_timestamp =
      json_obj_get_string_iso8601(ws_doc, ws_doc->values, L"timestamp", 9);

  if (j_timestamp == NULL)
    goto ret;

  const struct wcjson_value *restrict const j_events =
      wcjson_object_get(ws_doc, ws_doc->values, L"events", 6);

  if (j_events == NULL || !j_events->is_array) {
    werr("%s: %s: No 'events' array item: %.*s\n", coinbase_ws_uri,
         channel->name, (int)msg->data.len, msg->data.buf);
    Numeric_delete(j_timestamp);
    goto ret;
  }

  const struct wcjson_value *restrict j_evt = NULL;
  wcjson_value_foreach(j_evt, ws_doc, j_events) {
    const struct wcjson_value *restrict const j_evt_items =
        wcjson_object_get(ws_doc, j_evt, channel->items, channel->items_len);

    if (j_evt_items == NULL || !j_evt_items->is_array) {
      werr("%s: %s: No '%ls' array item: %.*s\n", coinbase_ws_uri,
           channel->name, channel->items, (int)msg->data.len, msg->data.buf);

      Numeric_delete(j_timestamp);
      goto ret;
    }

    struct String *restrict const j_evt_type =
        json_obj_get_string(ws_doc, j_evt, L"type", 4);

    if (j_evt_type == NULL) {
      Numeric_delete(j_timestamp);
      goto ret;
    }

    channel->last_message = mg_millis();

    if (String_length(j_evt_type) == 6 &&
        !strcmp("update", String_chars(j_evt_type))) {

      if (channel->update) {
        const struct wcjson_value *restrict j_evt_item = NULL;
        wcjson_value_foreach(j_evt_item, ws_doc, j_evt_items) {
          channel->update(ws_doc, j_evt_item, j_timestamp);
        }
      }
    } else if (String_length(j_evt_type) == 8 &&
               !strcmp("snapshot", String_chars(j_evt_type))) {

      if (channel->snapshot) {
        const struct wcjson_value *restrict j_evt_item = NULL;
        wcjson_value_foreach(j_evt_item, ws_doc, j_evt_items) {
          channel->snapshot(ws_doc, j_evt_item, j_timestamp);
        }
      }
    } else
      werr("%s: %s: %s: %.*s\n", coinbase_ws_uri, channel->name,
           String_chars(j_evt_type), (int)msg->data.len, msg->data.buf);

    String_delete(j_evt_type);
  }

  Numeric_delete(j_timestamp);
  errno = 0;
ret:
  if (errno)
    werr("%s: %s: %.*s\n", coinbase_ws_uri, strerror(errno), (int)msg->data.len,
         msg->data.buf);

  errno = saved_errno;
}

static void ws_subscribe(struct mg_connection *restrict const c,
                         const struct ws_channel *restrict const channel) {
  void *const *restrict items;
  char jwt[JSON_BODY_MAX + 1] = {0};
  size_t jwt_len = nitems(jwt);
  const int saved_errno = errno;
  struct wcjson wc_json = WCJSON_INITIALIZER;
  struct wcjson_document hb_doc = WCJSON_DOCUMENT_INITIALIZER;
  struct wcjson_document ch_doc = WCJSON_DOCUMENT_INITIALIZER;
  struct Array *restrict const m_array = coinbase_markets();

  hb_doc.v_nitems = 16;
  ch_doc.v_nitems = 16;

  if (ch_doc.v_nitems > SIZE_MAX - Array_size(m_array))
    panic();

  hb_doc.values = heap_malloc(hb_doc.v_nitems * sizeof(struct wcjson_value));
  ch_doc.v_nitems += Array_size(m_array);
  ch_doc.values = heap_malloc(ch_doc.v_nitems * sizeof(struct wcjson_value));

  errno = 0;

  struct wcjson_value *restrict const j_hb_msg = wcjson_value_object(&hb_doc);
  struct wcjson_value *restrict const j_ch_msg = wcjson_value_object(&ch_doc);
  struct wcjson_value *restrict const j_ch_arr = wcjson_value_array(&ch_doc);

  if (errno) {
    Array_unlock(m_array);
    goto ret;
  }

  items = Array_items(m_array);
  for (size_t i = Array_size(m_array); i-- > 0;) {
    const struct Market *restrict const m = items[i];
    wcjson_array_add_tail(&ch_doc, j_ch_arr,
                          wcjson_value_mbstring(&ch_doc, String_chars(m->sym),
                                                String_length(m->sym)));
  }
  Array_unlock(m_array);

  if (errno)
    goto ret;

  wcjson_object_add_tail(&hb_doc, j_hb_msg, L"type", 4,
                         wcjson_value_string(&hb_doc, L"subscribe", 9));

  wcjson_object_add_tail(&ch_doc, j_ch_msg, L"type", 4,
                         wcjson_value_string(&ch_doc, L"subscribe", 9));

  wcjson_object_add_tail(&ch_doc, j_ch_msg, L"product_ids", 11, j_ch_arr);

  if (errno || jwt_encode_cdp(jwt, &jwt_len, NULL) < 0)
    goto ret;

  errno = 0;

  wcjson_object_add_tail(&hb_doc, j_hb_msg, L"jwt", 3,
                         wcjson_value_mbstring(&hb_doc, jwt, strlen(jwt)));

  wcjson_object_add_tail(&hb_doc, j_hb_msg, L"channel", 7,
                         wcjson_value_string(&hb_doc, L"heartbeats", 10));

  wcjson_object_add_tail(&ch_doc, j_ch_msg, L"jwt", 3,
                         wcjson_value_mbstring(&ch_doc, jwt, strlen(jwt)));

  wcjson_object_add_tail(
      &ch_doc, j_ch_msg, L"channel", 7,
      wcjson_value_mbstring(&ch_doc, channel->name, strlen(channel->name)));

  if (wcjson_document_build(&wc_json, &ch_doc) < 0 ||
      wcjson_document_build(&wc_json, &hb_doc) < 0) {
    werr("%s: %s: subscribe: %s\n", coinbase_ws_uri, channel->name,
         json_mbserror(&wc_json));
    goto ret;
  }

  char hb_body[JSON_BODY_MAX + 1] = {0};
  char ch_body[JSON_BODY_MAX + 1] = {0};
  size_t hb_len = sizeof(hb_body);
  size_t ch_len = sizeof(ch_body);

  json_mbsprint(hb_body, &hb_len, &hb_doc, hb_doc.values);
  json_mbsprint(ch_body, &ch_len, &ch_doc, ch_doc.values);

  if (errno)
    goto ret;

  if (!mg_ws_send(c, hb_body, hb_len, WEBSOCKET_OP_TEXT) ||
      !mg_ws_send(c, ch_body, ch_len, WEBSOCKET_OP_TEXT))
    goto ret;

  errno = 0;
ret:
  heap_free(hb_doc.values);
  heap_free(hb_doc.strings);
  heap_free(hb_doc.mbstrings);
  heap_free(hb_doc.esc);
  heap_free(ch_doc.values);
  heap_free(ch_doc.strings);
  heap_free(ch_doc.mbstrings);
  heap_free(ch_doc.esc);

  if (errno)
    werr("%s: %s: subscribe: %s\n", coinbase_ws_uri, channel->name,
         strerror(errno));

  errno = saved_errno;
}

static void ws_evt_handler(struct mg_connection *c, int ev, void *ev_data) {
  struct ws_channel *restrict const channel = c->fn_data;

  switch (ev) {
  case MG_EV_CONNECT: {
#ifdef ABAG_COINBASE_DEBUG
    wout("%s: %s: %lu: MG_EV_CONNECT\n", coinbase_ws_uri, channel->name, c->id);
#endif
    struct mg_tls_opts ws_tls_opts = {0};
    ws_tls_opts.name = mg_url_host(coinbase_ws_uri);

    mg_tls_init(c, &ws_tls_opts);
    break;
  }
  case MG_EV_ERROR: {
    werr("%s: %s: %lu: %s\n", coinbase_ws_uri, channel->name, c->id,
         (char *)ev_data);
    c->is_closing = 1;
    break;
  }
  case MG_EV_WS_OPEN: {
#ifdef ABAG_COINBASE_DEBUG
    wout("%s: %s: %lu: MG_EV_WS_OPEN\n", coinbase_ws_uri, channel->name, c->id);
#endif
    if (running) {
      markets_reload = true;
      accounts_reload = true;
      ws_subscribe(c, channel);
    } else
      c->is_closing = 1;

    break;
  }
  case MG_EV_WS_MSG: {
    const struct mg_ws_message *restrict const wm = ev_data;
    const uint8_t type = wm->flags & 0x0F;

    if (running) {
      if (type == WEBSOCKET_OP_TEXT) {
        ws_handle_message(wm);
      } else if (type == WEBSOCKET_OP_CLOSE) {
#ifdef ABAG_COINBASE_DEBUG
        wout("%s: %s: %lu: WEBSOCKET_OP_CLOSE\n", coinbase_ws_uri,
             channel->name, c->id);
#endif
        c->is_closing = 1;
      } else
        werr("%s: %s: %lu: %d\n", coinbase_ws_uri, channel->name, c->id, type);

    } else
      c->is_closing = 1;

    break;
  }
  case MG_EV_CLOSE: {
#ifdef ABAG_COINBASE_DEBUG
    wout("%s: %s: %lu: MG_EV_CLOSE\n", coinbase_ws_uri, channel->name, c->id);
#endif
    if (running) {
      struct mg_mgr *restrict const mgr = c->mgr;

      do {
        thread_sleep(&coinbase_retry_rate);
        c = mg_ws_connect(mgr, coinbase_ws_uri, ws_evt_handler, channel, NULL);
        if (!c)
          werr("%s: %s: Failure reconnecting\n", coinbase_ws_uri,
               channel->name);

      } while (!c);
    }
    break;
  }
  }

  if (channel->last_message &&
      mg_millis() - channel->last_message > coinbase_stall_ms) {
    channel->reconnect = true;
    channel->last_message = mg_millis();
    if (verbose)
      wout("%s: %s: No events\n", coinbase_ws_uri, channel->name);
  }

  if (channel->reconnect) {
    channel->reconnect = false;
    c->is_closing = 1;
  }
}

static int coinbase_rest_query(struct wcjson_document *restrict rsp_doc,
                               const char *restrict const url,
                               const char *restrict const path,
                               const char *restrict const body,
                               const size_t body_len) {
  struct mg_str host = mg_url_host(url);
  char uri[URL_MAX_LENGTH + 1] = {0};
  char auth[HTTP_HEADER_MAX + 1] = {0};
  char jwt[JSON_BODY_MAX + 1] = {0};
  size_t jwt_len = nitems(jwt);
  int r = snprintf(uri, sizeof(uri), "%s %.*s%s", body_len > 0 ? "POST" : "GET",
                   (int)host.len, host.buf, path);

  if (r < 0 || (size_t)r >= sizeof(uri))
    panic();

  if (jwt_encode_cdp(jwt, &jwt_len, uri))
    return -1;

  r = snprintf(auth, sizeof(auth), "Bearer %s", jwt);

  if (r < 0 || (size_t)r >= sizeof(auth))
    panic();

  struct Map *restrict const headers = Map_new(StringMapOps, 4);
  struct String *restrict const k_auth = String_cnew("Authorization");
  struct String *restrict const v_auth = String_cnew(auth);
  Map_put(headers, k_auth, v_auth);

  thread_sleep(&coinbase_request_rate);

  rsp_doc->v_next = 0;
  rsp_doc->s_next = 0;
  rsp_doc->mb_next = 0;

  r = http_request_json(rsp_doc, url, headers, body, body_len);

  Map_delete(headers, NULL);
  String_delete(k_auth);
  String_delete(v_auth);

  return r;
}

#define allowed_in_url(c)                                                      \
  ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||                         \
   (c >= '0' && c <= '9') || c == ':' || c == '/' || c == '?' || c == '#' ||   \
   c == '[' || c == ']' || c == '@' || c == '!' || c == '$' || c == '&' ||     \
   c == '\'' || c == '(' || c == ')' || c == '*' || c == '+' || c == ',' ||    \
   c == ';' || c == '=' || c == '-' || c == '.' || c == '_' || c == '~')

inline static void envurl(char *restrict d, size_t len, const char *restrict nm,
                          const char *restrict dflt) {
  const char *restrict env = envs(nm, dflt);

  while (len-- != 0 && *env) {
    if (!allowed_in_url(*env))
      fatal("%s: %s", nm, env);

    *d++ = *env++;
  }

  if (len == SIZE_MAX && *env)
    fatal("%s: %s", nm, env);

  *d = '\0';
}

static void coinbase_init(void) {
  exchange_coinbase.id = String_cnew(COINBASE_UUID);
  exchange_coinbase.nm = String_cnew("coinbase");

  envurl(coinbase_rest_uri, sizeof(coinbase_rest_uri) - 1, "CDP_REST_URI",
         DEFAULT_CDP_REST_URI);

  envurl(coinbase_ws_uri, sizeof(coinbase_ws_uri) - 1, "CDP_WS_URI",
         DEFAULT_CDP_WS_URI);

  envurl(coinbase_account_path, sizeof(coinbase_account_path) - 1,
         "CDP_ACCOUNT_PATH", DEFAULT_CDP_ACCOUNT_PATH);

  envurl(coinbase_accounts_path, sizeof(coinbase_accounts_path) - 1,
         "CDP_ACCOUNTS_PATH", DEFAULT_CDP_ACCOUNTS_PATH);

  envurl(coinbase_fees_path, sizeof(coinbase_fees_path) - 1, "CDP_FEES_PATH",
         DEFAULT_CDP_FEES_PATH);

  envurl(coinbase_order_path, sizeof(coinbase_order_path) - 1, "CDP_ORDER_PATH",
         DEFAULT_CDP_ORDER_PATH);

  envurl(coinbase_products_path, sizeof(coinbase_products_path) - 1,
         "CDP_PRODUCTS_PATH", DEFAULT_CDP_PRODUCTS_PATH);

  envurl(coinbase_order_cancel_path, sizeof(coinbase_order_cancel_path) - 1,
         "CDP_ORDER_CANCEL_PATH", DEFAULT_CDP_ORDER_CANCEL_PATH);

  envurl(coinbase_order_create_path, sizeof(coinbase_order_create_path) - 1,
         "CDP_ORDER_CREATE_PATH", DEFAULT_CDP_ORDER_CREATE_PATH);

  const unsigned long req_s = envul("CDP_HTTP_REQUESTS_PER_SECOND",
                                    DEFAULT_CDP_HTTP_REQUESTS_PER_SECOND);

  if (req_s == 0)
    fatal("%s == 0", "CDP_HTTP_REQUESTS_PER_SECOND");

  coinbase_request_rate.tv_sec = 0;
  coinbase_request_rate.tv_nsec = 1000000000L / req_s;

  const unsigned long ret_s =
      envul("CDP_HTTP_RETRY_SECONDS", DEFAULT_CDP_HTTP_RETRY_SECONDS);

  coinbase_retry_rate.tv_sec = ret_s;
  coinbase_retry_rate.tv_nsec = 0;

  coinbase_stall_ms =
      envul("CDP_HTTP_STALL_MILLIS", DEFAULT_CDP_HTTP_STALL_MILLIS);

  if (verbose) {
    wout("\tCDP_REST_URI=%s\n", coinbase_rest_uri);
    wout("\tCDP_WS_URI=%s\n", coinbase_ws_uri);
    wout("\tCDP_ACCOUNT_PATH=%s\n", coinbase_account_path);
    wout("\tCDP_ACCOUNTS_PATH=%s\n", coinbase_accounts_path);
    wout("\tCDP_FEES_PATH=%s\n", coinbase_fees_path);
    wout("\tCDP_ORDER_PATH=%s\n", coinbase_order_path);
    wout("\tCDP_PRODUCTS_PATH=%s\n", coinbase_products_path);
    wout("\tCDP_ORDER_CANCEL_PATH=%s\n", coinbase_order_cancel_path);
    wout("\tCDP_ORDER_CREATE_PATH=%s\n", coinbase_order_create_path);
    wout("\tCDP_HTTP_REQUESTS_PER_SECOND=%lu\n", req_s);
    wout("\tCDP_HTTP_RETRY_SECONDS=%lu\n", ret_s);
    wout("\tCDP_HTTP_STALL_MILLIS=%lu\n", coinbase_stall_ms);
  }

  running = false;
  coinbase_cnf = NULL;
  coinbase_db = NULL;
  orders = Queue_new(128, (time_t)0);
  samples = Queue_new((MG_MAX_RECV_SIZE) / sizeof(struct Sample *),
                      (time_t)(coinbase_stall_ms / 1000L));
  markets = Array_new(1024);
  markets_by_symbol = Map_new(StringMapOps, 1024);
  markets_by_id = Map_new(StringMapOps, 1024);

  markets_reload = true;
  accounts = Array_new(256);
  accounts_by_id = Map_new(StringMapOps, 256);
  accounts_by_currency = Map_new(StringMapOps, 256);

  accounts_reload = true;
  pricing = NULL;
  mutex_init(&pricing_mutex);
  tls_create(&coinbase_tls_key, coinbase_tls_dtor);
}

static void coinbase_configure(const struct ExchangeConfig *restrict const c) {
  coinbase_cnf = c;
  coinbase_db = db_connect(COINBASE_DBCON);
}

static void coinbase_destroy(void) {
  if (coinbase_db)
    db_disconnect(coinbase_db);

  coinbase_cnf = NULL;
  String_delete(exchange_coinbase.id);
  String_delete(exchange_coinbase.nm);
  Queue_delete(orders, Order_delete);
  Queue_delete(samples, Sample_delete);
  Array_delete(markets, Market_delete);
  Map_delete(markets_by_symbol, NULL);
  Map_delete(markets_by_id, NULL);
  Array_delete(accounts, Account_delete);
  Map_delete(accounts_by_id, NULL);
  Map_delete(accounts_by_currency, NULL);
  Pricing_delete(pricing);
  mutex_destroy(&pricing_mutex);
  tls_delete(coinbase_tls_key);
}

static void coinbase_start(void) {
  struct mg_mgr *restrict const mgr = heap_calloc(1, sizeof(struct mg_mgr));
  mg_mgr_init(mgr);
  mg_mgr_config(mgr);

  Queue_start(orders);
  Queue_start(samples);

  running = true;
  for (size_t i = nitems(ws_channels); i-- > 0;) {
    if (ws_channels[i].items != NULL) {
      struct mg_connection *restrict const c = mg_ws_connect(
          mgr, coinbase_ws_uri, ws_evt_handler, &ws_channels[i], NULL);

      ws_channels[i].last_message = mg_millis();

      if (!c)
        fatal("%s: %s: Failure starting websocket\n", coinbase_ws_uri,
              ws_channels[i].name);
    }
  }

  thread_create(&mg_mgr_worker, mg_mgr_worker_func, mgr);
}

static void coinbase_stop(void) {
  running = false;
  Queue_stop(orders);
  Queue_stop(samples);
  thread_join(mg_mgr_worker, NULL);
}

static struct Sample *coinbase_sample_await(void) {
  struct Sample *restrict const s = Queue_dequeue_await(samples);

  if (Queue_dequeue_timedout(samples)) {
    werr("%s: Dequeuing ticker timed out after %" PRIdMAX " seconds\n",
         coinbase_ws_uri, (intmax_t)(coinbase_stall_ms / 1000L));

    for (size_t i = nitems(ws_channels); i-- > 0;)
      ws_channels[i].reconnect = true;
  }

  return s;
}

static struct Order *coinbase_order_await(void) {
  return Queue_dequeue_await(orders);
}

static struct Market *
parse_product(const struct wcjson_document *restrict const doc,
              const struct wcjson_value *restrict const prod) {
  const int saved_errno = errno;
  char m_id[DATABASE_UUID_MAX_LENGTH + 1] = {0};
  struct Market *restrict m = NULL;

  errno = 0;

  struct String *restrict const j_product_id =
      json_obj_get_string(doc, prod, L"product_id", 10);

  struct String *restrict const j_base_currency_id =
      json_obj_get_string(doc, prod, L"base_currency_id", 16);

  struct String *restrict const j_quote_currency_id =
      json_obj_get_string(doc, prod, L"quote_currency_id", 17);

  struct Numeric *restrict const j_price_increment =
      json_obj_get_string_number(doc, prod, L"price_increment", 15);

  struct Numeric *restrict const j_base_increment =
      json_obj_get_string_number(doc, prod, L"base_increment", 14);

  struct Numeric *restrict const j_quote_increment =
      json_obj_get_string_number(doc, prod, L"quote_increment", 15);

  struct String *restrict const j_price_increment_s =
      json_obj_get_string(doc, prod, L"price_increment", 15);

  struct String *restrict const j_base_increment_s =
      json_obj_get_string(doc, prod, L"base_increment", 14);

  struct String *restrict const j_quote_increment_s =
      json_obj_get_string(doc, prod, L"quote_increment", 15);

  const bool j_is_disabled = json_obj_get_bool(doc, prod, L"is_disabled", 11);
  //  const bool j_is_limit_only = json_obj_get_bool(doc, prod, L"limit_only",
  //  10);
  const bool j_is_post_only = json_obj_get_bool(doc, prod, L"post_only", 9);
  const bool j_is_view_only = json_obj_get_bool(doc, prod, L"view_only", 9);
  const bool j_is_new = json_obj_get_bool(doc, prod, L"new", 3);
  const bool j_is_cancel_only =
      json_obj_get_bool(doc, prod, L"cancel_only", 11);

  const bool j_is_trading_disabled =
      json_obj_get_bool(doc, prod, L"trading_disabled", 16);

  const bool j_is_auction_mode =
      json_obj_get_bool(doc, prod, L"auction_mode", 12);

  struct String *restrict const j_status =
      json_obj_get_string(doc, prod, L"status", 6);

  struct String *restrict const j_product_type =
      json_obj_get_string(doc, prod, L"product_type", 12);

  if (errno)
    goto ret;

  db_symbol_to_id(m_id, coinbase_db, COINBASE_UUID, String_chars(j_product_id));

  // Extract scale from price increment.
  const char *restrict const p_dot =
      strchr(String_chars(j_price_increment_s), '.');

  const char *restrict const b_dot =
      strchr(String_chars(j_base_increment_s), '.');

  const char *restrict const q_dot =
      strchr(String_chars(j_quote_increment_s), '.');

  const uintmax_t p_sc = p_dot ? strlen(p_dot + 1) : 0;
  const uintmax_t b_sc = b_dot ? strlen(b_dot + 1) : 0;
  const uintmax_t q_sc = q_dot ? strlen(q_dot + 1) : 0;

  String_delete(j_price_increment_s);
  String_delete(j_base_increment_s);
  String_delete(j_quote_increment_s);

  // Find matching accounts required for trading.
  const struct Account *restrict qa =
      coinbase_account_currency(j_quote_currency_id);

  const struct Account *restrict ba =
      coinbase_account_currency(j_base_currency_id);

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

  const enum market_status status_value = market_status(String_chars(j_status));
  const enum market_type type_value = market_type(String_chars(j_product_type));

  char nm[4096] = {0};
  int r = snprintf(nm, sizeof(nm), "%s@%s", String_chars(j_base_currency_id),
                   String_chars(j_quote_currency_id));

  if (r < 0 || (size_t)r >= sizeof(nm))
    panic();

  m = Market_new();
  m->id = String_cnew(m_id);
  m->b_id = j_base_currency_id;
  m->ba_id = ba_id;
  m->q_id = j_quote_currency_id;
  m->qa_id = qa_id;
  m->nm = String_cnew(nm);
  m->sym = j_product_id;
  m->type = type_value;
  m->status = status_value;
  m->p_sc = p_sc;
  m->p_inc = j_price_increment;
  m->b_sc = b_sc;
  m->b_inc = j_base_increment;
  m->q_sc = q_sc;
  m->q_inc = j_quote_increment;
  m->is_tradeable =
      qa_id != NULL && ba_id != NULL && type_value == MARKET_TYPE_SPOT;

  m->is_active = !(j_is_disabled || j_is_cancel_only || j_is_post_only ||
                   j_is_trading_disabled || j_is_new || j_is_auction_mode ||
                   j_is_view_only) &&
                 status_value == MARKET_STATUS_ONLINE;

  if (m->type == MARKET_TYPE_UNKNOWN)
    werr("%s: product: %s: %s: Unsupported market type: %s\n",
         coinbase_rest_uri, nm, m_id, String_chars(j_product_type));

  if (m->status == MARKET_STATUS_UNKNOWN)
    werr("%s: product: %s: %s: Unsupported market status: %s\n",
         coinbase_rest_uri, nm, m_id, String_chars(j_status));

  /*
   * Coinbase account active and ready flags change very infrequently - if at
   * all - once set or reset. Accounts will get reloaded whenever a status
   * websocket message is received. This happens more frequently so that it is
   * safe to not set a product's tradeable flag for inactive or unready
   * accounts.
   */
  m->is_tradeable = m->is_tradeable && qa_active_and_ready;
  m->is_tradeable = m->is_tradeable && ba_active_and_ready;

#ifdef ABAG_COINBASE_DEBUG
  if (!m->is_active)
    wout("%s: product: %s: %s: Market not active\n", coinbase_rest_uri, nm,
         m_id);
#endif

  errno = 0;
ret:
  if (m == NULL) {
    String_delete(j_product_id);
    String_delete(j_base_currency_id);
    String_delete(j_quote_currency_id);
    Numeric_delete(j_price_increment);
    Numeric_delete(j_base_increment);
    Numeric_delete(j_quote_increment);
    String_delete(j_status);
    String_delete(j_product_type);
    String_delete(j_price_increment_s);
    String_delete(j_base_increment_s);
    String_delete(j_quote_increment_s);
  }

  if (errno)
    werr("%s: product: %s\n", coinbase_rest_uri, strerror(errno));

  errno = saved_errno;
  return m;
}

static struct Array *
parse_products(struct Array *restrict const p,
               const struct wcjson_document *restrict const doc) {
  const int saved_errno = errno;
  char err[JSON_BODY_MAX + 1] = {0};
  size_t err_nitems = nitems(err);
  struct wcjson_value *restrict const j_products =
      wcjson_object_get(doc, doc->values, L"products", 8);

  if (j_products == NULL || !j_products->is_array) {
    if (json_mbsprint(err, &err_nitems, doc, doc->values)) {
      int r = snprintf(err, err_nitems, "%s", strerror(errno));
      if (r < 0 || (size_t)r >= err_nitems)
        panic();
    }
    werr("%s: products: No 'products' array item: %s\n", coinbase_rest_uri,
         err);
    goto ret;
  }

  const struct wcjson_value *restrict j_product = NULL;
  wcjson_value_foreach(j_product, doc, j_products) {
    struct Market *restrict const parsed = parse_product(doc, j_product);

    if (parsed == NULL)
      goto ret;

    Array_add_tail(p, parsed);
  }

  errno = 0;
ret:
  if (errno)
    werr("%s: products: %s\n", coinbase_rest_uri, strerror(errno));

  errno = saved_errno;
  return p;
}

static struct Array *coinbase_markets(void) {
  const struct coinbase_tls *restrict const tls = coinbase_tls();
  struct wcjson_document *restrict rsp_doc = tls->coinbase_markets.rsp_doc;
  char url[URL_MAX_LENGTH + 1] = {0};
  void *const *restrict items;

  Array_lock(markets);

  if (markets_reload) {
    accounts_reload = true;
    int r = snprintf(url, sizeof(url), "%s%s", coinbase_rest_uri,
                     coinbase_products_path);

    if (r < 0 || (size_t)r >= sizeof(url))
      panic();

    if (coinbase_rest_query(rsp_doc, url, coinbase_products_path, NULL, 0) <
        0) {

      for (size_t i = nitems(ws_channels); i-- > 0;)
        ws_channels[i].reconnect = true;

      goto ret;
    }

    Array_clear(markets, Market_delete);
    parse_products(markets, rsp_doc);
    Array_compact(markets);
    Map_delete(markets_by_symbol, NULL);
    Map_delete(markets_by_id, NULL);
    markets_by_symbol = Map_new(StringMapOps, Array_size(markets));
    markets_by_id = Map_new(StringMapOps, Array_size(markets));

    items = Array_items(markets);
    for (size_t i = Array_size(markets); i-- > 0;) {
      if (Map_put(markets_by_symbol, ((struct Market *)items[i])->sym,
                  items[i]))
        fatal("%s: Market symbol uniqueness constraint: %s", url,
              String_chars(((struct Market *)items[i])->sym));

      if (Map_put(markets_by_id, ((struct Market *)items[i])->id, items[i]))
        fatal("%s: Market id uniqueness constraint: %s", url,
              String_chars(((struct Market *)items[i])->id));
    }

    markets_reload = false;
  }
ret:
  return markets;
}

static struct Market *coinbase_market(const struct String *restrict const id) {
  struct Array *restrict const m_array = coinbase_markets();
  struct Market *restrict m = Map_get(markets_by_id, id);

  if (m != NULL) {
    m->mtx = Array_mutex(m_array);
    return m;
  }

  Array_unlock(m_array);
  return NULL;
}

static struct Market *
coinbase_market_by_symbol(const struct String *restrict const name) {
  struct Array *restrict const m_array = coinbase_markets();
  struct Market *restrict const m = Map_get(markets_by_symbol, name);

  if (m != NULL) {
    m->mtx = Array_mutex(m_array);
    return m;
  }

  Array_unlock(m_array);
  return NULL;
}

static struct Account *
parse_account(const struct wcjson_document *restrict const doc,
              const struct wcjson_value *restrict const acct) {
  const int saved_errno = errno;
  char err[JSON_BODY_MAX + 1] = {0};
  size_t err_nitems = nitems(err);
  struct Account *restrict a = NULL;

  errno = 0;

  struct String *restrict const j_uuid =
      json_obj_get_string(doc, acct, L"uuid", 4);

  struct String *restrict const j_name =
      json_obj_get_string(doc, acct, L"name", 4);

  struct String *restrict const j_currency =
      json_obj_get_string(doc, acct, L"currency", 8);

  struct String *restrict const j_type =
      json_obj_get_string(doc, acct, L"type", 4);

  const struct wcjson_value *j_active =
      json_obj_get_optional_bool(doc, acct, L"active", 6);

  const struct wcjson_value *j_ready =
      json_obj_get_optional_bool(doc, acct, L"ready", 5);

  const struct wcjson_value *j_available_balance =
      wcjson_object_get(doc, acct, L"available_balance", 17);

  struct Numeric *restrict j_value = NULL;

  if (j_available_balance == NULL || !j_available_balance->is_object) {
    if (json_mbsprint(err, &err_nitems, doc, acct)) {
      int r = snprintf(err, err_nitems, "%s", strerror(errno));
      if (r < 0 || (size_t)r >= err_nitems)
        panic();
    }
    werr("%s: account: No 'available_balance' object item: %s\n",
         coinbase_rest_uri, err);
    goto ret;
  }

  j_value = json_obj_get_string_number(doc, j_available_balance, L"value", 5);

  if (errno)
    goto ret;

  a = Account_new();
  a->id = j_uuid;
  a->nm = j_name;
  a->c_id = j_currency;
  a->type = account_type(String_chars(j_type));
  a->avail = j_value;
  a->is_active = j_active && j_active->is_true;
  a->is_ready = j_ready && j_ready->is_true;

  if (a->type == ACCOUNT_TYPE_UNSPECIFIED)
    werr("%s: account: %s: %s: Account type unsupported\n", coinbase_rest_uri,
         String_chars(j_uuid), String_chars(j_type));

  errno = 0;
ret:
  if (a == NULL) {
    String_delete(j_uuid);
    String_delete(j_name);
    String_delete(j_currency);
    Numeric_delete(j_value);
  }

  String_delete(j_type);

  if (errno)
    werr("%s: account: %s\n", coinbase_rest_uri, strerror(errno));

  errno = saved_errno;
  return a;
}

static int parse_accounts(struct Array *restrict const a,
                          const struct wcjson_document *restrict const doc) {
  const int saved_errno = errno;
  char err[JSON_BODY_MAX + 1] = {0};
  size_t err_nitems = nitems(err);
  int r = -1;
  struct wcjson_value *restrict const j_accounts =
      wcjson_object_get(doc, doc->values, L"accounts", 8);

  if (j_accounts == NULL || !j_accounts->is_array) {
    if (json_mbsprint(err, &err_nitems, doc, doc->values)) {
      int s = snprintf(err, err_nitems, "%s", strerror(errno));
      if (s < 0 || (size_t)s >= err_nitems)
        panic();
    }
    werr("%s: accounts: No 'accounts' array item: %s\n", coinbase_rest_uri,
         err);
    goto ret;
  }

  const struct wcjson_value *restrict j_account = NULL;
  wcjson_value_foreach(j_account, doc, j_accounts) {
    struct Account *restrict const parsed = parse_account(doc, j_account);

    if (parsed == NULL)
      goto ret;

    if (parsed->type == ACCOUNT_TYPE_CRYPTO ||
        parsed->type == ACCOUNT_TYPE_FIAT)
      Array_add_tail(a, parsed);
    else
      Account_delete(parsed);
  }

  r = 0;
  errno = 0;
ret:
  if (errno)
    werr("%s: accounts: %s\n", coinbase_rest_uri, strerror(errno));

  errno = saved_errno;
  return r;
}

static int accounts_with_cursor(struct Array *restrict result,
                                const char *restrict const cursor) {
  const struct coinbase_tls *restrict const tls = coinbase_tls();
  struct wcjson_document *restrict rsp_doc = tls->accounts_with_cursor.rsp_doc;
  const int saved_errno = errno;
  char url[URL_MAX_LENGTH + 1] = {0};
  struct String *restrict j_cursor = NULL;
  int r;

  if (cursor)
    r = snprintf(url, sizeof(url), "%s%s?limit=%d&cursor=%s", coinbase_rest_uri,
                 coinbase_accounts_path, 128, cursor);

  else
    r = snprintf(url, sizeof(url), "%s%s?limit=%d", coinbase_rest_uri,
                 coinbase_accounts_path, 128);

  if (r < 0 || (size_t)r >= sizeof(url))
    panic();

  r = -1;
  errno = 0;

  if (coinbase_rest_query(rsp_doc, url, coinbase_accounts_path, NULL, 0) < 0)
    goto ret;

  r = parse_accounts(result, rsp_doc);

  if (r != 0)
    goto ret;

  r = -1;
  errno = 0;
  const struct wcjson_value *restrict const j_has_next =
      json_obj_get_optional_bool(rsp_doc, rsp_doc->values, L"has_next", 8);

  if (errno)
    goto ret;

  if (j_has_next && j_has_next->is_true) {
    j_cursor = json_obj_get_string(rsp_doc, rsp_doc->values, L"cursor", 6);

    if (errno)
      goto ret;

    r = accounts_with_cursor(result, String_chars(j_cursor));

    if (r != 0)
      goto ret;
  }

  r = 0;
  errno = 0;
ret:
  String_delete(j_cursor);

  if (errno)
    werr("%s: %s\n", url, strerror(errno));

  errno = saved_errno;
  return r;
}

static struct Array *coinbase_accounts(void) {
  void *const *restrict items;

  Array_lock(accounts);

  if (accounts_reload) {
    Array_clear(accounts, Account_delete);
    Map_delete(accounts_by_id, NULL);
    Map_delete(accounts_by_currency, NULL);

    if (accounts_with_cursor(accounts, NULL) == 0)
      accounts_reload = false;
    else
      for (size_t i = nitems(ws_channels); i > 0; i--)
        ws_channels[i - 1].reconnect = true;

    Array_compact(accounts);

    accounts_by_id = Map_new(StringMapOps, Array_size(accounts));
    accounts_by_currency = Map_new(StringMapOps, Array_size(accounts));

    items = Array_items(accounts);
    for (size_t i = Array_size(accounts); i-- > 0;) {
      if (Map_put(accounts_by_currency, ((struct Account *)items[i])->c_id,
                  items[i]) != NULL)
        fatal("%s: accounts: Account currency uniqueness constraint: %s",
              coinbase_rest_uri,
              String_chars(((struct Account *)items[i])->c_id));

      if (Map_put(accounts_by_id, ((struct Account *)items[i])->id, items[i]) !=
          NULL)
        fatal("%s: accounts: Account id uniqueness constraint: %s",
              coinbase_rest_uri,
              String_chars(((struct Account *)items[i])->id));
    }
  }

  return accounts;
}

static struct Account *
coinbase_account_currency(const struct String *restrict const currency) {
  struct Array *restrict const haystack = coinbase_accounts();
  struct Account *restrict needle = NULL;
  void *const *restrict items = Array_items(haystack);

  for (size_t i = Array_size(haystack); i-- > 0;)
    if (String_equals(((struct Account *)items[i])->c_id, currency)) {
      needle = items[i];
      needle->mtx = Array_mutex(accounts);
      return needle;
    }

  Array_unlock(haystack);
  return NULL;
}

static struct Account *
coinbase_account(const struct String *restrict const id) {
  const struct coinbase_tls *restrict const tls = coinbase_tls();
  struct wcjson_document *restrict rsp_doc = tls->coinbase_account.rsp_doc;
  char path[URL_MAX_LENGTH + 1] = {0};
  char url[URL_MAX_LENGTH + 1] = {0};
  char err[JSON_BODY_MAX + 1] = {0};
  size_t err_nitems = nitems(err);
  struct Account *restrict a = NULL;
  int r = snprintf(path, sizeof(path), "%s%s", coinbase_account_path,
                   String_chars(id));

  if (r < 0 || (size_t)r >= sizeof(path))
    panic();

  r = snprintf(url, sizeof(url), "%s%s", coinbase_rest_uri, path);
  if (r < 0 || (size_t)r >= sizeof(url))
    panic();

  if (coinbase_rest_query(rsp_doc, url, path, NULL, 0) < 0)
    goto ret;

  const struct wcjson_value *restrict const j_account =
      wcjson_object_get(rsp_doc, rsp_doc->values, L"account", 7);

  if (j_account == NULL || !j_account->is_object) {
    if (json_mbsprint(err, &err_nitems, rsp_doc, rsp_doc->values)) {
      r = snprintf(err, err_nitems, "%s", strerror(errno));
      if (r < 0 || (size_t)r >= err_nitems)
        panic();
    }
    werr("%s: No 'account' object item: %s\n", url, err);
    goto ret;
  }

  a = parse_account(rsp_doc, j_account);
ret:
  return a;
}

static struct Order *
parse_order(const struct wcjson_document *restrict const doc,
            const struct wcjson_value *restrict const order) {
  const int saved_errno = errno;
  char err[JSON_BODY_MAX + 1] = {0};
  size_t err_nitems = nitems(err);
  struct Order *restrict o = NULL;
  struct Market *restrict m = NULL;

  errno = 0;

  struct String *restrict const j_order_id =
      json_obj_get_string(doc, order, L"order_id", 8);

  struct String *restrict const j_product_id =
      json_obj_get_string(doc, order, L"product_id", 10);

  struct String *restrict const j_status =
      json_obj_get_string(doc, order, L"status", 6);

  struct String *restrict const j_reject_message =
      json_obj_get_optional_string(doc, order, L"reject_message", 14);

  struct String *restrict const j_cancel_message =
      json_obj_get_optional_string(doc, order, L"cancel_message", 14);

  struct Numeric *restrict const j_filled_size =
      json_obj_get_string_number(doc, order, L"filled_size", 11);

  struct Numeric *restrict const j_filled_value =
      json_obj_get_string_number(doc, order, L"filled_value", 12);

  struct Numeric *restrict const j_total_fees =
      json_obj_get_string_number(doc, order, L"total_fees", 10);

  struct Numeric *restrict const j_created_time =
      json_obj_get_string_iso8601(doc, order, L"created_time", 12);

  struct Numeric *restrict const j_last_fill_time =
      json_obj_get_optional_string_iso8601(doc, order, L"last_fill_time", 14);

  const bool j_settled = json_obj_get_bool(doc, order, L"settled", 7);

  const struct wcjson_value *restrict const j_size_inclusive_of_fees =
      json_obj_get_optional_bool(doc, order, L"size_inclusive_of_fees", 22);

  const struct wcjson_value *restrict const j_size_in_quote =
      json_obj_get_optional_bool(doc, order, L"size_in_quote", 13);

  struct Numeric *restrict j_base_size = NULL;
  struct Numeric *restrict j_limit_price = NULL;

  const struct wcjson_value *restrict const j_order_configuration =
      wcjson_object_get(doc, order, L"order_configuration", 19);

  if (j_order_configuration == NULL || !j_order_configuration->is_object) {
    if (json_mbsprint(err, &err_nitems, doc, order)) {
      int r = snprintf(err, err_nitems, "%s", strerror(errno));
      if (r < 0 || (size_t)r >= err_nitems)
        panic();
    }
    werr("%s: order: No 'order_configuration' object item: %s\n",
         coinbase_rest_uri, err);
    goto ret;
  }

  const struct wcjson_value *restrict const j_limit_limit_gtc =
      wcjson_object_get(doc, j_order_configuration, L"limit_limit_gtc", 15);

  j_base_size =
      json_obj_get_string_number(doc, j_limit_limit_gtc, L"base_size", 9);

  j_limit_price =
      json_obj_get_string_number(doc, j_limit_limit_gtc, L"limit_price", 11);

  if (errno)
    goto ret;

  // XXX: size_inclusive_of_fees boolean
  // XXX: size_in_quote boolean
  // XXX:   Not available at product level and via user channel events.

  if (j_size_inclusive_of_fees && j_size_inclusive_of_fees->is_true)
    werr("%s: order: %s: 'size_inclusive_of_fees' unsupported\n",
         coinbase_rest_uri, String_chars(j_order_id));

  if (j_size_in_quote && j_size_in_quote->is_true)
    werr("%s: order: %s: 'size_in_quote' unsupported\n", coinbase_rest_uri,
         String_chars(j_order_id));

  struct String *restrict msg = NULL;

  if (j_reject_message != NULL && String_length(j_reject_message) != 0) {
    msg = j_reject_message;
    String_delete(j_cancel_message);
  } else if (j_cancel_message != NULL && String_length(j_cancel_message) != 0) {
    msg = j_cancel_message;
    String_delete(j_reject_message);
  }

  m = coinbase_market_by_symbol(j_product_id);

  if (m == NULL) {
    werr("%s: order: %s: Market not available: %s\n", coinbase_rest_uri,
         String_chars(j_order_id), String_chars(j_product_id));
    goto ret;
  }

  o = Order_new();
  o->id = j_order_id;
  o->m_id = String_copy(m->id);
  o->settled = j_settled;
  o->status = order_status(String_chars(j_status));
  o->cnanos = j_created_time;
  o->dnanos = j_last_fill_time;
  o->b_ordered = j_base_size;
  o->p_ordered = j_limit_price;
  o->b_filled = j_filled_size;
  o->q_filled = j_filled_value;
  o->q_fees = j_total_fees;
  o->msg = msg;

  mutex_unlock(m->mtx);

  if (o->status == ORDER_STATUS_UNKNOWN)
    werr("%s: order: %s: Status unsupported: %s\n", coinbase_rest_uri,
         String_chars(j_order_id), String_chars(j_status));

  errno = 0;
ret:
  if (o == NULL) {
    String_delete(j_order_id);
    String_delete(j_reject_message);
    String_delete(j_cancel_message);
    Numeric_delete(j_filled_size);
    Numeric_delete(j_filled_value);
    Numeric_delete(j_total_fees);
    Numeric_delete(j_created_time);
    Numeric_delete(j_last_fill_time);
    Numeric_delete(j_base_size);
    Numeric_delete(j_limit_price);
  }

  String_delete(j_product_id);
  String_delete(j_status);

  if (errno)
    werr("%s: order: %s\n", coinbase_rest_uri, strerror(errno));

  errno = saved_errno;
  return o;
}

static struct Order *coinbase_order(const struct String *restrict const id) {
  const struct coinbase_tls *restrict const tls = coinbase_tls();
  struct wcjson_document *restrict rsp_doc = tls->coinbase_order.rsp_doc;
  char path[URL_MAX_LENGTH + 1] = {0};
  char url[URL_MAX_LENGTH + 1] = {0};
  char err[JSON_BODY_MAX + 1] = {0};
  size_t err_nitems = nitems(err);
  struct Order *restrict o = NULL;
  int r = snprintf(path, sizeof(path), "%s%s", coinbase_order_path,
                   String_chars(id));

  if (r < 0 || (size_t)r >= sizeof(path))
    panic();

  r = snprintf(url, sizeof(url), "%s%s", coinbase_rest_uri, path);
  if (r < 0 || (size_t)r >= sizeof(url))
    panic();

  if (coinbase_rest_query(rsp_doc, url, path, NULL, 0) < 0)
    goto ret;

  const struct wcjson_value *restrict const j_order =
      wcjson_object_get(rsp_doc, rsp_doc->values, L"order", 5);

  if (j_order == NULL || !j_order->is_object) {
    if (json_mbsprint(err, &err_nitems, rsp_doc, rsp_doc->values)) {
      r = snprintf(err, err_nitems, "%s", strerror(errno));
      if (r < 0 || (size_t)r >= err_nitems)
        panic();
    }
    werr("%s: %s: No 'order' object item: %s\n", url, String_chars(id), err);
    goto ret;
  }

  o = parse_order(rsp_doc, j_order);
ret:
  return o;
}

static bool coinbase_order_cancel(const struct String *restrict const id) {
  const struct coinbase_tls *restrict const tls = coinbase_tls();
  struct wcjson_document *restrict rsp_doc = tls->coinbase_order_cancel.rsp_doc;
  bool success = false;
  bool found = false;
  char url[URL_MAX_LENGTH + 1] = {0};
  char err[JSON_BODY_MAX + 1] = {0};
  size_t err_nitems = nitems(err);
  const int saved_errno = errno;
  struct wcjson wc_json = WCJSON_INITIALIZER;
  // Request Values
  //  j_req
  //  id
  //  ids pair
  //  1 + 1 + 2
  struct wcjson_document req_doc = {
      .values = (struct wcjson_value[4]){{0}},
      .v_nitems = 4,
  };

  int r = snprintf(url, sizeof(url), "%s%s", coinbase_rest_uri,
                   coinbase_order_cancel_path);

  if (r < 0 || (size_t)r >= sizeof(url))
    panic();

  errno = 0;

  struct wcjson_value *restrict const j_req = wcjson_value_object(&req_doc);
  struct wcjson_value *restrict const j_ids = wcjson_value_array(&req_doc);

  wcjson_array_add_tail(
      &req_doc, j_ids,
      wcjson_value_mbstring(&req_doc, String_chars(id), String_length(id)));

  wcjson_object_add_tail(&req_doc, j_req, L"order_ids", 9, j_ids);

  if (errno)
    goto ret;

  if (wcjson_document_build(&wc_json, &req_doc) < 0)
    goto ret;

  char mb[JSON_BODY_MAX + 1] = {0};
  size_t mb_len = nitems(mb);
  if (json_mbsprint(mb, &mb_len, &req_doc, req_doc.values) < 0)
    goto ret;

  errno = 0;

  if (coinbase_rest_query(rsp_doc, url, coinbase_order_cancel_path, mb,
                          mb_len) < 0)
    goto ret;

  const struct wcjson_value *restrict const j_results =
      wcjson_object_get(rsp_doc, rsp_doc->values, L"results", 7);

  if (j_results == NULL || !j_results->is_array) {
    if (json_mbsprint(err, &err_nitems, rsp_doc, rsp_doc->values)) {
      r = snprintf(err, err_nitems, "%s", strerror(errno));
      if (r < 0 || (size_t)r >= err_nitems)
        panic();
    }
    werr("%s: No 'results' array item: %s\n", url, err);
    goto ret;
  }

  const struct wcjson_value *restrict j_result = NULL;
  wcjson_value_foreach(j_result, rsp_doc, j_results) {
    errno = 0;
    const struct String *restrict const j_order_id =
        json_obj_get_string(rsp_doc, j_result, L"order_id", 8);

    const bool j_success = json_obj_get_bool(rsp_doc, j_result, L"success", 7);

    if (errno)
      goto ret;

    if (String_equals(id, j_order_id)) {
      found = true;
      success = j_success;
      break;
    }
  }

  if (!(found && success)) {
    err_nitems = nitems(err);
    if (json_mbsprint(err, &err_nitems, rsp_doc, rsp_doc->values)) {
      r = snprintf(err, err_nitems, "%s", strerror(errno));
      if (r < 0 || (size_t)r >= err_nitems)
        panic();
    }

    werr("%s: %s: %s\n", url, String_chars(id), err);
    goto ret;
  }

  errno = 0;
ret:
  if (errno)
    werr("%s: %s\n", url, strerror(errno));

  errno = saved_errno;
  return success;
}

static int
order_create_body(char *restrict const mb, size_t *restrict const mb_len,
                  const char *restrict const url,
                  const struct String *restrict const m_sym,
                  const struct String *restrict const side,
                  const char *restrict const base_amount, const size_t b_len,
                  const char *restrict const price, const size_t p_len) {
  const int saved_errno = errno;
  char cl_id[DATABASE_UUID_MAX_LENGTH + 1] = {0};
  struct wcjson wc_json = WCJSON_INITIALIZER;
  int r = -1;

  db_uuid(cl_id, coinbase_db);

  struct wcjson_document doc = {
      .values = (struct wcjson_value[24]){{0}},
      .v_nitems = 24,
  };

  errno = 0;

  struct wcjson_value *restrict const j_body = wcjson_value_object(&doc);
  struct wcjson_value *restrict const j_conf = wcjson_value_object(&doc);
  struct wcjson_value *restrict const j_llgtc = wcjson_value_object(&doc);
  struct wcjson_value *restrict const j_side =
      wcjson_value_mbstring(&doc, String_chars(side), String_length(side));

  struct wcjson_value *restrict const j_p_id =
      wcjson_value_mbstring(&doc, String_chars(m_sym), String_length(m_sym));

  struct wcjson_value *restrict const j_base =
      wcjson_value_mbstring(&doc, base_amount, b_len);

  struct wcjson_value *restrict const j_price =
      wcjson_value_mbstring(&doc, price, p_len);

  struct wcjson_value *restrict const j_cl_id =
      wcjson_value_mbstring(&doc, cl_id, strlen(cl_id));

  struct wcjson_value *restrict const j_post_only =
      wcjson_value_bool(&doc, false);

  wcjson_object_add_tail(&doc, j_body, L"order_configuration", 19, j_conf);
  wcjson_object_add_tail(&doc, j_conf, L"limit_limit_gtc", 15, j_llgtc);
  wcjson_object_add_tail(&doc, j_body, L"client_order_id", 15, j_cl_id);
  wcjson_object_add_tail(&doc, j_body, L"product_id", 10, j_p_id);
  wcjson_object_add_tail(&doc, j_body, L"side", 4, j_side);
  wcjson_object_add_tail(&doc, j_llgtc, L"post_only", 9, j_post_only);
  wcjson_object_add_tail(&doc, j_llgtc, L"base_size", 9, j_base);
  wcjson_object_add_tail(&doc, j_llgtc, L"limit_price", 11, j_price);

  if (errno)
    goto ret;

  if (wcjson_document_build(&wc_json, &doc) < 0)
    goto ret;

  if (json_mbsprint(mb, mb_len, &doc, doc.values) < 0)
    goto ret;

  r = 0;
  errno = 0;
ret:
  if (errno)
    werr("%s: %s\n", url, strerror(errno));

  errno = saved_errno;
  return r;
}

static struct String *coinbase_order_post(
    const char *restrict const m_sym, const char *restrict const side,
    const char *restrict const base_amount, const char *restrict const price) {
  const int saved_errno = errno;
  const struct coinbase_tls *restrict const tls = coinbase_tls();
  struct wcjson_document *restrict rsp_doc = tls->coinbase_order_post.rsp_doc;
  struct String *restrict j_order_id = NULL;
  struct String *restrict sym = String_cnew(m_sym);
  struct String *restrict sd = String_cnew(side);
  char url[URL_MAX_LENGTH + 1] = {0};
  char mb[JSON_BODY_MAX + 1] = {0};
  size_t mb_len = nitems(mb);
  char err[JSON_BODY_MAX + 1] = {0};
  size_t err_nitems = nitems(err);
  int r = snprintf(url, sizeof(url), "%s%s", coinbase_rest_uri,
                   coinbase_order_create_path);

  if (r < 0 || (size_t)r >= sizeof(url))
    panic();

  errno = 0;

  if (order_create_body(mb, &mb_len, url, sym, sd, base_amount,
                        strlen(base_amount), price, strlen(price)) < 0)
    goto ret;

  String_delete(sym);
  String_delete(sd);

  errno = 0;

  if (coinbase_rest_query(rsp_doc, url, coinbase_order_create_path, mb,
                          mb_len) < 0)
    goto ret;

  const bool j_success =
      json_obj_get_bool(rsp_doc, rsp_doc->values, L"success", 7);

  if (errno)
    goto ret;

  if (j_success) {
    const struct wcjson_value *restrict const j_success_response =
        wcjson_object_get(rsp_doc, rsp_doc->values, L"success_response", 16);

    if (j_success_response == NULL || !j_success_response->is_object) {
      if (json_mbsprint(err, &err_nitems, rsp_doc, rsp_doc->values)) {
        r = snprintf(err, err_nitems, "%s", strerror(errno));
        if (r < 0 || (size_t)r >= err_nitems)
          panic();
      }

      werr("%s: No 'success_response' object item: %s\n", url, err);
      goto ret;
    }

    j_order_id =
        json_obj_get_string(rsp_doc, j_success_response, L"order_id", 8);

    if (errno)
      goto ret;

    mutex_lock(&pricing_mutex);
    Pricing_delete(pricing);
    pricing = NULL;
    mutex_unlock(&pricing_mutex);
  } else {
    if (json_mbsprint(err, &err_nitems, rsp_doc, rsp_doc->values)) {
      r = snprintf(err, err_nitems, "%s", strerror(errno));
      if (r < 0 || (size_t)r >= err_nitems)
        panic();
    }
    werr("%s: %s\n", url, err);
  }

  errno = 0;
ret:
  if (errno)
    werr("%s: %s\n", url, strerror(errno));

  errno = saved_errno;
  return j_order_id;
}

static inline struct String *
coinbase_order_demand(const char *restrict const m_sym,
                      const char *restrict const base_amount,
                      const char *restrict const price) {
  return coinbase_order_post(m_sym, "BUY", base_amount, price);
}

static inline struct String *
coinbase_order_supply(const char *restrict const m_sym,
                      const char *restrict const base_amount,
                      const char *restrict const price) {
  return coinbase_order_post(m_sym, "SELL", base_amount, price);
}

static struct Pricing *
parse_pricing(const struct wcjson_document *restrict const doc,
              const struct wcjson_value *restrict const pr) {
  const int saved_errno = errno;
  char err[JSON_BODY_MAX + 1] = {0};
  size_t err_nitems = nitems(err);
  struct Pricing *restrict p = NULL;
  struct String *restrict j_pricing_tier = NULL;
  struct Numeric *restrict j_taker_fee_rate = NULL;
  struct Numeric *restrict j_maker_fee_rate = NULL;
  const struct wcjson_value *restrict const j_fee_tier =
      wcjson_object_get(doc, pr, L"fee_tier", 8);

  if (j_fee_tier == NULL || !j_fee_tier->is_object) {
    if (json_mbsprint(err, &err_nitems, doc, pr)) {
      int r = snprintf(err, err_nitems, "%s", strerror(errno));
      if (r < 0 || (size_t)r >= err_nitems)
        panic();
    }

    werr("%s: pricing: No 'fee_tier' object item: %s\n", coinbase_rest_uri,
         err);
    goto ret;
  }

  errno = 0;

  j_pricing_tier = json_obj_get_string(doc, j_fee_tier, L"pricing_tier", 12);
  j_taker_fee_rate =
      json_obj_get_string_number(doc, j_fee_tier, L"taker_fee_rate", 14);

  j_maker_fee_rate =
      json_obj_get_string_number(doc, j_fee_tier, L"maker_fee_rate", 14);

  if (errno)
    goto ret;

  struct Numeric *restrict const efr = Numeric_copy(
      Numeric_cmp(j_taker_fee_rate, j_maker_fee_rate) > 0 ? j_taker_fee_rate
                                                          : j_maker_fee_rate);

  p = Pricing_new();
  p->nm = j_pricing_tier;
  p->tf_pc = Numeric_mul(j_taker_fee_rate, hundred);
  p->mf_pc = Numeric_mul(j_maker_fee_rate, hundred);
  p->ef_pc = Numeric_mul(efr, hundred);

  Numeric_delete(j_taker_fee_rate);
  Numeric_delete(j_maker_fee_rate);
  Numeric_delete(efr);

  errno = 0;
ret:
  if (p == NULL) {
    String_delete(j_pricing_tier);
    Numeric_delete(j_taker_fee_rate);
    Numeric_delete(j_maker_fee_rate);
    p = Pricing_new();
    p->nm = String_cnew("fallback");
    p->tf_pc = Numeric_from_char("1.2");
    p->mf_pc = Numeric_from_char("1.2");
    p->ef_pc = Numeric_from_char("1.2");
  }

  if (errno)
    werr("%s: pricing: %s\n", coinbase_rest_uri, strerror(errno));

  errno = saved_errno;
  return p;
}

static struct Pricing *coinbase_pricing(void) {
  const struct coinbase_tls *restrict const tls = coinbase_tls();
  struct wcjson_document *restrict rsp_doc = tls->coinbase_pricing.rsp_doc;
  char url[URL_MAX_LENGTH + 1] = {0};

  mutex_lock(&pricing_mutex);

  if (pricing != NULL) {
    pricing->mtx = &pricing_mutex;
    return pricing;
  }

  int r = snprintf(url, sizeof(url), "%s%s?product_type=SPOT",
                   coinbase_rest_uri, coinbase_fees_path);

  if (r < 0 || (size_t)r >= sizeof(url))
    panic();

  if (coinbase_rest_query(rsp_doc, url, coinbase_fees_path, NULL, 0) < 0)
    goto ret;

  pricing = parse_pricing(rsp_doc, rsp_doc->values);
  pricing->mtx = &pricing_mutex;
ret:
  return pricing;
}
