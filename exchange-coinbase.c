/* $SchulteIT: exchange-coinbase.c 15282 2025-11-05 22:54:21Z schulte $ */
/* $JDTAUS: exchange-coinbase.c 9616 2026-07-02 17:18:43Z schulte $ */

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

#include "config.h"
#include "database.h"
#include "exchange.h"
#include "heap.h"
#include "map.h"
#include "math.h"
#include "mongoose.h"
#include "proc.h"
#include "queue.h"
#include "thread.h"
#include "time.h"
#include "version.h"
#include "wcjson-document.h"

#include <inttypes.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#ifndef DEFAULT_CDP_WS_URI
#define DEFAULT_CDP_WS_URI "wss://advanced-trade-ws.coinbase.com"
#endif

#ifndef DEFAULT_CDP_REST_URI
#define DEFAULT_CDP_REST_URI "https://api.coinbase.com"
#endif

#ifndef DEFAULT_CDP_ACCOUNT_PATH
#define DEFAULT_CDP_ACCOUNT_PATH "/api/v3/brokerage/accounts/%s"
#endif

#ifndef DEFAULT_CDP_ACCOUNTS_PATH
#define DEFAULT_CDP_ACCOUNTS_PATH "/api/v3/brokerage/accounts"
#endif

#ifndef DEFAULT_CDP_FEES_PATH
#define DEFAULT_CDP_FEES_PATH "/api/v3/brokerage/transaction_summary"
#endif

#ifndef DEFAULT_CDP_ORDER_PATH
#define DEFAULT_CDP_ORDER_PATH "/api/v3/brokerage/orders/historical/%s"
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
#define HTTP_RESPONSE_MAX_WCHARS (size_t)5242880

#define WCJSON_MAX_VALUES (size_t)65536         // 5MB
#define WCJSON_MAX_CHARACTERS (size_t)786432    // 3MB
#define WCJSON_MAX_MBCHARACTERS (size_t)1048576 // 1MB
#define WCJSON_MAX_ESCCHARACTERS (size_t)512    // 2kb
#define WCJSON_STRLEN_MAX (size_t)8192
#define WCJSON_BODY_MAX (size_t)32767

#define WCJSON_DECLARE_STRING_ITEM(_item)                                      \
  struct wcjson_value *restrict j_##_item = NULL;

#define WCJSON_STRING_ITEM(_doc, _val, _item, _len, _errbuf, _ret)             \
  j_##_item = wcjson_object_get(_doc, _val, L## #_item, _len);                 \
  if (j_##_item == NULL || !(j_##_item->is_string && j_##_item->s_len)) {      \
    werr("coinbase: No '" #_item "' string item: %s\n",                        \
         wcjsondoc_string(_errbuf, sizeof(_errbuf), _doc, _val, NULL));        \
    goto _ret;                                                                 \
  }

#define WCJSON_DECLARE_STRING_ITEM_OPT(_item)                                  \
  WCJSON_DECLARE_STRING_ITEM(_item)                                            \
  bool j_##_item##_exists = false;

#define WCJSON_STRING_ITEM_OPT(_doc, _val, _item, _len, _errbuf, _ret)         \
  j_##_item = wcjson_object_get(_doc, _val, L## #_item, _len);                 \
  j_##_item##_exists = j_##_item != NULL && j_##_item->is_string;              \
  if (j_##_item != NULL && !(j_##_item->is_string || j_##_item->is_null)) {    \
    werr("coinbase: No '" #_item "' string item: %s\n",                        \
         wcjsondoc_string(_errbuf, sizeof(_errbuf), _doc, _val, NULL));        \
    goto _ret;                                                                 \
  }

#define WCJSON_DECLARE_ARRAY_ITEM(_item)                                       \
  struct wcjson_value *restrict j_##_item = NULL;

#define WCJSON_ARRAY_ITEM(_doc, _val, _item, _len, _errbuf, _ret)              \
  j_##_item = wcjson_object_get(_doc, _val, L## #_item, _len);                 \
  if (j_##_item == NULL || !j_##_item->is_array) {                             \
    werr("coinbase: No '" #_item "' array item: %s\n",                         \
         wcjsondoc_string(_errbuf, sizeof(_errbuf), _doc, _val, NULL));        \
    goto _ret;                                                                 \
  }

#define WCJSON_DECLARE_OBJECT_ITEM(_item)                                      \
  struct wcjson_value *restrict j_##_item = NULL;

#define WCJSON_OBJECT_ITEM(_doc, _val, _item, _len, _errbuf, _ret)             \
  j_##_item = wcjson_object_get(_doc, _val, L## #_item, _len);                 \
  if (j_##_item == NULL || !j_##_item->is_object) {                            \
    werr("coinbase: No '" #_item "' object item: %s\n",                        \
         wcjsondoc_string(_errbuf, sizeof(_errbuf), _doc, _val, NULL));        \
    goto _ret;                                                                 \
  }

#define WCJSON_DECLARE_BOOL_ITEM(_item)                                        \
  struct wcjson_value *restrict j_##_item = NULL;

#define WCJSON_BOOL_ITEM(_doc, _val, _item, _len, _errbuf, _ret)               \
  j_##_item = wcjson_object_get(_doc, _val, L## #_item, _len);                 \
  if (j_##_item == NULL || !j_##_item->is_boolean) {                           \
    werr("coinbase: No '" #_item "' bool item: %s\n",                          \
         wcjsondoc_string(_errbuf, sizeof(_errbuf), _doc, _val, NULL));        \
    goto _ret;                                                                 \
  }

#define WCJSON_DECLARE_BOOL_ITEM_OPT(_item)                                    \
  WCJSON_DECLARE_BOOL_ITEM(_item)                                              \
  bool j_##_item##_exists = false;

#define WCJSON_BOOL_ITEM_OPT(_doc, _val, _item, _len, _errbuf, _ret)           \
  j_##_item = wcjson_object_get(_doc, _val, L## #_item, _len);                 \
  j_##_item##_exists = j_##_item != NULL && j_##_item->is_boolean;             \
  if (j_##_item != NULL && !j_##_item->is_boolean) {                           \
    werr("coinbase: No '" #_item "' bool item: %s\n",                          \
         wcjsondoc_string(_errbuf, sizeof(_errbuf), _doc, _val, NULL));        \
    goto _ret;                                                                 \
  }

#define WCJSON_DECLARE_NUMERIC_ITEM(_item)                                     \
  WCJSON_DECLARE_STRING_ITEM(_item)                                            \
  struct Numeric *restrict j_##_item##_num = NULL;

#define WCJSON_NUMERIC_ITEM(_doc, _val, _item, _len, _errbuf, _ret)            \
  WCJSON_STRING_ITEM(_doc, _val, _item, _len, _errbuf, _ret)                   \
  j_##_item##_num = Numeric_from_char(j_##_item->mbstring);                    \
  if (j_##_item##_num == NULL) {                                               \
    werr("coinbase: No '" #_item "' numeric item: %s\n",                       \
         wcjsondoc_string(_errbuf, sizeof(_errbuf), _doc, _val, NULL));        \
    goto _ret;                                                                 \
  }

#define WCJSON_DECLARE_NUMERIC_ITEM_OPT(_item)                                 \
  WCJSON_DECLARE_STRING_ITEM_OPT(_item)                                        \
  struct Numeric *restrict j_##_item##_num = NULL;

#define WCJSON_NUMERIC_ITEM_OPT(_doc, _val, _item, _len, _errbuf, _ret)        \
  WCJSON_STRING_ITEM_OPT(_doc, _val, _item, _len, _errbuf, _ret)               \
  if (j_##_item##_exists && j_##_item->s_len) {                                \
    j_##_item##_num = Numeric_from_char(j_##_item->mbstring);                  \
    if (j_##_item##_num == NULL) {                                             \
      werr("coinbase: No '" #_item "' numeric item: %s\n",                     \
           wcjsondoc_string(_errbuf, sizeof(_errbuf), _doc, _val, NULL));      \
      goto _ret;                                                               \
    }                                                                          \
  }

#define WCJSON_DECLARE_ISO8601_NANOS_ITEM(_item)                               \
  WCJSON_DECLARE_STRING_ITEM(_item)                                            \
  struct Numeric *restrict j_##_item##_nanos = Numeric_new();

#define WCJSON_ISO8601_NANOS_ITEM(_doc, _val, _item, _len, _errbuf, _ret)      \
  WCJSON_STRING_ITEM(_doc, _val, _item, _len, _errbuf, _ret)                   \
  if (!nanos_from_iso8601(j_##_item->mbstring, j_##_item->mb_len,              \
                          j_##_item##_nanos)) {                                \
    werr("coinbase: No '" #_item "' ISO8601 item: %s\n",                       \
         wcjsondoc_string(_errbuf, sizeof(_errbuf), _doc, _val, NULL));        \
    Numeric_delete(j_##_item##_nanos);                                         \
    j_##_item##_nanos = NULL;                                                  \
    goto _ret;                                                                 \
  }

#define WCJSON_DECLARE_ISO8601_NANOS_ITEM_OPT(_item)                           \
  WCJSON_DECLARE_STRING_ITEM_OPT(_item)                                        \
  struct Numeric *restrict j_##_item##_nanos = NULL;

#define WCJSON_ISO8601_NANOS_ITEM_OPT(_doc, _val, _item, _len, _errbuf, _ret)  \
  WCJSON_STRING_ITEM_OPT(_doc, _val, _item, _len, _errbuf, _ret)               \
  if (j_##_item##_exists) {                                                    \
    j_##_item##_nanos = Numeric_new();                                         \
    if (!nanos_from_iso8601(j_##_item->mbstring, j_##_item->mb_len,            \
                            j_##_item##_nanos)) {                              \
      werr("coinbase: No '" #_item "' ISO8601 item; %s\n",                     \
           wcjsondoc_string(_errbuf, sizeof(_errbuf), _doc, _val, NULL));      \
      Numeric_delete(j_##_item##_nanos);                                       \
      j_##_item##_nanos = NULL;                                                \
      j_##_item##_exists = false;                                              \
      goto _ret;                                                               \
    }                                                                          \
  }

#define WCJSON_DECLARE_MARKET_ITEM(_item)                                      \
  WCJSON_DECLARE_STRING_ITEM(_item)                                            \
  struct String *restrict j_##_item##_str = NULL;                              \
  struct Market *restrict j_##_item##_m = NULL;

#define WCJSON_MARKET_ITEM(_doc, _val, _item, _len, _errbuf, _ret)             \
  WCJSON_STRING_ITEM(_doc, _val, _item, _len, _errbuf, _ret)                   \
  j_##_item##_str = String_cnew(j_##_item->mbstring);                          \
  j_##_item##_m = coinbase_market_by_symbol(j_##_item##_str);                  \
  String_delete(j_##_item##_str);                                              \
  j_##_item##_str = NULL;                                                      \
  if (j_##_item##_m == NULL) {                                                 \
    werr("coinbase: Market '%s' not available: %s\n", j_##_item->mbstring,     \
         wcjsondoc_string(_errbuf, sizeof(_errbuf), _doc, _val, NULL));        \
    for (size_t i = nitems(ws_channels); i-- > 0;)                             \
      ws_channels[i].reconnect = true;                                         \
    goto _ret;                                                                 \
  }

#define WCJSON_DECLARE_MARKET_ITEM_OPT(_item)                                  \
  WCJSON_DECLARE_STRING_ITEM_OPT(_item)                                        \
  struct String *restrict j_##_item##_str = NULL;                              \
  struct Market *restrict j_##_item##_m = NULL;

#define WCJSON_MARKET_ITEM_OPT(_doc, _val, _item, _len, _errbuf, _ret)         \
  WCJSON_STRING_ITEM_OPT(_doc, _val, _item, _len, _errbuf, _ret)               \
  if (j_##_item##_exists && j_##_item->s_len) {                                \
    j_##_item##_str = String_cnew(j_##_item->mbstring);                        \
    j_##_item##_m = coinbase_market_by_symbol(j_##_item##_str);                \
    String_delete(j_##_item##_str);                                            \
    j_##_item##_str = NULL;                                                    \
    if (j_##_item##_m == NULL) {                                               \
      werr("coinbase: Market '%s' not available: %s\n", j_##_item->mbstring,   \
           wcjsondoc_string(_errbuf, sizeof(_errbuf), _doc, _val, NULL));      \
      for (size_t i = nitems(ws_channels); i-- > 0;)                           \
        ws_channels[i].reconnect = true;                                       \
      goto _ret;                                                               \
    }                                                                          \
  }

#ifndef nitems
#define nitems(_a) (sizeof((_a)) / sizeof((_a)[0]))
#endif

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

struct http_listener_ctx {
  const char *url;
  const char *path;
  const char *body;
  const size_t body_len;
  wchar_t *rsp;
  size_t rsp_len;
  uint64_t exp_time;
  bool success;
  bool done;
};

extern const struct Numeric *restrict const zero;
extern const struct Numeric *restrict const hundred;
extern const struct Numeric *restrict const milli_nanos;

extern const struct Config *restrict const cnf;
extern const bool verbose;

static const struct ExchangeConfig *restrict coinbase_cnf;
static const char *coinbase_ws_uri;
static const char *coinbase_rest_uri;
static struct timespec coinbase_request_rate;
static struct timespec coinbase_retry_rate;
static const char *coinbase_account_path;
static const char *coinbase_accounts_path;
static const char *coinbase_fees_path;
static const char *coinbase_order_path;
static const char *coinbase_order_cancel_path;
static const char *coinbase_order_create_path;
static const char *coinbase_products_path;
static unsigned long coinbase_stall_ms;
static unsigned long coinbase_timeout_ms;
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
static thrd_t mg_mgr_worker;
static struct wcjson_document *restrict ws_doc;

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

static int wcjsontoerrno(const struct wcjson *restrict const wcjson) {
  switch (wcjson->status) {
  case WCJSON_ABORT_INVALID:
    return EINVAL;
  case WCJSON_ABORT_END_OF_INPUT:
    return EIO;
  case WCJSON_ABORT_ERROR:
    return wcjson->errnum;
  default:
    panic();
  }
}

static struct wcjson_document *wcjsondoc_new(void) {
  struct wcjson_document *restrict const doc =
      heap_calloc(1, sizeof(struct wcjson_document));

  doc->values = heap_calloc(WCJSON_MAX_VALUES, sizeof(struct wcjson_value));
  doc->v_nitems = WCJSON_MAX_VALUES;
  doc->v_next = 0;
  doc->strings = heap_calloc(WCJSON_MAX_CHARACTERS, sizeof(wchar_t));
  doc->s_nitems = WCJSON_MAX_CHARACTERS;
  doc->s_next = 0;
  doc->mbstrings = heap_calloc(WCJSON_MAX_MBCHARACTERS, sizeof(char));
  doc->mb_nitems = WCJSON_MAX_MBCHARACTERS;
  doc->mb_next = 0;
  doc->esc = heap_calloc(WCJSON_MAX_ESCCHARACTERS, sizeof(wchar_t));
  doc->e_nitems = WCJSON_MAX_ESCCHARACTERS;

  return doc;
}

static void wcjsondoc_delete(struct wcjson_document *restrict const doc) {
  heap_free(doc->values);
  heap_free(doc->strings);
  heap_free(doc->mbstrings);
  heap_free(doc->esc);
  heap_free(doc);
}

static const char *
wcjsondoc_string(char *restrict const dst, size_t dst_nitems,
                 const struct wcjson_document *restrict const doc,
                 const struct wcjson_value *restrict const value,
                 size_t *restrict s_len) {
  wchar_t wc[WCJSON_BODY_MAX + 1] = {0};
  size_t wc_len = nitems(wc) - 1;

  if (wcjsondocsprint(wc, &wc_len, doc, value) < 0)
    panic();

  size_t mb_len = wcstombs(dst, wc, dst_nitems);
  if (mb_len == (size_t)-1 || mb_len == dst_nitems)
    panic();

  if (s_len != NULL)
    *s_len = mb_len;

  return dst;
}

static struct wcjson_value *
wcjson_string(struct wcjson_document *restrict const doc,
              const char *restrict const s) {
  wchar_t wc[WCJSON_STRLEN_MAX + 1] = {0};
  const size_t wc_len = mbstowcs(wc, s, nitems(wc));

  if (wc_len == (size_t)-1 || wc_len == nitems(wc))
    panic();

  const wchar_t *restrict const doc_s = wcjson_document_string(doc, wc, wc_len);

  if (doc_s == NULL)
    panic();

  struct wcjson_value *restrict const j_s =
      wcjson_value_string(doc, doc_s, wc_len);

  if (j_s == NULL)
    panic();

  return j_s;
}

static struct wcjson_value *
wcjson_number(struct wcjson_document *restrict const doc,
              const char *restrict const s) {
  struct wcjson_value *restrict const r = wcjson_string(doc, s);
  r->is_string = 0;
  r->is_number = 1;
  return r;
}

static struct wcjson_value *
wcjson_object(struct wcjson_document *restrict const doc) {
  struct wcjson_value *restrict const o = wcjson_value_object(doc);
  if (o == NULL)
    panic();
  return o;
}

static struct wcjson_value *
wcjson_array(struct wcjson_document *restrict const doc) {
  struct wcjson_value *restrict const a = wcjson_value_array(doc);
  if (a == NULL)
    panic();
  return a;
}

static struct wcjson_value *
wcjson_bool(struct wcjson_document *restrict const doc, const bool flag) {
  struct wcjson_value *restrict const v = wcjson_value_bool(doc, flag);
  if (v == NULL)
    panic();
  return v;
}

static void wcjson_array_add(struct wcjson_document *restrict const doc,
                             struct wcjson_value *restrict const arr,
                             struct wcjson_value *restrict const value) {
  if (wcjson_array_add_tail(doc, arr, value) < 0)
    panic();
}

static void wcjson_object_add(struct wcjson_document *restrict const doc,
                              struct wcjson_value *restrict const obj,
                              const wchar_t *key, const size_t key_len,
                              struct wcjson_value *restrict const value) {
  if (wcjson_object_add_tail(doc, obj, key, key_len, value) < 0)
    panic();
}

static inline char *b64_encode_uri(const char *restrict const s,
                                   const size_t s_len,
                                   size_t *restrict const e_len) {
  if (s_len > (SIZE_MAX / 4 - 1) * 3 - 2)
    panic();

  char *restrict const e = heap_malloc(4 * ((s_len + 2) / 3) + 1);
  if (EVP_EncodeBlock((unsigned char *)e, (const unsigned char *)s, s_len) < 0)
    panic();

  char *restrict p = e;
  *e_len = 0;

  // XXX: Check out of bounds
  while (*p) {
    switch (*p) {
    case '=':
      *p = '\0';
      return e;
    case '+':
      *p = '-';
      break;
    case '/':
      *p = '_';
      break;
    }

    *e_len += 1;
    p++;
  }

  return e;
}

static char *jwt_encode_cdp(const char *restrict const uri) {
  int r;
  size_t h_len, h_b64_len, p_len, p_b64_len, s_len, s_b64_len;
  char jwt_body[WCJSON_BODY_MAX + 1] = {0};
  char jwt_time[WCJSON_STRLEN_MAX + 1] = {0};
  struct wcjson_document jwt_doc = {
      .values = (struct wcjson_value[36]){0},
      .v_nitems = 36,
      .v_nitems_cnt = 0,
      .v_next = 0,
      .strings = (wchar_t[512]){0},
      .s_nitems = 512,
      .s_nitems_cnt = 0,
      .s_next = 0,
      .mbstrings = (char[512]){0},
      .mb_nitems = 512,
      .mb_nitems_cnt = 0,
      .mb_next = 0,
      .esc = (wchar_t[2048]){0},
      .e_nitems = 2048,
      .e_nitems_cnt = 0,
  };

  struct wcjson_value *restrict j_obj = wcjson_object(&jwt_doc);
  wcjson_object_add(&jwt_doc, j_obj, L"alg", 3,
                    wcjson_string(&jwt_doc, "ES256"));

  wcjson_object_add(
      &jwt_doc, j_obj, L"kid", 3,
      wcjson_string(&jwt_doc, String_chars(coinbase_cnf->jwt_kid)));

  wcjson_object_add(&jwt_doc, j_obj, L"typ", 3, wcjson_string(&jwt_doc, "JWT"));
  wcjsondoc_string(jwt_body, sizeof(jwt_body), &jwt_doc, jwt_doc.values,
                   &h_len);

  char *restrict const h_b64 = b64_encode_uri(jwt_body, h_len, &h_b64_len);

#ifdef ABAG_COINBASE_DEBUG
  wout("coinbase: JWT header: %s: %s\n", jwt_body, h_b64);
#endif

  jwt_doc.v_next = 0;
  jwt_doc.s_next = 0;
  jwt_doc.mb_next = 0;

  j_obj = wcjson_object(&jwt_doc);
  wcjson_object_add(&jwt_doc, j_obj, L"iss", 3, wcjson_string(&jwt_doc, "cdp"));
  wcjson_object_add(
      &jwt_doc, j_obj, L"sub", 3,
      wcjson_string(&jwt_doc, String_chars(coinbase_cnf->jwt_kid)));

  if (uri != NULL)
    wcjson_object_add(&jwt_doc, j_obj, L"uri", 3, wcjson_string(&jwt_doc, uri));

  const time_t now = time(NULL);
  r = snprintf(jwt_time, sizeof(jwt_time), "%" PRIdMAX, (intmax_t)now - 1);

  if (r < 0 || (size_t)r >= sizeof(jwt_time))
    panic();

  wcjson_object_add(&jwt_doc, j_obj, L"nbf", 3,
                    wcjson_number(&jwt_doc, jwt_time));

  r = snprintf(jwt_time, sizeof(jwt_time), "%" PRIdMAX, (intmax_t)now + 120);

  if (r < 0 || (size_t)r >= sizeof(jwt_time))
    panic();

  wcjson_object_add(&jwt_doc, j_obj, L"exp", 3,
                    wcjson_number(&jwt_doc, jwt_time));

  wcjsondoc_string(jwt_body, sizeof(jwt_body), &jwt_doc, jwt_doc.values,
                   &p_len);

  char *restrict const p_b64 = b64_encode_uri(jwt_body, p_len, &p_b64_len);

#ifdef ABAG_COINBASE_DEBUG
  wout("coinbase: JWT payload: %s: %s\n", jwt_body, p_b64);
#endif

  BIO *restrict bio = BIO_new_mem_buf(String_chars(coinbase_cnf->jwt_key),
                                      String_length(coinbase_cnf->jwt_key));

  if (!bio)
    panic();

  EVP_PKEY *restrict const key = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);

  if (!key)
    panic();

  BIO_free(bio);

  if (h_b64_len > SIZE_MAX - p_b64_len - 2 ||
      p_b64_len > SIZE_MAX - h_b64_len - 2)
    panic();

  const size_t hdotp_len = h_b64_len + p_b64_len + 1;
  char *restrict const hdotp = heap_malloc(hdotp_len + 1);
  r = snprintf(hdotp, hdotp_len + 1, "%s.%s", h_b64, p_b64);

  if (r < 0 || (size_t)r >= hdotp_len + 1)
    panic();

  EVP_MD_CTX *restrict const sign_ctx = EVP_MD_CTX_new();

  if (!sign_ctx)
    panic();

  if (EVP_DigestSignInit(sign_ctx, NULL, EVP_sha256(), NULL, key) <= 0 ||
      EVP_DigestSignUpdate(sign_ctx, (unsigned char *)hdotp, hdotp_len) <= 0 ||
      EVP_DigestSignFinal(sign_ctx, NULL, &s_len) <= 0)
    panic();

  char *restrict const s = heap_malloc(s_len + 1);

  if (EVP_DigestSignFinal(sign_ctx, (unsigned char *)s, &s_len) <= 0)
    panic();

  const unsigned char *p = (const unsigned char *)s;
  ECDSA_SIG *restrict sig = d2i_ECDSA_SIG(NULL, &p, s_len);

  if (!sig)
    panic();

  const BIGNUM *ecdsa_r;
  const BIGNUM *ecdsa_s;

  ECDSA_SIG_get0(sig, &ecdsa_r, &ecdsa_s);

  unsigned char raw[64] = {0};

  if (BN_bn2binpad(ecdsa_r, raw, 32) != 32 ||
      BN_bn2binpad(ecdsa_s, raw + 32, 32) != 32)
    panic();

  ECDSA_SIG_free(sig);

  char *restrict const s_b64 =
      b64_encode_uri((const char *)raw, sizeof(raw), &s_b64_len);

#ifdef ABAG_COINBASE_DEBUG
  wout("coinbase: JWT signature: %s\n", s_b64);
#endif

  if (hdotp_len > SIZE_MAX - 2 - s_b64_len ||
      s_b64_len > SIZE_MAX - 2 - hdotp_len)
    panic();

  const size_t jwt_len = hdotp_len + s_b64_len + 1;
  char *restrict const jwt = heap_malloc(jwt_len + 1);
  r = snprintf(jwt, jwt_len + 1, "%s.%s", hdotp, s_b64);

  if (r < 0 || (size_t)r >= jwt_len + 1)
    panic();

#ifdef ABAG_COINBASE_DEBUG
  wout("coinbase: JWT: %s\n", jwt);
#endif

  heap_free(s_b64);
  heap_free(s);
  EVP_MD_CTX_free(sign_ctx);
  heap_free(hdotp);
  EVP_PKEY_free(key);
  heap_free(p_b64);
  heap_free(h_b64);
  return jwt;
}

static void mg_mgr_config(struct mg_mgr *restrict const mgr) {
  if (cnf->dns_v4)
    mgr->dns4.url = String_chars(cnf->dns_v4);

  if (cnf->dns_v6)
    mgr->dns6.url = String_chars(cnf->dns_v6);

  if (cnf->dns_to) {
    struct Numeric *restrict r0 = Numeric_div(cnf->dns_to, milli_nanos);
    mgr->dnstimeout = Numeric_to_int(r0);
    Numeric_delete(r0);
  }
}

static int mg_mgr_worker_func(void *restrict const arg) {
  struct mg_mgr *restrict const mgr = arg;

  while (running)
    mg_mgr_poll(mgr, coinbase_timeout_ms / 2);

  mg_mgr_free(mgr);
  heap_free(mgr);
  thread_exit(EXIT_SUCCESS);
}

static void ws_ticker_update(const struct wcjson_document *restrict const doc,
                             const struct wcjson_value *restrict const ticker,
                             const struct Numeric *restrict const nanos) {
  char errbuf[WCJSON_BODY_MAX + 1] = {0};
  WCJSON_DECLARE_MARKET_ITEM_OPT(product_id)
  WCJSON_DECLARE_NUMERIC_ITEM_OPT(price)

  WCJSON_MARKET_ITEM_OPT(doc, ticker, product_id, 10, errbuf, ret)
  WCJSON_NUMERIC_ITEM_OPT(doc, ticker, price, 5, errbuf, ret)

  if (j_product_id_m != NULL && j_price_num != NULL &&
      Numeric_cmp(j_price_num, zero) > 0) {
    struct Sample *restrict const s = Sample_new();
    s->m_id = String_copy(j_product_id_m->id);
    s->nanos = Numeric_copy(nanos);
    s->price = j_price_num;
    j_price_num = NULL;

    Queue_enqueue_await(samples, s);

    if (Queue_enqueue_timedout(samples)) {
      werr("coinbase: Enqueuing ticker timed out after %" PRIdMAX
           " seconds: %s\n",
           (intmax_t)(coinbase_stall_ms / 1000L),
           wcjsondoc_string(errbuf, sizeof(errbuf), doc, ticker, NULL));

      Sample_delete(s);
    }
  }

ret:
  Numeric_delete(j_price_num);

  if (j_product_id_m != NULL)
    mutex_unlock(j_product_id_m->mtx);

  return;
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
  struct Order *restrict o = NULL;
  char errbuf[WCJSON_BODY_MAX + 1] = {0};
  WCJSON_DECLARE_STRING_ITEM(order_id)
  WCJSON_DECLARE_MARKET_ITEM(product_id)
  WCJSON_DECLARE_NUMERIC_ITEM(cumulative_quantity)
  WCJSON_DECLARE_NUMERIC_ITEM(leaves_quantity)
  WCJSON_DECLARE_NUMERIC_ITEM(filled_value)
  WCJSON_DECLARE_NUMERIC_ITEM(total_fees)
  WCJSON_DECLARE_NUMERIC_ITEM(outstanding_hold_amount)
  WCJSON_DECLARE_ISO8601_NANOS_ITEM(creation_time)
  WCJSON_DECLARE_STRING_ITEM(status)
  WCJSON_DECLARE_NUMERIC_ITEM(limit_price)
  WCJSON_DECLARE_STRING_ITEM_OPT(reject_Reason)
  WCJSON_DECLARE_STRING_ITEM_OPT(cancel_reason)

  WCJSON_STRING_ITEM(doc, order, order_id, 8, errbuf, ret)
  WCJSON_MARKET_ITEM(doc, order, product_id, 10, errbuf, ret)
  WCJSON_NUMERIC_ITEM(doc, order, cumulative_quantity, 19, errbuf, ret)
  WCJSON_NUMERIC_ITEM(doc, order, leaves_quantity, 15, errbuf, ret)
  WCJSON_NUMERIC_ITEM(doc, order, filled_value, 12, errbuf, ret)
  WCJSON_NUMERIC_ITEM(doc, order, total_fees, 10, errbuf, ret)
  WCJSON_NUMERIC_ITEM(doc, order, outstanding_hold_amount, 23, errbuf, ret)
  WCJSON_ISO8601_NANOS_ITEM(doc, order, creation_time, 13, errbuf, ret)
  WCJSON_STRING_ITEM(doc, order, status, 6, errbuf, ret)
  WCJSON_NUMERIC_ITEM(doc, order, limit_price, 11, errbuf, ret)
  WCJSON_STRING_ITEM_OPT(doc, order, reject_Reason, 13, errbuf, ret)
  WCJSON_STRING_ITEM_OPT(doc, order, cancel_reason, 13, errbuf, ret)

  struct String *msg = NULL;

  if (j_reject_Reason_exists) {
    msg = String_cnew(j_reject_Reason->mbstring);
    if (String_length(msg) == 0) {
      String_delete(msg);
      msg = NULL;
    }
  }

  if (msg == NULL && j_cancel_reason_exists) {
    msg = String_cnew(j_cancel_reason->mbstring);
    if (String_length(msg) == 0) {
      String_delete(msg);
      msg = NULL;
    }
  }

  o = Order_new();
  o->id = String_cnew(j_order_id->mbstring);
  o->m_id = String_copy(j_product_id_m->id);
  o->status = order_status(j_status->mbstring);
  o->cnanos = j_creation_time_nanos;
  o->b_ordered = Numeric_add(j_cumulative_quantity_num, j_leaves_quantity_num);
  o->p_ordered = j_limit_price_num;
  o->b_filled = j_cumulative_quantity_num;
  o->q_filled = j_filled_value_num;
  o->q_fees = j_total_fees_num;
  o->msg = msg;
  o->settled = Numeric_cmp(j_leaves_quantity_num, zero) == 0 &&
               Numeric_cmp(j_outstanding_hold_amount_num, zero) == 0 &&
               Numeric_cmp(o->b_ordered, o->b_filled) == 0;
  o->dnanos = o->settled ? Numeric_copy(nanos) : NULL;

  if (o->status == ORDER_STATUS_UNKNOWN)
    werr("coinbase: %s: user: %s: Order status unknown: %s\n", coinbase_ws_uri,
         j_status->mbstring,
         wcjsondoc_string(errbuf, sizeof(errbuf), doc, order, NULL));

  if ((o->status == ORDER_STATUS_CANCELLED ||
       o->status == ORDER_STATUS_FAILED || o->status == ORDER_STATUS_EXPIRED) &&
      Numeric_cmp(j_outstanding_hold_amount_num, zero) != 0) {
    Order_delete(o);
    goto ret;
  }

  /*
   * The user channel is lacking the last_fill_time property the REST API
   * provides. The timestamp of the event is used instead. Sadly the events are
   * sent out before the final settle is performed so that the timestamp of the
   * event can be lower a few nanoseconds or microsends than the timestamp of
   * the real settle. For orders filled immediately this can lead to the event
   * timestamp lying before the creation timestamp. There is no other way to
   * poll the REST API due to the channel events lacking that information.
   */
  if (o->settled) {
    struct Order *restrict const tmp = o;
    o = coinbase_order(o->id);
    Order_delete(tmp);
    if (o == NULL)
      goto ret;
  }

  Queue_enqueue_await(orders, o);
ret:
  if (j_product_id_m != NULL)
    mutex_unlock(j_product_id_m->mtx);

  Numeric_delete(j_leaves_quantity_num);
  Numeric_delete(j_outstanding_hold_amount_num);
}

static int mb_decode(wchar_t *restrict const wc, size_t *wc_len,
                     const char *restrict const mb, const size_t mb_len) {
  size_t m_len = mb_len;
  size_t d_len = 0;
  mbstate_t mbs = {0};

  for (size_t i = 0, len = 0; (*wc_len)-- != 0 && m_len != 0;
       i += len, m_len -= len, d_len++) {
    switch (len = mbrtowc(wc + d_len, mb + i, m_len, &mbs)) {
    case 0:
      *wc_len = d_len;
      return 0;
    case (size_t)-1:
      werr("coinbase: Illegal byte sequence: %zu: 0x%.2hhx: %.*s\n", i,
           *(mb + i), (int)mb_len, mb);
      return -1;
    case (size_t)-2:
      werr("coinbase: Incomplete byte sequence: %zu: %.*s\n", i, (int)mb_len,
           mb);
      return -1;
    case (size_t)-3:
      // Not in ISO/IEC 9899:202y (en) - 7.33.6.4.3
      panic();
    }
  }

  if (*wc_len == SIZE_MAX || m_len != 0)
    panic();

  wc[d_len] = '\0';
  *wc_len = d_len;
  return 0;
}

static inline int
mg_ws_message_decode(wchar_t *restrict const wc, size_t *wc_len,
                     const struct mg_ws_message *restrict const msg) {
  return mb_decode(wc, wc_len, msg->data.buf, msg->data.len);
}

static inline int
mg_http_message_decode(wchar_t *restrict const wc, size_t *wc_len,
                       const struct mg_http_message *restrict const msg) {
  return mb_decode(wc, wc_len, msg->body.buf, msg->body.len);
}

static void ws_handle_message(const struct mg_ws_message *restrict const msg) {
  struct wcjson wcjson = WCJSON_INITIALIZER;
  char errbuf[WCJSON_BODY_MAX + 1] = {0};
  WCJSON_DECLARE_STRING_ITEM(channel)
  WCJSON_DECLARE_STRING_ITEM_OPT(type)
  WCJSON_DECLARE_STRING_ITEM_OPT(message)
  WCJSON_DECLARE_ISO8601_NANOS_ITEM(timestamp)
  WCJSON_DECLARE_ARRAY_ITEM(events)

  ws_doc->v_next = 0;
  ws_doc->s_next = 0;
  ws_doc->mb_next = 0;

  wchar_t wcmsg[WCJSON_BODY_MAX + 1] = {0};
  size_t wc_len = nitems(wcmsg) - 1;
  if (mg_ws_message_decode(wcmsg, &wc_len, msg))
    return;

  if (wcjsondocvalues(&wcjson, ws_doc, wcmsg, wc_len) < 0) {
    werr("coinbase: Failure parsing message: %s: %.*s\n",
         strerror(wcjsontoerrno(&wcjson)), (int)msg->data.len, msg->data.buf);
    return;
  }

  if (wcjsondocstrings(&wcjson, ws_doc) < 0) {
    werr("coinbase: Failure parsing message: %s: %.*s\n",
         strerror(wcjsontoerrno(&wcjson)), (int)msg->data.len, msg->data.buf);
    return;
  }

  if (wcjsondocmbstrings(&wcjson, ws_doc) < 0) {
    werr("coinbase: Failure parsing message: %s: %.*s\n",
         strerror(wcjsontoerrno(&wcjson)), (int)msg->data.len, msg->data.buf);
    return;
  }

  WCJSON_STRING_ITEM_OPT(ws_doc, ws_doc->values, type, 4, errbuf, ret)

  if (j_type_exists) {
    WCJSON_STRING_ITEM_OPT(ws_doc, ws_doc->values, message, 7, errbuf, ret)
    werr("coinbase: %.*s\n", (int)msg->data.len, msg->data.buf);

    if (j_message_exists)
      werr("coinbase: %s\n", j_message->mbstring);

    return;
  }

  WCJSON_STRING_ITEM(ws_doc, ws_doc->values, channel, 7, errbuf, ret)
  struct ws_channel *restrict const channel = ws_channel(j_channel->mbstring);

  if (channel == NULL) {
    werr("coinbase: %s: %.*s\n", j_channel->mbstring, (int)msg->data.len,
         msg->data.buf);
    return;
  }

#ifdef ABAG_COINBASE_DEBUG
  if (channel->debug)
    wout("coinbase: %s: %.*s\n", j_channel->mbstring, (int)msg->data.len,
         msg->data.buf);
#endif

  if (channel->items == NULL)
    return;

  WCJSON_ISO8601_NANOS_ITEM(ws_doc, ws_doc->values, timestamp, 9, errbuf, ret)
  WCJSON_ARRAY_ITEM(ws_doc, ws_doc->values, events, 6, errbuf, ret_free)

  const struct wcjson_value *restrict j_evt = NULL;
  wcjson_value_foreach(j_evt, ws_doc, j_events) {
    WCJSON_STRING_ITEM(ws_doc, j_evt, type, 4, errbuf, ret_free)

    const struct wcjson_value *restrict const j_evt_items =
        wcjson_object_get(ws_doc, j_evt, channel->items, channel->items_len);

    if (j_evt_items == NULL || !j_evt_items->is_array) {
      werr("coinbase: No '%ls' array item: %s\n", channel->items,
           wcjsondoc_string(errbuf, sizeof(errbuf), ws_doc, j_evt, NULL));
      goto ret_free;
    }

    channel->last_message = mg_millis();

    if (j_type->mb_len == 6 && !strcmp("update", j_type->mbstring)) {
      if (channel->update) {
        const struct wcjson_value *restrict j_evt_item = NULL;
        wcjson_value_foreach(j_evt_item, ws_doc, j_evt_items) {
          channel->update(ws_doc, j_evt_item, j_timestamp_nanos);
        }
      }
    } else if (j_type->mb_len == 8 && !strcmp("snapshot", j_type->mbstring)) {
      if (channel->snapshot) {
        const struct wcjson_value *restrict j_evt_item = NULL;
        wcjson_value_foreach(j_evt_item, ws_doc, j_evt_items) {
          channel->snapshot(ws_doc, j_evt_item, j_timestamp_nanos);
        }
      }
    } else
      werr("coinbase: %s: %s\n", channel->name,
           wcjsondoc_string(errbuf, sizeof(errbuf), ws_doc, j_evt, NULL));
  }
ret_free:
  Numeric_delete(j_timestamp_nanos);
ret:
  return;
}

static void ws_subscribe(struct mg_connection *restrict const c,
                         const struct ws_channel *restrict const channel) {
  struct wcjson_document *restrict const doc = wcjsondoc_new();
  void *const *restrict items;

  struct Array *restrict const m_array = coinbase_markets();
  struct wcjson_value *restrict const j_msg = wcjson_object(doc);
  struct wcjson_value *restrict const j_arr = wcjson_array(doc);

  items = Array_items(m_array);
  for (size_t i = Array_size(m_array); i-- > 0;) {
    struct wcjson_value *restrict const j_id =
        wcjson_string(doc, String_chars(((struct Market *)items[i])->sym));

    wcjson_array_add(doc, j_arr, j_id);
  }
  Array_unlock(m_array);

  struct wcjson_value *restrict const j_subscribe =
      wcjson_string(doc, "subscribe");

  wcjson_object_add(doc, j_msg, L"type", 4, j_subscribe);
  wcjson_object_add(doc, j_msg, L"product_ids", 11, j_arr);

  char *restrict const jwt = jwt_encode_cdp(NULL);

  struct wcjson_value *restrict const j_jwt = wcjson_string(doc, jwt);
  wcjson_object_add(doc, j_msg, L"jwt", 3, j_jwt);

  struct wcjson_value *restrict const j_hb = wcjson_string(doc, "heartbeats");
  wcjson_object_add(doc, j_msg, L"channel", 7, j_hb);

  char hb_body[WCJSON_BODY_MAX + 1] = {0};
  size_t hb_len = 0;
  wcjsondoc_string(hb_body, sizeof(hb_body), doc, doc->values, &hb_len);

  struct wcjson_value *restrict const j_ch = wcjson_string(doc, channel->name);
  (void)wcjson_object_remove(doc, j_msg, L"channel", 7);
  wcjson_object_add(doc, j_msg, L"channel", 7, j_ch);

  char ch_body[WCJSON_BODY_MAX + 1] = {0};
  size_t ch_len = 0;
  wcjsondoc_string(ch_body, sizeof(ch_body), doc, doc->values, &ch_len);

  mg_ws_send(c, hb_body, hb_len, WEBSOCKET_OP_TEXT);
  mg_ws_send(c, ch_body, ch_len, WEBSOCKET_OP_TEXT);

  heap_free(jwt);
  wcjsondoc_delete(doc);
}

static void ws_listener(struct mg_connection *c, int ev, void *ev_data) {
  struct ws_channel *restrict const channel = c->fn_data;

  switch (ev) {
  case MG_EV_CONNECT: {
#ifdef ABAG_COINBASE_DEBUG
    wout("coinbase: %s: %lu: MG_EV_CONNECT\n", channel->name, c->id);
#endif
    struct mg_tls_opts ws_tls_opts = {0};
    ws_tls_opts.name = mg_url_host(coinbase_ws_uri);

    mg_tls_init(c, &ws_tls_opts);
    break;
  }
  case MG_EV_ERROR: {
    // XXX: ev_data
    werr("coinbase: %s: %s: %lu: %s\n", coinbase_ws_uri, channel->name, c->id,
         (char *)ev_data);
    c->is_closing = 1;
    break;
  }
  case MG_EV_WS_OPEN: {
#ifdef ABAG_COINBASE_DEBUG
    wout("coinbase: %s: %lu: MG_EV_WS_OPEN\n", channel->name, c->id);
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
        wout("coinbase: %s: %lu: WEBSOCKET_OP_CLOSE\n", channel->name, c->id);
#endif
        c->is_closing = 1;
      } else
        werr("coinbase: %s: %s: %lu: %d\n", coinbase_ws_uri, channel->name,
             c->id, type);

    } else
      c->is_closing = 1;

    break;
  }
  case MG_EV_CLOSE: {
#ifdef ABAG_COINBASE_DEBUG
    wout("coinbase: %s: %lu: MG_EV_CLOSE\n", channel->name, c->id);
#endif
    if (running) {
      struct mg_mgr *restrict const mgr = c->mgr;

      do {
        thread_sleep(&coinbase_retry_rate);
        c = mg_ws_connect(mgr, coinbase_ws_uri, ws_listener, channel, NULL);
        if (!c)
          werr("coinbase: %s: %s: Failure reconnecting\n", coinbase_ws_uri,
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
      wout("coinbase: %s: %s: No events\n", coinbase_ws_uri, channel->name);
  }

  if (channel->reconnect) {
    channel->reconnect = false;
    c->is_closing = 1;
  }
}

static void http_listener(struct mg_connection *c, int ev, void *ev_data) {
  struct http_listener_ctx *restrict const http_ctx = c->fn_data;
  const char *restrict const method = http_ctx->body_len > 0 ? "POST" : "GET";

  switch (ev) {
  case MG_EV_OPEN:
    http_ctx->exp_time = mg_millis() + coinbase_timeout_ms;
    break;
  case MG_EV_CLOSE:
    http_ctx->done = true;
    break;
  case MG_EV_POLL: {
    if (mg_millis() > http_ctx->exp_time &&
        (c->is_connecting || c->is_resolving)) {
      mg_error(c, "Connect timeout");
    }
    break;
  }
  case MG_EV_ERROR: {
    c->is_draining = 1;
    http_ctx->success = false;
    werr("coinbase: %s\n", (char *)ev_data);
    break;
  }
  case MG_EV_CONNECT: {
    struct mg_str host = mg_url_host(http_ctx->url);

    if (c->is_tls) {
      struct mg_tls_opts http_tls_opts = {0};
      http_tls_opts.name = host;
      mg_tls_init(c, &http_tls_opts);
    }

    char uri[URL_MAX_LENGTH + 1] = {0};
    int r = snprintf(uri, sizeof(uri), "%s %.*s%s", method, (int)host.len,
                     host.buf, http_ctx->path);
    if (r < 0 || (size_t)r >= sizeof(uri))
      panic();

    char *restrict const jwt = jwt_encode_cdp(uri);

    // XXX: mg_printf does not support %zu
    mg_printf(c,
              "%s %s HTTP/1.0\r\n"
              "Authorization: Bearer %s\r\n"
              "Host: %.*s\r\n"
              "Content-Type: application/json\r\n"
              "Content-Length: %u\r\n"
              "Connection: close\r\n"
              "User-Agent: Abagnale; %s\r\n"
              "\r\n",
              method, mg_url_uri(http_ctx->url), jwt, (int)host.len, host.buf,
              http_ctx->body_len, ABAG_REVISION);

    mg_send(c, http_ctx->body, http_ctx->body_len);
    heap_free(jwt);
    break;
  }
  case MG_EV_HTTP_MSG: {
    struct mg_http_message *restrict const msg = ev_data;
    http_ctx->success = mg_http_status(msg) == 200;
    c->is_draining = 1;

#ifdef ABAG_COINBASE_DEBUG
    wout("coinbase: %.*s\n", (int)msg->message.len, msg->message.buf);
#endif

    if (!http_ctx->success) {
      werr("coinbase: %s: HTTP %d: %.*s\n", http_ctx->url, mg_http_status(msg),
           (int)msg->body.len, msg->body.buf);
      return;
    }

    http_ctx->rsp_len = HTTP_RESPONSE_MAX_WCHARS;
    http_ctx->rsp = heap_calloc(HTTP_RESPONSE_MAX_WCHARS + 1, sizeof(wchar_t));

    if (mg_http_message_decode(http_ctx->rsp, &http_ctx->rsp_len, msg))
      http_ctx->success = false;

    break;
  }
  }
}

static int http_req(struct wcjson_document *restrict const doc,
                    const char *restrict const url,
                    const char *restrict const path,
                    const char *restrict const body, const size_t body_len) {
  struct mg_mgr mgr = {0};
  struct mg_connection *restrict c = NULL;
  int r = -1;
  struct http_listener_ctx http_ctx = {
      .success = false,
      .done = false,
      .url = url,
      .path = path,
      .body = body,
      .body_len = body_len,
      .rsp = NULL,
      .rsp_len = 0,
  };

#ifdef ABAG_COINBASE_DEBUG
  wout("coinbase: HTTP %s %s\n", body != NULL ? "POST" : "GET", url);
  if (body != NULL)
    wout("%.*s\n", (int)body_len, body);
#endif

  thread_sleep(&coinbase_request_rate);

  mg_mgr_init(&mgr);
  mg_mgr_config(&mgr);

  c = mg_http_connect(&mgr, url, http_listener, &http_ctx);

  if (c == NULL) {
    werr("coinbase: %s: Could not create HTTP connection\n", url);
    goto err;
  }

  while (!http_ctx.done)
    mg_mgr_poll(&mgr, coinbase_timeout_ms);

  if (!http_ctx.success)
    goto err;

  struct wcjson wcjson = WCJSON_INITIALIZER;

  r = wcjsondocvalues(&wcjson, doc, http_ctx.rsp, http_ctx.rsp_len);
  if (r < 0) {
    werr("coinbase: Failure parsing response: %s: %s: %.*ls\n", url,
         strerror(wcjsontoerrno(&wcjson)), (int)http_ctx.rsp_len, http_ctx.rsp);
    goto err;
  }

  if (doc->values == NULL) {
    doc->values = heap_calloc(doc->v_nitems_cnt, sizeof(struct wcjson_value));
    doc->v_nitems = doc->v_nitems_cnt;
    (void)wcjsondocvalues(&wcjson, doc, http_ctx.rsp, http_ctx.rsp_len);
  }

  if (doc->strings == NULL) {
    doc->strings = heap_calloc(doc->s_nitems_cnt, sizeof(wchar_t));
    doc->s_nitems = doc->s_nitems_cnt;
  }

  r = wcjsondocstrings(&wcjson, doc);
  if (r < 0) {
    werr("coinbase: Failure parsing response: %s: %s: %.*ls\n", url,
         strerror(wcjsontoerrno(&wcjson)), (int)http_ctx.rsp_len, http_ctx.rsp);
    goto err;
  }

  if (doc->mbstrings == NULL) {
    doc->mbstrings = heap_calloc(doc->mb_nitems_cnt, sizeof(char));
    doc->mb_nitems = doc->mb_nitems_cnt;
  }

  if (doc->esc == NULL) {
    doc->esc = heap_calloc(doc->e_nitems_cnt, sizeof(wchar_t));
    doc->e_nitems = doc->e_nitems_cnt;
  }

  r = wcjsondocmbstrings(&wcjson, doc);
  if (r < 0) {
    werr("coinbase: Failure parsing response: %s: %s: %.*ls\n", url,
         strerror(wcjsontoerrno(&wcjson)), (int)http_ctx.rsp_len, http_ctx.rsp);
    goto err;
  }

  r = 0;
err:
  mg_mgr_free(&mgr);
  heap_free(http_ctx.rsp);
  return r;
}

static void coinbase_init(void) {
  exchange_coinbase.id = String_cnew(COINBASE_UUID);
  exchange_coinbase.nm = String_cnew("coinbase");
  coinbase_rest_uri = envs("CDP_REST_URI", DEFAULT_CDP_REST_URI);
  coinbase_ws_uri = envs("CDP_WS_URI", DEFAULT_CDP_WS_URI);
  coinbase_account_path = envs("CDP_ACCOUNT_PATH", DEFAULT_CDP_ACCOUNT_PATH);
  coinbase_accounts_path = envs("CDP_ACCOUNTS_PATH", DEFAULT_CDP_ACCOUNTS_PATH);
  coinbase_fees_path = envs("CDP_FEES_PATH", DEFAULT_CDP_FEES_PATH);
  coinbase_order_path = envs("CDP_ORDER_PATH", DEFAULT_CDP_ORDER_PATH);
  coinbase_products_path = envs("CDP_PRODUCTS_PATH", DEFAULT_CDP_PRODUCTS_PATH);
  coinbase_order_cancel_path =
      envs("CDP_ORDER_CANCEL_PATH", DEFAULT_CDP_ORDER_CANCEL_PATH);

  coinbase_order_create_path =
      envs("CDP_ORDER_CREATE_PATH", DEFAULT_CDP_ORDER_CREATE_PATH);

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

  coinbase_timeout_ms =
      envul("CDP_HTTP_TIMEOUT_MILLIS", DEFAULT_CDP_HTTP_TIMEOUT_MILLIS);

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
    wout("\tCDP_HTTP_TIMEOUT_MILLIS=%lu\n", coinbase_timeout_ms);
  }

  running = false;
  coinbase_cnf = NULL;
  coinbase_db = NULL;
  orders = Queue_new(128, (time_t)0);
  samples = Queue_new((MG_MAX_RECV_SIZE) / sizeof(struct Sample *),
                      (time_t)(coinbase_stall_ms / 1000L));
  mg_log_set(MG_LL_NONE); // NONE, ERROR, INFO, DEBUG, VERBOSE
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
  ws_doc = wcjsondoc_new();
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
  wcjsondoc_delete(ws_doc);
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
          mgr, coinbase_ws_uri, ws_listener, &ws_channels[i], NULL);

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
    werr("coinbase: Dequeuing ticker timed out after %" PRIdMAX " seconds\n",
         (intmax_t)(coinbase_stall_ms / 1000L));

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
  char m_id[DATABASE_UUID_MAX_LENGTH + 1] = {0};
  char errbuf[WCJSON_BODY_MAX + 1] = {0};
  struct Market *restrict m = NULL;
  WCJSON_DECLARE_STRING_ITEM(product_id)
  WCJSON_DECLARE_STRING_ITEM(base_currency_id)
  WCJSON_DECLARE_STRING_ITEM(quote_currency_id)
  WCJSON_DECLARE_NUMERIC_ITEM(price_increment)
  WCJSON_DECLARE_NUMERIC_ITEM(base_increment)
  WCJSON_DECLARE_NUMERIC_ITEM(quote_increment)
  WCJSON_DECLARE_BOOL_ITEM(is_disabled)
  WCJSON_DECLARE_BOOL_ITEM(cancel_only)
  WCJSON_DECLARE_BOOL_ITEM(limit_only)
  WCJSON_DECLARE_BOOL_ITEM(post_only)
  WCJSON_DECLARE_BOOL_ITEM(trading_disabled)
  WCJSON_DECLARE_BOOL_ITEM(auction_mode)
  WCJSON_DECLARE_BOOL_ITEM(view_only)
  WCJSON_DECLARE_BOOL_ITEM(new)
  WCJSON_DECLARE_STRING_ITEM(status)
  WCJSON_DECLARE_STRING_ITEM(product_type)

  WCJSON_STRING_ITEM(doc, prod, product_id, 10, errbuf, ret)
  WCJSON_STRING_ITEM(doc, prod, base_currency_id, 16, errbuf, ret)
  //  WCJSON_NUMERIC_ITEM(doc, prod, base_min_size, 13, errbuf, ret)
  WCJSON_STRING_ITEM(doc, prod, quote_currency_id, 17, errbuf, ret)
  //  WCJSON_NUMERIC_ITEM(doc, prod, quote_min_size, 14, errbuf, ret)
  WCJSON_NUMERIC_ITEM(doc, prod, price_increment, 15, errbuf, ret)
  WCJSON_NUMERIC_ITEM(doc, prod, base_increment, 14, errbuf, ret)
  WCJSON_NUMERIC_ITEM(doc, prod, quote_increment, 15, errbuf, ret)
  WCJSON_BOOL_ITEM(doc, prod, is_disabled, 11, errbuf, ret)
  WCJSON_BOOL_ITEM(doc, prod, cancel_only, 11, errbuf, ret)
  WCJSON_BOOL_ITEM(doc, prod, limit_only, 10, errbuf, ret)
  WCJSON_BOOL_ITEM(doc, prod, post_only, 9, errbuf, ret)
  WCJSON_BOOL_ITEM(doc, prod, trading_disabled, 16, errbuf, ret)
  WCJSON_BOOL_ITEM(doc, prod, auction_mode, 12, errbuf, ret)
  WCJSON_BOOL_ITEM(doc, prod, view_only, 9, errbuf, ret)
  WCJSON_BOOL_ITEM(doc, prod, new, 3, errbuf, ret)
  WCJSON_STRING_ITEM(doc, prod, status, 6, errbuf, ret)
  WCJSON_STRING_ITEM(doc, prod, product_type, 12, errbuf, ret)

  db_symbol_to_id(m_id, coinbase_db, COINBASE_UUID, j_product_id->mbstring);

  // Extract scale from price increment.
  const char *restrict const p_dot = strchr(j_price_increment->mbstring, '.');
  const char *restrict const b_dot = strchr(j_base_increment->mbstring, '.');
  const char *restrict const q_dot = strchr(j_quote_increment->mbstring, '.');
  struct String *restrict const b_id =
      String_cnew(j_base_currency_id->mbstring);

  struct String *restrict const q_id =
      String_cnew(j_quote_currency_id->mbstring);

  // Find matching accounts required for trading.
  const struct Account *restrict qa = coinbase_account_currency(q_id);
  const struct Account *restrict ba = coinbase_account_currency(b_id);
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

  const enum market_status status_value = market_status(j_status->mbstring);
  const enum market_type type_value = market_type(j_product_type->mbstring);

  char nm[4096] = {0};
  int r = snprintf(nm, sizeof(nm), "%s@%s", j_base_currency_id->mbstring,
                   j_quote_currency_id->mbstring);

  if (r < 0 || (size_t)r >= sizeof(nm))
    panic();

  m = Market_new();
  m->id = String_cnew(m_id);
  m->b_id = b_id;
  m->ba_id = ba_id;
  m->q_id = q_id;
  m->qa_id = qa_id;
  m->nm = String_cnew(nm);
  m->sym = String_cnew(j_product_id->mbstring);
  m->type = type_value;
  m->status = status_value;
  m->p_sc = p_dot ? strlen(p_dot + 1) : 0;
  m->p_inc = j_price_increment_num;
  m->b_sc = b_dot ? strlen(b_dot + 1) : 0;
  m->b_inc = j_base_increment_num;
  m->q_sc = q_dot ? strlen(q_dot + 1) : 0;
  m->q_inc = j_quote_increment_num;
  m->is_tradeable =
      qa_id != NULL && ba_id != NULL && type_value == MARKET_TYPE_SPOT;

  m->is_active = !(j_is_disabled->is_true || j_cancel_only->is_true ||
                   j_post_only->is_true || j_trading_disabled->is_true ||
                   j_new->is_true) &&
                 status_value == MARKET_STATUS_ONLINE;

  if (m->type == MARKET_TYPE_UNKNOWN) {
    werr("coinbase: %s@%s: %s: Unsupported market type: %s\n",
         j_base_currency_id->mbstring, j_quote_currency_id->mbstring,
         j_product_type->mbstring,
         wcjsondoc_string(errbuf, sizeof(errbuf), doc, prod, NULL));
  }

  if (m->status == MARKET_STATUS_UNKNOWN) {
    werr("coinbase: %s@%s: %s: Unsupported market status: %s\n",
         j_base_currency_id->mbstring, j_quote_currency_id->mbstring,
         j_status->mbstring,
         wcjsondoc_string(errbuf, sizeof(errbuf), doc, prod, NULL));
  }

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
  if (!m->is_active) {
    wout("coinbase: %s@%s: Market not active: %s\n",
         j_base_currency_id->mbstring, j_quote_currency_id->mbstring,
         wcjsondoc_string(errbuf, sizeof(errbuf), doc, prod, NULL));
  }
#endif
ret:
  return m;
}

static struct Array *
parse_products(struct Array *restrict const p,
               const struct wcjson_document *restrict const doc) {
  char errbuf[WCJSON_BODY_MAX + 1] = {0};
  WCJSON_DECLARE_ARRAY_ITEM(products)

  WCJSON_ARRAY_ITEM(doc, doc->values, products, 8, errbuf, ret)

  const struct wcjson_value *restrict j_product = NULL;
  wcjson_value_foreach(j_product, doc, j_products) {
    struct Market *restrict const parsed = parse_product(doc, j_product);

    if (parsed != NULL)
      Array_add_tail(p, parsed);
  }

ret:
  return p;
}

static struct Array *coinbase_markets(void) {
  struct wcjson_document doc = WCJSON_DOCUMENT_INITIALIZER;
  char url[URL_MAX_LENGTH + 1] = {0};
  void *const *restrict items;

  Array_lock(markets);

  if (markets_reload) {
    accounts_reload = true;
    int r = snprintf(url, sizeof(url), "%s%s", coinbase_rest_uri,
                     coinbase_products_path);

    if (r < 0 || (size_t)r >= sizeof(url))
      panic();

    if (http_req(&doc, url, coinbase_products_path, NULL, 0) == 0) {
      Array_clear(markets, Market_delete);
      parse_products(markets, &doc);
      Array_shrink(markets);
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
    } else
      for (size_t i = nitems(ws_channels); i-- > 0;)
        ws_channels[i].reconnect = true;
  }

  heap_free(doc.values);
  heap_free(doc.strings);
  heap_free(doc.mbstrings);
  heap_free(doc.esc);
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
  struct Account *restrict a = NULL;
  char errbuf[WCJSON_BODY_MAX + 1] = {0};
  WCJSON_DECLARE_STRING_ITEM(uuid)
  WCJSON_DECLARE_STRING_ITEM(name)
  WCJSON_DECLARE_STRING_ITEM(currency)
  WCJSON_DECLARE_STRING_ITEM(type)
  WCJSON_DECLARE_OBJECT_ITEM(available_balance)
  WCJSON_DECLARE_NUMERIC_ITEM(value)
  WCJSON_DECLARE_BOOL_ITEM_OPT(active)
  WCJSON_DECLARE_BOOL_ITEM_OPT(ready)

  WCJSON_STRING_ITEM(doc, acct, uuid, 4, errbuf, ret)
  WCJSON_STRING_ITEM(doc, acct, name, 4, errbuf, ret)
  WCJSON_STRING_ITEM(doc, acct, currency, 8, errbuf, ret)
  WCJSON_STRING_ITEM(doc, acct, type, 4, errbuf, ret)
  WCJSON_OBJECT_ITEM(doc, acct, available_balance, 17, errbuf, ret)
  WCJSON_NUMERIC_ITEM(doc, j_available_balance, value, 5, errbuf, ret)
  WCJSON_BOOL_ITEM_OPT(doc, acct, active, 6, errbuf, ret)
  WCJSON_BOOL_ITEM_OPT(doc, acct, ready, 5, errbuf, ret)

  a = Account_new();
  a->id = String_cnew(j_uuid->mbstring);
  a->nm = String_cnew(j_name->mbstring);
  a->c_id = String_cnew(j_currency->mbstring);
  a->type = account_type(j_type->mbstring);
  a->avail = j_value_num;
  a->is_active = j_active_exists && j_active->is_true;
  a->is_ready = j_ready_exists && j_ready->is_true;

  if (a->type == ACCOUNT_TYPE_UNSPECIFIED)
    werr("coinbase: %s: Account type unsupported: %s\n", j_type->mbstring,
         wcjsondoc_string(errbuf, sizeof(errbuf), doc, acct, NULL));

ret:
  return a;
}

static int parse_accounts(struct Array *restrict const a,
                          const struct wcjson_document *restrict const doc) {
  char errbuf[WCJSON_BODY_MAX + 1] = {0};
  int r = -1;
  WCJSON_DECLARE_ARRAY_ITEM(accounts)

  WCJSON_ARRAY_ITEM(doc, doc->values, accounts, 8, errbuf, ret)

  const struct wcjson_value *restrict j_account = NULL;
  wcjson_value_foreach(j_account, doc, j_accounts) {
    struct Account *restrict const parsed = parse_account(doc, j_account);

    if (parsed != NULL) {
      if (parsed->type == ACCOUNT_TYPE_CRYPTO ||
          parsed->type == ACCOUNT_TYPE_FIAT)
        Array_add_tail(a, parsed);
      else
        Account_delete(parsed);
    }
  }

  r = 0;
ret:
  return r;
}

static int accounts_with_cursor(struct Array *restrict result,
                                const char *restrict const cursor) {
  char url[URL_MAX_LENGTH + 1] = {0};
  struct wcjson_document doc = WCJSON_DOCUMENT_INITIALIZER;
  char errbuf[WCJSON_BODY_MAX + 1] = {0};
  int r;
  WCJSON_DECLARE_BOOL_ITEM(has_next)
  WCJSON_DECLARE_STRING_ITEM(cursor)

  if (cursor)
    r = snprintf(url, sizeof(url), "%s%s?limit=%d&cursor=%s", coinbase_rest_uri,
                 coinbase_accounts_path, 128, cursor);

  else
    r = snprintf(url, sizeof(url), "%s%s?limit=%d", coinbase_rest_uri,
                 coinbase_accounts_path, 128);

  if (r < 0 || (size_t)r >= sizeof(url))
    panic();

  r = -1;
  if (http_req(&doc, url, coinbase_accounts_path, NULL, 0) == 0) {
    r = parse_accounts(result, &doc);

    if (r != 0)
      goto ret;

    r = -1;
    WCJSON_BOOL_ITEM(&doc, doc.values, has_next, 8, errbuf, ret)

    if (j_has_next->is_true) {
      WCJSON_STRING_ITEM(&doc, doc.values, cursor, 6, errbuf, ret)
      r = accounts_with_cursor(result, j_cursor->mbstring);

      if (r != 0)
        goto ret;
    }

    r = 0;
  }

ret:
  heap_free(doc.values);
  heap_free(doc.strings);
  heap_free(doc.mbstrings);
  heap_free(doc.esc);
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

    Array_shrink(accounts);

    accounts_by_id = Map_new(StringMapOps, Array_size(accounts));
    accounts_by_currency = Map_new(StringMapOps, Array_size(accounts));

    items = Array_items(accounts);
    for (size_t i = Array_size(accounts); i-- > 0;) {
      if (Map_put(accounts_by_currency, ((struct Account *)items[i])->c_id,
                  items[i]) != NULL)
        fatal("Account currency uniqueness constraint: %s",
              String_chars(((struct Account *)items[i])->c_id));

      if (Map_put(accounts_by_id, ((struct Account *)items[i])->id, items[i]) !=
          NULL)
        fatal("Account id uniqueness constraint: %s",
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
  char path[URL_MAX_LENGTH + 1] = {0};
  char url[URL_MAX_LENGTH + 1] = {0};
  char errbuf[WCJSON_BODY_MAX + 1] = {0};
  struct Account *restrict a = NULL;
  struct wcjson_document doc = WCJSON_DOCUMENT_INITIALIZER;
  WCJSON_DECLARE_OBJECT_ITEM(account)
  int r = snprintf(path, sizeof(path), coinbase_account_path, String_chars(id));

  if (r < 0 || (size_t)r >= sizeof(path))
    panic();

  r = snprintf(url, sizeof(url), "%s%s", coinbase_rest_uri, path);
  if (r < 0 || (size_t)r >= sizeof(url))
    panic();

  if (http_req(&doc, url, path, NULL, 0) == 0) {
    WCJSON_OBJECT_ITEM(&doc, doc.values, account, 7, errbuf, ret)
    a = parse_account(&doc, j_account);
  }

ret:
  heap_free(doc.values);
  heap_free(doc.strings);
  heap_free(doc.mbstrings);
  heap_free(doc.esc);
  return a;
}

static struct Order *
parse_order(const struct wcjson_document *restrict const doc,
            const struct wcjson_value *restrict const order) {
  struct Order *restrict o = NULL;
  char errbuf[WCJSON_BODY_MAX + 1] = {0};
  WCJSON_DECLARE_STRING_ITEM(order_id)
  WCJSON_DECLARE_MARKET_ITEM(product_id)
  WCJSON_DECLARE_NUMERIC_ITEM(filled_size)
  WCJSON_DECLARE_NUMERIC_ITEM(filled_value)
  WCJSON_DECLARE_NUMERIC_ITEM(total_fees)
  WCJSON_DECLARE_ISO8601_NANOS_ITEM(created_time)
  WCJSON_DECLARE_STRING_ITEM(status)
  WCJSON_DECLARE_BOOL_ITEM(settled)
  WCJSON_DECLARE_OBJECT_ITEM(order_configuration)
  WCJSON_DECLARE_OBJECT_ITEM(limit_limit_gtc)
  WCJSON_DECLARE_NUMERIC_ITEM(base_size)
  WCJSON_DECLARE_NUMERIC_ITEM(limit_price)
  WCJSON_DECLARE_STRING_ITEM_OPT(reject_message)
  WCJSON_DECLARE_STRING_ITEM_OPT(cancel_message)
  WCJSON_DECLARE_ISO8601_NANOS_ITEM_OPT(last_fill_time)
  // XXX: size_inclusive_of_fees boolean
  // XXX: size_in_quote boolean
  // XXX:   Not available at product level and via user channel events.
  WCJSON_DECLARE_BOOL_ITEM_OPT(size_inclusive_of_fees);
  WCJSON_DECLARE_BOOL_ITEM_OPT(size_in_quote)

  WCJSON_STRING_ITEM(doc, order, order_id, 8, errbuf, ret)
  WCJSON_MARKET_ITEM(doc, order, product_id, 10, errbuf, ret)
  WCJSON_NUMERIC_ITEM(doc, order, filled_size, 11, errbuf, ret)
  WCJSON_NUMERIC_ITEM(doc, order, filled_value, 12, errbuf, ret)
  WCJSON_NUMERIC_ITEM(doc, order, total_fees, 10, errbuf, ret)
  WCJSON_ISO8601_NANOS_ITEM(doc, order, created_time, 12, errbuf, ret)
  WCJSON_STRING_ITEM(doc, order, status, 6, errbuf, ret)
  WCJSON_BOOL_ITEM(doc, order, settled, 7, errbuf, ret)
  WCJSON_BOOL_ITEM_OPT(doc, order, size_inclusive_of_fees, 22, errbuf, ret)
  WCJSON_BOOL_ITEM_OPT(doc, order, size_in_quote, 13, errbuf, ret)
  WCJSON_OBJECT_ITEM(doc, order, order_configuration, 19, errbuf, ret)
  WCJSON_OBJECT_ITEM(doc, j_order_configuration, limit_limit_gtc, 15, errbuf,
                     ret)
  WCJSON_NUMERIC_ITEM(doc, j_limit_limit_gtc, base_size, 9, errbuf, ret)
  WCJSON_NUMERIC_ITEM(doc, j_limit_limit_gtc, limit_price, 11, errbuf, ret)
  WCJSON_STRING_ITEM_OPT(doc, order, reject_message, 14, errbuf, ret)
  WCJSON_STRING_ITEM_OPT(doc, order, cancel_message, 14, errbuf, ret)
  WCJSON_ISO8601_NANOS_ITEM_OPT(doc, order, last_fill_time, 14, errbuf, ret)

  struct String *restrict msg = NULL;

  if (j_reject_message_exists) {
    msg = String_cnew(j_reject_message->mbstring);
    if (String_length(msg) == 0) {
      String_delete(msg);
      msg = NULL;
    }
  }

  if (msg == NULL && j_cancel_message_exists) {
    msg = String_cnew(j_cancel_message->mbstring);
    if (String_length(msg) == 0) {
      String_delete(msg);
      msg = NULL;
    }
  }

  o = Order_new();
  o->id = String_cnew(j_order_id->mbstring);
  o->m_id = String_copy(j_product_id_m->id);
  o->settled = j_settled->is_true;
  o->status = order_status(j_status->mbstring);
  o->cnanos = j_created_time_nanos;
  o->dnanos = j_last_fill_time_nanos;
  o->b_ordered = j_base_size_num;
  o->p_ordered = j_limit_price_num;
  o->b_filled = j_filled_size_num;
  o->q_filled = j_filled_value_num;
  o->q_fees = j_total_fees_num;
  o->msg = msg;

  if (o->status == ORDER_STATUS_UNKNOWN)
    werr("coinbase: %s: status unsupported: %s\n", String_chars(o->id),
         wcjsondoc_string(errbuf, sizeof(errbuf), doc, order, NULL));

  if (j_size_inclusive_of_fees_exists && j_size_inclusive_of_fees->is_true)
    werr("coinbase: %s: size_inclusive_of_fees unsupported: %s\n",
         String_chars(o->id),
         wcjsondoc_string(errbuf, sizeof(errbuf), doc, order, NULL));

  if (j_size_in_quote_exists && j_size_in_quote->is_true)
    werr("coinbase: %s: size_in_quote unsupported: %s\n", String_chars(o->id),
         wcjsondoc_string(errbuf, sizeof(errbuf), doc, order, NULL));

ret:
  if (j_product_id_m != NULL)
    mutex_unlock(j_product_id_m->mtx);

  return o;
}

static struct Order *coinbase_order(const struct String *restrict const id) {
  char path[URL_MAX_LENGTH + 1] = {0};
  char url[URL_MAX_LENGTH + 1] = {0};
  char errbuf[WCJSON_BODY_MAX + 1] = {0};
  struct Order *restrict o = NULL;
  struct wcjson_document doc = WCJSON_DOCUMENT_INITIALIZER;
  WCJSON_DECLARE_OBJECT_ITEM(order)
  int r = snprintf(path, sizeof(path), coinbase_order_path, String_chars(id));

  if (r < 0 || (size_t)r >= sizeof(path))
    panic();

  r = snprintf(url, sizeof(url), "%s%s", coinbase_rest_uri, path);
  if (r < 0 || (size_t)r >= sizeof(url))
    panic();

  if (http_req(&doc, url, path, NULL, 0) == 0) {
    WCJSON_OBJECT_ITEM(&doc, doc.values, order, 5, errbuf, ret)
    o = parse_order(&doc, j_order);
  }

ret:
  heap_free(doc.values);
  heap_free(doc.strings);
  heap_free(doc.mbstrings);
  heap_free(doc.esc);
  return o;
}

static bool coinbase_order_cancel(const struct String *restrict const id) {
  bool ret = false;
  bool found = false;
  char url[URL_MAX_LENGTH + 1] = {0};
  char errbuf[WCJSON_BODY_MAX + 1] = {0};
  WCJSON_DECLARE_ARRAY_ITEM(results)
  WCJSON_DECLARE_STRING_ITEM(order_id)
  WCJSON_DECLARE_BOOL_ITEM(success)
  struct wcjson_document doc = WCJSON_DOCUMENT_INITIALIZER;
  struct wcjson_document *restrict const b_doc = wcjsondoc_new();
  struct wcjson_value *restrict const body = wcjson_object(b_doc);
  struct wcjson_value *restrict const ids = wcjson_array(b_doc);
  struct wcjson_value *restrict const v_id =
      wcjson_string(b_doc, String_chars(id));

  wcjson_array_add(b_doc, ids, v_id);
  wcjson_object_add(b_doc, body, L"order_ids", 9, ids);

  struct wcjson wcjson = WCJSON_INITIALIZER;
  if (wcjsondocstrings(&wcjson, b_doc) < 0)
    panic();

  char mbbody[WCJSON_BODY_MAX + 1] = {0};
  size_t mb_len = 0;
  wcjsondoc_string(mbbody, sizeof(mbbody), b_doc, b_doc->values, &mb_len);

  int r = snprintf(url, sizeof(url), "%s%s", coinbase_rest_uri,
                   coinbase_order_cancel_path);
  if (r < 0 || (size_t)r >= sizeof(url))
    panic();

  if (http_req(&doc, url, coinbase_order_cancel_path, mbbody, mb_len) == 0) {
    WCJSON_ARRAY_ITEM(&doc, doc.values, results, 7, errbuf, ret)

    const struct wcjson_value *restrict j_result = NULL;
    wcjson_value_foreach(j_result, &doc, j_results) {
      WCJSON_STRING_ITEM(&doc, j_result, order_id, 8, errbuf, ret)
      WCJSON_BOOL_ITEM(&doc, j_result, success, 7, errbuf, ret)

      if (!strcmp(String_chars(id), j_order_id->mbstring)) {
        found = true;
        ret = j_success->is_true;
        break;
      }
    }

    if (!found) {
      werr("coinbase: %s: Order id not available: %s\n", String_chars(id),
           wcjsondoc_string(errbuf, sizeof(errbuf), &doc, doc.values, NULL));
    }

    if (!ret)
      werr("coinbase: %s: %s\n", String_chars(id),
           wcjsondoc_string(errbuf, sizeof(errbuf), &doc, doc.values, NULL));
  }

ret:
  heap_free(doc.values);
  heap_free(doc.strings);
  heap_free(doc.mbstrings);
  heap_free(doc.esc);
  wcjsondoc_delete(b_doc);
  return ret;
}

static void order_create_body(char *restrict const mbbody, size_t mbbody_nitems,
                              struct wcjson_document *restrict const doc,
                              const char *restrict const m_sym,
                              const char *restrict const side,
                              const char *restrict const base_amount,
                              const char *restrict const price,
                              size_t *restrict mb_len) {
  char cl_id[DATABASE_UUID_MAX_LENGTH + 1] = {0};
  db_uuid(cl_id, coinbase_db);

  struct wcjson_value *restrict const body = wcjson_object(doc);
  struct wcjson_value *restrict const conf = wcjson_object(doc);
  struct wcjson_value *restrict const llgtc = wcjson_object(doc);
  struct wcjson_value *restrict const j_p_id = wcjson_string(doc, m_sym);
  struct wcjson_value *restrict const j_side = wcjson_string(doc, side);
  struct wcjson_value *restrict const j_base = wcjson_string(doc, base_amount);
  struct wcjson_value *restrict const j_price = wcjson_string(doc, price);
  struct wcjson_value *restrict const j_post_only = wcjson_bool(doc, false);
  struct wcjson_value *restrict const j_cl_id = wcjson_string(doc, cl_id);

  wcjson_object_add(doc, body, L"order_configuration", 19, conf);
  wcjson_object_add(doc, conf, L"limit_limit_gtc", 15, llgtc);
  wcjson_object_add(doc, body, L"client_order_id", 15, j_cl_id);
  wcjson_object_add(doc, body, L"product_id", 10, j_p_id);
  wcjson_object_add(doc, body, L"side", 4, j_side);
  wcjson_object_add(doc, llgtc, L"post_only", 9, j_post_only);
  wcjson_object_add(doc, llgtc, L"base_size", 9, j_base);
  wcjson_object_add(doc, llgtc, L"limit_price", 11, j_price);

  wcjsondoc_string(mbbody, mbbody_nitems, doc, doc->values, mb_len);
}

static struct String *coinbase_order_post(
    const char *restrict const m_sym, const char *restrict const side,
    const char *restrict const base_amount, const char *restrict const price) {
  struct String *restrict o_id = NULL;
  char url[URL_MAX_LENGTH + 1] = {0};
  char body[WCJSON_BODY_MAX + 1] = {0};
  char errbuf[WCJSON_BODY_MAX + 1] = {0};
  size_t b_len = 0;
  struct wcjson_document doc = WCJSON_DOCUMENT_INITIALIZER;
  struct wcjson_document *restrict const b_doc = wcjsondoc_new();
  WCJSON_DECLARE_BOOL_ITEM(success)
  WCJSON_DECLARE_OBJECT_ITEM(success_response)
  WCJSON_DECLARE_STRING_ITEM(order_id)

  order_create_body(body, sizeof(body), b_doc, m_sym, side, base_amount, price,
                    &b_len);

  int r = snprintf(url, sizeof(url), "%s%s", coinbase_rest_uri,
                   coinbase_order_create_path);
  if (r < 0 || (size_t)r >= sizeof(url))
    panic();

  if (http_req(&doc, url, coinbase_order_create_path, body, b_len) == 0) {
    WCJSON_BOOL_ITEM(&doc, doc.values, success, 7, errbuf, ret)

    if (j_success->is_true) {
      WCJSON_OBJECT_ITEM(&doc, doc.values, success_response, 16, errbuf, ret)
      WCJSON_STRING_ITEM(&doc, j_success_response, order_id, 8, errbuf, ret)

      o_id = String_cnew(j_order_id->mbstring);

      mutex_lock(&pricing_mutex);
      Pricing_delete(pricing);
      pricing = NULL;
      mutex_unlock(&pricing_mutex);
    } else
      werr("coinbase: %s\n",
           wcjsondoc_string(errbuf, sizeof(errbuf), &doc, doc.values, NULL));
  }
ret:
  heap_free(doc.values);
  heap_free(doc.strings);
  heap_free(doc.mbstrings);
  heap_free(doc.esc);
  wcjsondoc_delete(b_doc);
  return o_id;
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
  char errbuf[WCJSON_BODY_MAX + 1] = {0};
  WCJSON_DECLARE_OBJECT_ITEM(fee_tier)
  WCJSON_DECLARE_STRING_ITEM(pricing_tier)
  WCJSON_DECLARE_NUMERIC_ITEM(taker_fee_rate)
  WCJSON_DECLARE_NUMERIC_ITEM(maker_fee_rate)

  WCJSON_OBJECT_ITEM(doc, pr, fee_tier, 8, errbuf, fallback)
  WCJSON_STRING_ITEM(doc, j_fee_tier, pricing_tier, 12, errbuf, fallback)
  WCJSON_NUMERIC_ITEM(doc, j_fee_tier, taker_fee_rate, 14, errbuf, fallback)
  WCJSON_NUMERIC_ITEM(doc, j_fee_tier, maker_fee_rate, 14, errbuf, fallback)

  struct Numeric *restrict const efr =
      Numeric_copy(Numeric_cmp(j_taker_fee_rate_num, j_maker_fee_rate_num) > 0
                       ? j_taker_fee_rate_num
                       : j_maker_fee_rate_num);

  struct Pricing *restrict parsed = Pricing_new();
  parsed->nm = String_cnew(j_pricing_tier->mbstring);
  parsed->tf_pc = Numeric_mul(j_taker_fee_rate_num, hundred);
  parsed->mf_pc = Numeric_mul(j_maker_fee_rate_num, hundred);
  parsed->ef_pc = Numeric_mul(efr, hundred);

  Numeric_delete(j_taker_fee_rate_num);
  Numeric_delete(j_maker_fee_rate_num);
  Numeric_delete(efr);

  return parsed;
fallback:
  parsed = Pricing_new();
  parsed->nm = String_cnew("fallback");
  parsed->tf_pc = Numeric_from_char("1.2");
  parsed->mf_pc = Numeric_from_char("1.2");
  parsed->ef_pc = Numeric_from_char("1.2");
  return parsed;
}

static struct Pricing *coinbase_pricing(void) {
  char url[URL_MAX_LENGTH + 1] = {0};
  struct wcjson_document doc = WCJSON_DOCUMENT_INITIALIZER;

  mutex_lock(&pricing_mutex);

  if (pricing != NULL)
    goto ret;

  int r = snprintf(url, sizeof(url), "%s%s?product_type=SPOT",
                   coinbase_rest_uri, coinbase_fees_path);

  if (r < 0 || (size_t)r >= sizeof(url))
    panic();

  if (http_req(&doc, url, coinbase_fees_path, NULL, 0) == 0)
    pricing = parse_pricing(&doc, doc.values);

ret:
  pricing->mtx = &pricing_mutex;
  heap_free(doc.values);
  heap_free(doc.strings);
  heap_free(doc.mbstrings);
  heap_free(doc.esc);
  return pricing;
}
