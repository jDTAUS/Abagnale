/* $SchulteIT: exchange-coinbase.c 15282 2025-11-05 22:54:21Z schulte $ */
// clang-format off
/* $JDTAUS$ */
// clang-format on

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

#include <inttypes.h>
#include <jwt.h>
#include <wcjson-document.h>
#include <wcjson.h>

#define SIGNATURE_MAX_LENGTH (size_t)16384
#define TIMESTAMP_MAX_LENGTH (size_t)64
#define HEADER_MAX_LENGTH (size_t)1024
#define URL_MAX_LENGTH (size_t)512
#define PRODUCT_NAME_MAX_LENGTH (size_t)30
#define HTTP_RESPONSE_MAX_WCHARS (size_t)5242880

#define WCJSON_MAX_VALUES 65536         // 5MB
#define WCJSON_MAX_CHARACTERS 786432    // 3MB
#define WCJSON_MAX_MBCHARACTERS 1048576 // 1MB
#define WCJSON_MAX_ESCCHARACTERS 512    // 2kb
#define WCJSON_STRLEN_MAX 8192
#define WCJSON_BODY_MAX 32767

#define COINBASE_UUID "74cc13c5-4835-491b-95f2-6af672ad141a"
#define COINBASE_DBCON "coinbase"

#ifndef ABAG_COINBASE_ADVANCED_API_WEBSOCKET_HOST
#define ABAG_COINBASE_ADVANCED_API_WEBSOCKET_HOST                              \
  "advanced-trade-ws.coinbase.com"
#endif

#ifndef ABAG_COINBASE_WEBSOCKET_URL
#define ABAG_COINBASE_WEBSOCKET_URL                                            \
  "wss://" ABAG_COINBASE_ADVANCED_API_WEBSOCKET_HOST
#endif

#ifndef ABAG_COINBASE_ADVANCED_API_HOST
#define ABAG_COINBASE_ADVANCED_API_HOST "api.coinbase.com"
#endif

#ifndef ABAG_COINBASE_ADVANCED_API_URI
#define ABAG_COINBASE_ADVANCED_API_URI                                         \
  "https://" ABAG_COINBASE_ADVANCED_API_HOST
#endif

#ifndef ABAG_COINBASE_PRODUCTS_PATH
#define ABAG_COINBASE_PRODUCTS_PATH "/api/v3/brokerage/products"
#endif

#ifndef ABAG_COINBASE_PRODUCTS_RESOURCE_URL
#define ABAG_COINBASE_PRODUCTS_RESOURCE_URL                                    \
  ABAG_COINBASE_ADVANCED_API_URI                                               \
  ABAG_COINBASE_PRODUCTS_PATH
#endif

#ifndef ABAG_COINBASE_ACCOUNTS_PATH
#define ABAG_COINBASE_ACCOUNTS_PATH "/api/v3/brokerage/accounts"
#endif

#ifndef ABAG_COINBASE_ACCOUNTS_RESOURCE_URL
#define ABAG_COINBASE_ACCOUNTS_RESOURCE_URL                                    \
  ABAG_COINBASE_ADVANCED_API_URI                                               \
  ABAG_COINBASE_ACCOUNTS_PATH
#endif

#ifndef ABAG_COINBASE_ACCOUNTS_RESOURCE_LIMIT
#define ABAG_COINBASE_ACCOUNTS_RESOURCE_LIMIT 200
#endif

#ifndef ABAG_COINBASE_FEES_PATH
#define ABAG_COINBASE_FEES_PATH "/api/v3/brokerage/transaction_summary"
#endif

#ifndef ABAG_COINBASE_FEES_RESOURCE_URL
#define ABAG_COINBASE_FEES_RESOURCE_URL                                        \
  ABAG_COINBASE_ADVANCED_API_URI                                               \
  ABAG_COINBASE_FEES_PATH
#endif

#ifndef ABAG_COINBASE_ACCOUNT_PATH
#define ABAG_COINBASE_ACCOUNT_PATH "/api/v3/brokerage/accounts/%s"
#endif

#ifndef ABAG_COINBASE_ORDER_PATH
#define ABAG_COINBASE_ORDER_PATH "/api/v3/brokerage/orders/historical/%s"
#endif

#ifndef ABAG_COINBASE_ORDER_CANCEL_PATH
#define ABAG_COINBASE_ORDER_CANCEL_PATH "/api/v3/brokerage/orders/batch_cancel"
#endif

#ifndef ABAG_COINBASE_ORDER_CANCEL_RESOURCE_URL
#define ABAG_COINBASE_ORDER_CANCEL_RESOURCE_URL                                \
  ABAG_COINBASE_ADVANCED_API_URI                                               \
  ABAG_COINBASE_ORDER_CANCEL_PATH
#endif

#ifndef ABAG_COINBASE_ORDER_CREATE_PATH
#define ABAG_COINBASE_ORDER_CREATE_PATH "/api/v3/brokerage/orders"
#endif

#ifndef ABAG_COINBASE_ORDER_CREATE_RESOURCE_URL
#define ABAG_COINBASE_ORDER_CREATE_RESOURCE_URL                                \
  ABAG_COINBASE_ADVANCED_API_URI                                               \
  ABAG_COINBASE_ORDER_CREATE_PATH
#endif

#ifndef API_RATE_REQUESTS_PER_SECOND
#define API_RATE_REQUESTS_PER_SECOND 30
#endif

#ifndef WEBSOCKET_RECONNECT_TIMEOUT_SECONDS
#define WEBSOCKET_RECONNECT_TIMEOUT_SECONDS 3
#endif

#ifndef MG_POLL_WAIT_MILLIS
#define MG_POLL_WAIT_MILLIS 30000L
#endif

#ifndef HTTP_TIMEOUT_MILLIS
#define HTTP_TIMEOUT_MILLIS 60000L
#endif

#ifndef WEBSOCKET_STALL_MILLIS
#define WEBSOCKET_STALL_MILLIS 3600000L
#endif

#define WCJSON_DECLARE_STRING_ITEM(_item)                                      \
  struct wcjson_value *restrict j_##_item = NULL;

#define WCJSON_STRING_ITEM(_doc, _val, _item, _len, _errbuf, _ret)             \
  j_##_item = wcjson_object_get(_doc, _val, L## #_item, _len);                 \
  if (j_##_item == NULL || !j_##_item->is_string) {                            \
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
  struct Numeric *restrict j_##_item##_nanos = Numeric_new();

#define WCJSON_ISO8601_NANOS_ITEM_OPT(_doc, _val, _item, _len, _errbuf, _ret)  \
  WCJSON_STRING_ITEM_OPT(_doc, _val, _item, _len, _errbuf, _ret)               \
  j_##_item##_nanos = Numeric_new();                                           \
  if (j_##_item##_exists &&                                                    \
      !nanos_from_iso8601(j_##_item->mbstring, j_##_item->mb_len,              \
                          j_##_item##_nanos)) {                                \
    werr("coinbase: No '" #_item "' ISO8601 item; %s\n",                       \
         wcjsondoc_string(_errbuf, sizeof(_errbuf), _doc, _val, NULL));        \
    Numeric_delete(j_##_item##_nanos);                                         \
    j_##_item##_nanos = NULL;                                                  \
    j_##_item##_exists = false;                                                \
    goto _ret;                                                                 \
  }

#define WCJSON_DECLARE_PRODUCT_ITEM(_item)                                     \
  WCJSON_DECLARE_STRING_ITEM(_item)                                            \
  struct String *restrict j_##_item##_str = NULL;                              \
  struct Product *restrict j_##_item##_p = NULL;

#define WCJSON_PRODUCT_ITEM(_doc, _val, _item, _len, _errbuf, _ret)            \
  WCJSON_STRING_ITEM(_doc, _val, _item, _len, _errbuf, _ret)                   \
  j_##_item##_str = String_cnew(j_##_item->mbstring);                          \
  j_##_item##_p = coinbase_product_name(j_##_item##_str);                      \
  String_delete(j_##_item##_str);                                              \
  j_##_item##_str = NULL;                                                      \
  if (j_##_item##_p == NULL) {                                                 \
    werr("coinbase: Product '%s' not found: %s\n", j_##_item->mbstring,        \
         wcjsondoc_string(_errbuf, sizeof(_errbuf), _doc, _val, NULL));        \
    goto _ret;                                                                 \
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
  const enum product_type type;
} product_type_map[] = {
    {"UNKNOWN_PRODUCT_TYPE", PRODUCT_TYPE_UNKNOWN},
    {"SPOT", PRODUCT_TYPE_SPOT},
    {"FUTURE", PRODUCT_TYPE_FUTURE},
};

static const struct {
  const char *restrict json;
  const enum product_status status;
} product_status_map[] = {
    {"online", PRODUCT_STATUS_ONLINE},
    {"delisted", PRODUCT_STATUS_DELISTED},
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

static const struct ExchangeConfig *coinbase_cnf;

static struct Array *products;
static struct Map *products_by_name;
static struct Map *products_by_id;
static bool products_reload;

static struct Array *accounts;
static struct Map *accounts_by_id;
static struct Map *accounts_by_currency;
static bool accounts_reload;

static struct Pricing *pricing;
static mtx_t pricing_mutex;

static _Atomic bool running;
static struct Queue *orders;
static struct Queue *samples;
static thrd_t mg_mgr_worker;
static struct wcjson_document *ws_doc;
static struct timespec api_request_rate = {
    .tv_sec = 0,
    .tv_nsec = 1000000000L / API_RATE_REQUESTS_PER_SECOND,
};
static struct timespec api_reconnect_rate = {
    .tv_sec = WEBSOCKET_RECONNECT_TIMEOUT_SECONDS,
    .tv_nsec = 0,
};
static void coinbase_init(void);
static void coinbase_configure(const struct ExchangeConfig *restrict const);
static void coinbase_destroy(void);
static void coinbase_start(void);
static void coinbase_stop(void);
static struct Order *coinbase_order_await(void);
static struct Sample *coinbase_sample_await(void);
static struct Pricing *coinbase_pricing(void);
static struct Array *coinbase_products(void);
static struct Product *coinbase_product(const struct String *restrict const);
static struct Product *
coinbase_product_name(const struct String *restrict const);
static struct Array *coinbase_accounts(void);
static struct Account *
coinbase_account_currency(const struct String *restrict const);
static struct Account *coinbase_account(const struct String *restrict const);
static struct Order *coinbase_order(const struct String *restrict const);
static bool coinbase_cancel(const struct String *restrict const);
static struct String *coinbase_buy(const struct String *restrict const,
                                   const char *restrict const,
                                   const char *restrict const);
static struct String *coinbase_sell(const struct String *restrict const,
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
    .sample_await = coinbase_sample_await,
    .pricing = coinbase_pricing,
    .products = coinbase_products,
    .product = coinbase_product,
    .accounts = coinbase_accounts,
    .account = coinbase_account,
    .order = coinbase_order,
    .cancel = coinbase_cancel,
    .buy = coinbase_buy,
    .sell = coinbase_sell,
};

static struct ws_channel {
  const char *restrict const name;
  const wchar_t *restrict const items;
  const size_t items_len;
  uint64_t last_message;
  bool debug;
  bool reconnect;
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
  for (int i = nitems(order_status_map); i > 0; i--)
    if (!strcmp(order_status_map[i - 1].json, status))
      return order_status_map[i - 1].status;

  return ORDER_STATUS_UNKNOWN;
}

static enum product_type product_type(const char *restrict const type) {
  for (int i = nitems(product_type_map); i > 0; i--)
    if (!strcmp(product_type_map[i - 1].json, type))
      return product_type_map[i - 1].type;

  return PRODUCT_TYPE_UNKNOWN;
}

static enum product_status product_status(const char *restrict const status) {
  for (int i = nitems(product_status_map); i > 0; i--)
    if (!strcmp(product_status_map[i - 1].json, status))
      return product_status_map[i - 1].status;

  return PRODUCT_STATUS_UNKNOWN;
}

static enum account_type account_type(const char *restrict const type) {
  for (int i = nitems(account_type_map); i > 0; i--)
    if (!strcmp(account_type_map[i - 1].json, type))
      return account_type_map[i - 1].type;

  return ACCOUNT_TYPE_UNSPECIFIED;
}

static struct ws_channel *ws_channel(const char *restrict const name) {
  for (size_t i = nitems(ws_channels); i > 0; i--)
    if (!strcmp(ws_channels[i - 1].name, name))
      return &ws_channels[i - 1];

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
    werr("%s: %d: %s: Invalid wcjson status\n", __FILE__, __LINE__, __func__);
    fatal();
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
  size_t wc_len = dst_nitems;

  if (wcjsondocsprint(wc, &wc_len, doc, value) < 0) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strerror(errno));
    fatal();
  }

  size_t mb_len = wcstombs(dst, wc, dst_nitems);
  if (mb_len == (size_t)-1) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strerror(errno));
    fatal();
  }

  if (mb_len == dst_nitems) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strerror(ERANGE));
    fatal();
  }

  if (s_len != NULL)
    *s_len = mb_len;

  return dst;
}

static struct wcjson_value *
wcjson_string(struct wcjson_document *restrict const doc,
              const char *restrict const s) {
  wchar_t wc[WCJSON_STRLEN_MAX] = {0};
  const size_t wc_len = mbstowcs(wc, s, nitems(wc));

  if (wc_len == (size_t)-1) {
    werr("%s: %d: %s: %s: %s\n", __FILE__, __LINE__, __func__, s,
         strerror(errno));
    fatal();
  }
  if (wc_len == nitems(wc)) {
    werr("%s: %d: %s: %s: %s\n", __FILE__, __LINE__, __func__, s,
         strerror(ERANGE));
    fatal();
  }

  const wchar_t *restrict const doc_s = wcjson_document_string(doc, wc, wc_len);

  if (doc_s == NULL) {
    werr("%s: %d: %s: %s: %s\n", __FILE__, __LINE__, __func__, s,
         strerror(errno));
    fatal();
  }

  struct wcjson_value *restrict const j_s =
      wcjson_value_string(doc, doc_s, wc_len);

  if (j_s == NULL) {
    werr("%s: %d: %s: %s: %s\n", __FILE__, __LINE__, __func__, s,
         strerror(errno));
    fatal();
  }

  return j_s;
}

static struct wcjson_value *
wcjson_object(struct wcjson_document *restrict const doc) {
  struct wcjson_value *restrict const o = wcjson_value_object(doc);
  if (o == NULL) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strerror(errno));
    fatal();
  }
  return o;
}

static struct wcjson_value *
wcjson_array(struct wcjson_document *restrict const doc) {
  struct wcjson_value *restrict const a = wcjson_value_array(doc);
  if (a == NULL) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strerror(errno));
    fatal();
  }
  return a;
}

static struct wcjson_value *
wcjson_bool(struct wcjson_document *restrict const doc, const bool flag) {
  struct wcjson_value *restrict const v = wcjson_value_bool(doc, flag);
  if (v == NULL) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strerror(errno));
    fatal();
  }
  return v;
}

static void wcjson_array_add(struct wcjson_document *restrict const doc,
                             struct wcjson_value *restrict const arr,
                             struct wcjson_value *restrict const value) {
  if (wcjson_array_add_tail(doc, arr, value) < 0) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strerror(errno));
    fatal();
  }
}

static void wcjson_object_add(struct wcjson_document *restrict const doc,
                              struct wcjson_value *restrict const obj,
                              const wchar_t *key, const size_t key_len,
                              struct wcjson_value *restrict const value) {
  if (wcjson_object_add_tail(doc, obj, key, key_len, value) < 0) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strerror(errno));
    fatal();
  }
}

static char *jwt_encode_cdp(const char *restrict const uri) {
  jwt_t *jwt = NULL;
  int r = 0;

  r = jwt_new(&jwt);
  if (r != 0) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strerror(r));
    fatal();
  }

  r = jwt_add_grant(jwt, "sub", String_chars(coinbase_cnf->jwt_kid));
  if (r != 0) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strerror(r));
    fatal();
  }

  r = jwt_add_grant(jwt, "iss", "cdp");
  if (r != 0) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strerror(r));
    fatal();
  }

  const time_t now = time(NULL);

  r = jwt_add_grant_int(jwt, "nbf", now - 1);
  if (r != 0) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strerror(r));
    fatal();
  }

  r = jwt_add_grant_int(jwt, "exp", now + 120);
  if (r != 0) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strerror(r));
    fatal();
  }

  if (uri != NULL) {
    r = jwt_add_grant(jwt, "uri", uri);
    if (r != 0) {
      werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strerror(r));
      fatal();
    }
  }

  r = jwt_add_header(jwt, "kid", String_chars(coinbase_cnf->jwt_kid));
  if (r != 0) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strerror(r));
    fatal();
  }

  r = jwt_set_alg(jwt, JWT_ALG_ES256,
                  (unsigned char *)String_chars(coinbase_cnf->jwt_key),
                  String_length(coinbase_cnf->jwt_key));
  if (r != 0) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strerror(r));
    fatal();
  }

  char *restrict const encoded = jwt_encode_str(jwt);
  if (encoded == NULL) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strerror(errno));
    fatal();
  }

  jwt_free(jwt);
  return encoded;
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
    mg_mgr_poll(mgr, MG_POLL_WAIT_MILLIS);

  mg_mgr_poll(mgr, MG_POLL_WAIT_MILLIS);
  mg_mgr_free(mgr);
  thread_exit(EXIT_SUCCESS);
}

static void ws_ticker_update(const struct wcjson_document *restrict const doc,
                             const struct wcjson_value *restrict const ticker,
                             const struct Numeric *restrict const nanos) {
  char errbuf[WCJSON_BODY_MAX + 1] = {0};
  WCJSON_DECLARE_PRODUCT_ITEM(product_id)
  WCJSON_DECLARE_NUMERIC_ITEM(price)

  WCJSON_PRODUCT_ITEM(doc, ticker, product_id, 10, errbuf, ret)
  WCJSON_NUMERIC_ITEM(doc, ticker, price, 5, errbuf, ret)

  if (Numeric_cmp(j_price_num, zero) > 0) {
    struct Sample *restrict const s = Sample_new();
    s->p_id = String_copy(j_product_id_p->id);
    s->nanos = Numeric_copy(nanos);
    s->price = j_price_num;

    Queue_enqueue(samples, s);
  } else
    Numeric_delete(j_price_num);

ret:
  if (j_product_id_p != NULL)
    mutex_unlock(j_product_id_p->mtx);

  return;
}

static void ws_status_update(const struct wcjson_document *restrict const doc,
                             const struct wcjson_value *restrict const product,
                             const struct Numeric *restrict const nanos) {
  for (size_t i = nitems(ws_channels); i > 0; i--)
    ws_channels[i - 1].reconnect = true;
}

static void ws_user_update(const struct wcjson_document *restrict const doc,
                           const struct wcjson_value *restrict const order,
                           const struct Numeric *restrict const nanos) {
  struct Order *restrict o = NULL;
  char errbuf[WCJSON_BODY_MAX + 1] = {0};
  WCJSON_DECLARE_STRING_ITEM(order_id)
  WCJSON_DECLARE_PRODUCT_ITEM(product_id)
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
  WCJSON_PRODUCT_ITEM(doc, order, product_id, 10, errbuf, ret)
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
  o->p_id = String_copy(j_product_id_p->id);
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
    werr("coinbase: user: %s: Order status unknown\n", j_status->mbstring);

  if ((o->status == ORDER_STATUS_CANCELLED ||
       o->status == ORDER_STATUS_FAILED || o->status == ORDER_STATUS_EXPIRED) &&
      Numeric_cmp(j_outstanding_hold_amount_num, zero) != 0) {
    Order_delete(o);
    goto ret;
  }

  Queue_enqueue(orders, o);
ret:
  if (j_product_id_p != NULL)
    mutex_unlock(j_product_id_p->mtx);

  Numeric_delete(j_leaves_quantity_num);
  Numeric_delete(j_outstanding_hold_amount_num);
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

  if (msg->data.len > WCJSON_BODY_MAX) {
    werr("%s: %d: %s: %s: %.*s\n", __FILE__, __LINE__, __func__,
         strerror(ERANGE), (int)msg->data.len, msg->data.buf);
    fatal();
  }

  wchar_t wcmsg[WCJSON_BODY_MAX + 1] = {0};
  size_t wc_len = mbstowcs(wcmsg, msg->data.buf, msg->data.len);
  if (wc_len == (size_t)-1) {
    werr("%s: %d: %s: %s: %.*s\n", __FILE__, __LINE__, __func__,
         strerror(errno), (int)msg->data.len, msg->data.buf);
    fatal();
  }

  if (wcjsondocvalues(&wcjson, ws_doc, wcmsg, wc_len) < 0) {
    werr("%s: %d: %s: %s: %.*s\n", __FILE__, __LINE__, __func__,
         strerror(wcjsontoerrno(&wcjson)), (int)msg->data.len, msg->data.buf);
    fatal();
  }

  if (wcjsondocstrings(&wcjson, ws_doc) < 0) {
    werr("%s: %d: %s: %s: %.*s\n", __FILE__, __LINE__, __func__,
         strerror(wcjsontoerrno(&wcjson)), (int)msg->data.len, msg->data.buf);
    fatal();
  }

  if (wcjsondocmbstrings(&wcjson, ws_doc) < 0) {
    werr("%s: %d: %s: %s: %.*s\n", __FILE__, __LINE__, __func__,
         strerror(wcjsontoerrno(&wcjson)), (int)msg->data.len, msg->data.buf);
    fatal();
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
    wdebug("coinbase: %s: %.*s\n", j_channel->mbstring, (int)msg->data.len,
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

    if (!strcmp("update", j_type->mbstring)) {
      if (channel->update) {
        const struct wcjson_value *restrict j_evt_item = NULL;
        wcjson_value_foreach(j_evt_item, ws_doc, j_evt_items) {
          channel->update(ws_doc, j_evt_item, j_timestamp_nanos);
        }
      }
    } else if (!strcmp("snapshot", j_type->mbstring)) {
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
  void **items;

  struct Array *restrict const p_array = coinbase_products();
  struct wcjson_value *restrict const j_msg = wcjson_object(doc);
  struct wcjson_value *restrict const j_arr = wcjson_array(doc);

  items = Array_items(p_array);
  for (size_t i = Array_size(p_array); i > 0; i--) {
    struct wcjson_value *restrict const j_nm =
        wcjson_string(doc, String_chars(((struct Product *)items[i - 1])->nm));

    wcjson_array_add(doc, j_arr, j_nm);
  }
  Array_unlock(p_array);

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

static void ws_listener(struct mg_connection *restrict c, int ev,
                        void *restrict const ev_data) {
  struct ws_channel *restrict const channel = c->fn_data;

  switch (ev) {
  case MG_EV_CONNECT: {
#ifdef ABAG_COINBASE_DEBUG
    wdebug("coinbase: %s: %lu: MG_EV_CONNECT\n", channel->name, c->id);
#endif
    struct mg_tls_opts ws_tls_opts = {
        .ca = mg_str(""),
        .cert = mg_str(""),
        .key = mg_str(""),
        .name = mg_url_host(ABAG_COINBASE_WEBSOCKET_URL),
    };

    mg_tls_init(c, &ws_tls_opts);
    break;
  }
  case MG_EV_ERROR: {
    werr("coinbase: %s: %lu: MG_EV_ERROR: %s\n", channel->name, c->id,
         (char *)ev_data);
    c->is_closing = 1;
    break;
  }
  case MG_EV_WS_OPEN: {
#ifdef ABAG_COINBASE_DEBUG
    wdebug("coinbase: %s: %lu: MG_EV_WS_OPEN\n", channel->name, c->id);
#endif
    if (running) {
      products_reload = true;
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
        wdebug("coinbase: %s: %lu: WEBSOCKET_OP_CLOSE\n", channel->name, c->id);
#endif
        c->is_closing = 1;
      } else
        werr("coinbase: %s: %lu: %d\n", channel->name, c->id, type);

    } else
      c->is_closing = 1;

    break;
  }
  case MG_EV_CLOSE: {
#ifdef ABAG_COINBASE_DEBUG
    wdebug("coinbase: %s: %lu: MG_EV_CLOSE\n", channel->name, c->id);
#endif
    if (running) {
      struct mg_mgr *restrict const mgr = c->mgr;

      do {
        thread_sleep(&api_reconnect_rate);
        c = mg_ws_connect(mgr, ABAG_COINBASE_WEBSOCKET_URL, ws_listener,
                          channel, NULL);
        if (!c)
          werr("coinbase: %s: Failure reconnecting\n", channel->name);

      } while (!c);
    }
    break;
  }
  }

  if (channel->last_message &&
      mg_millis() - channel->last_message > WEBSOCKET_STALL_MILLIS) {
    channel->reconnect = true;
    channel->last_message = mg_millis();
    if (verbose)
      wout("coinbase: %s: No events\n", channel->name);
  }

  if (channel->reconnect) {
    channel->reconnect = false;
    c->is_closing = 1;
  }
}

static void http_listener(struct mg_connection *restrict c, int ev,
                          void *restrict const ev_data) {
  struct http_listener_ctx *restrict const http_ctx = c->fn_data;
  const char *restrict const method = http_ctx->body_len > 0 ? "POST" : "GET";

  switch (ev) {
  case MG_EV_OPEN:
    http_ctx->exp_time = mg_millis() + HTTP_TIMEOUT_MILLIS;
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
    http_ctx->success = false;
    werr("coinbase: HTTP %s %s: %lu: %s\n", method, http_ctx->url, c->id,
         (char *)ev_data);
    break;
  }
  case MG_EV_CONNECT: {
    struct mg_str host = mg_url_host(http_ctx->url);

    if (c->is_tls) {
      struct mg_tls_opts http_tls_opts = {
          .ca = mg_str(""),
          .cert = mg_str(""),
          .key = mg_str(""),
          .name = host,
      };

      mg_tls_init(c, &http_tls_opts);
    }

    char uri[URL_MAX_LENGTH] = {0};
    int r = snprintf(uri, sizeof(uri), "%s %.*s%s", method, (int)host.len,
                     host.buf, http_ctx->path);
    if (r < 0 || (size_t)r >= sizeof(uri)) {
      werr("%s: %d: %s\n", __FILE__, __LINE__, __func__);
      fatal();
    }

    char *restrict const jwt = jwt_encode_cdp(uri);

    // XXX: mg_printf does not support %zu
    mg_printf(c,
              "%s %s HTTP/1.0\r\n"
              "Authorization: Bearer %s\r\n"
              "Host: %.*s\r\n"
              "Content-Type: application/json\r\n"
              "Content-Length: %u\r\n"
              "Connection: close\r\n"
              // clang-format off
              "User-Agent: Abagnale/0 $JDTAUS$\r\n"
              // clang-format on
              "\r\n",
              method, mg_url_uri(http_ctx->url), jwt, (int)host.len, host.buf,
              http_ctx->body_len);

    mg_send(c, http_ctx->body, http_ctx->body_len);
    heap_free(jwt);
    break;
  }
  case MG_EV_HTTP_MSG: {
    struct mg_http_message *restrict const msg = ev_data;
    mbstate_t mbs = {0};
    http_ctx->success = mg_http_status(msg) == 200;

#ifdef ABAG_COINBASE_DEBUG
    wdebug("coinbase: %.*s\n", (int)msg->message.len, msg->message.buf);
#endif

    if (http_ctx->success) {
      http_ctx->rsp_len = 0;
      http_ctx->rsp = heap_calloc(HTTP_RESPONSE_MAX_WCHARS, sizeof(wchar_t));

      for (size_t i = 0, mb_len = 0; i < msg->body.len;
           i += mb_len, http_ctx->rsp_len++) {
        mb_len = mbrtowc(&http_ctx->rsp[http_ctx->rsp_len], &msg->body.buf[i],
                         msg->body.len - i, &mbs);

        if (mb_len == 0)
          break;

        if (mb_len == (size_t)-1) {
          mg_error(c, "%s", strerror(errno));
          return;
        }

        if (mb_len == (size_t)-2 || mb_len == (size_t)-3) {
          mg_error(c, "Incomplete multibyte sequence");
          return;
        }

        // i + mb_len <= SIZE_MAX
        // => i <= SIZE_MAX - mb_len
        // => mb_len <= SIZE_MAX - i
        if (i > SIZE_MAX - mb_len || mb_len > SIZE_MAX - i) {
          werr("%s: %d: %s\n", __FILE__, __LINE__, __func__);
          fatal();
        }

        // rsp_len + 1 <= HTTP_RESPONSE_MAX_WCHARS
        // => rsp_len <= HTTP_RESPONSE_MAX_WCHARS -1
        if (http_ctx->rsp_len > HTTP_RESPONSE_MAX_WCHARS - 1) {
          werr("%s: %d: %s\n", __FILE__, __LINE__, __func__);
          fatal();
        }
      }
    } else {
      mg_error(c, "%d", mg_http_status(msg));
      return;
    }

    c->is_draining = 1;
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
  wdebug("coinbase: HTTP %s %s\n", body != NULL ? "POST" : "GET", url);
  if (body != NULL)
    wdebug("%.*s\n", (int)body_len, body);
#endif

  thread_sleep(&api_request_rate);

  mg_mgr_init(&mgr);
  mg_mgr_config(&mgr);

  c = mg_http_connect(&mgr, url, http_listener, &http_ctx);

  if (c == NULL) {
    werr("coinbase: %s: Could not create HTTP connection\n", url);
    goto cleanup;
  }

  while (!http_ctx.done)
    mg_mgr_poll(&mgr, MG_POLL_WAIT_MILLIS);

  if (!http_ctx.success)
    goto cleanup;

  struct wcjson wcjson = WCJSON_INITIALIZER;

  r = wcjsondocvalues(&wcjson, doc, http_ctx.rsp, http_ctx.rsp_len);
  if (r < 0) {
    werr("coinbase: %s: %s: %.*ls\n", url, strerror(wcjsontoerrno(&wcjson)),
         (int)http_ctx.rsp_len, http_ctx.rsp);
    goto cleanup;
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
    werr("%s: %d: %s: %s: %s: %.*ls\n", __FILE__, __LINE__, __func__, url,
         strerror(wcjsontoerrno(&wcjson)), (int)http_ctx.rsp_len, http_ctx.rsp);
    fatal();
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
    werr("%s: %d: %s: %s: %s: %.*ls\n", __FILE__, __LINE__, __func__, url,
         strerror(wcjsontoerrno(&wcjson)), (int)http_ctx.rsp_len, http_ctx.rsp);
    fatal();
  }

  r = 0;
cleanup:
  mg_mgr_free(&mgr);
  heap_free(http_ctx.rsp);
  return r;
}

static void coinbase_init(void) {
  exchange_coinbase.id = String_cnew(COINBASE_UUID);
  exchange_coinbase.nm = String_cnew("coinbase");
  running = false;
  coinbase_cnf = NULL;
  orders = Queue_new(128, (time_t)0);
  samples = Queue_new((MG_MAX_RECV_SIZE) / sizeof(struct Sample *),
                      (time_t)(WEBSOCKET_STALL_MILLIS / 1000));
  mg_log_set(MG_LL_NONE); // NONE, ERROR, INFO, DEBUG, VERBOSE
  products = Array_new(1024);
  products_by_name = Map_new(1024);
  products_by_id = Map_new(1024);
  products_reload = true;
  accounts = Array_new(256);
  accounts_by_id = Map_new(256);
  accounts_by_currency = Map_new(256);
  accounts_reload = true;
  pricing = NULL;
  mutex_init(&pricing_mutex);
  ws_doc = wcjsondoc_new();
}

static void coinbase_configure(const struct ExchangeConfig *restrict const c) {
  coinbase_cnf = c;
  db_connect(COINBASE_DBCON);
}

static void coinbase_destroy(void) {
  if (coinbase_cnf)
    db_disconnect(COINBASE_DBCON);

  coinbase_cnf = NULL;
  String_delete(exchange_coinbase.id);
  String_delete(exchange_coinbase.nm);
  Queue_delete(orders, Order_delete);
  Queue_delete(samples, Sample_delete);
  Array_delete(products, Product_delete);
  Map_delete(products_by_name, NULL);
  Map_delete(products_by_id, NULL);
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
  for (size_t i = nitems(ws_channels); i > 0; i--) {
    if (ws_channels[i - 1].items != NULL) {
      struct mg_connection *restrict const c =
          mg_ws_connect(mgr, ABAG_COINBASE_WEBSOCKET_URL, ws_listener,
                        &ws_channels[i - 1], NULL);

      ws_channels[i - 1].last_message = mg_millis();

      if (!c) {
        werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__,
             ws_channels[i - 1].name);
        fatal();
      }
    }
  }

  thread_create(&mg_mgr_worker, mg_mgr_worker_func, mgr);
}

static void coinbase_stop(void) {
  Queue_stop(orders);
  Queue_stop(samples);
  running = false;
  thread_join(mg_mgr_worker, NULL);
}

static struct Sample *coinbase_sample_await(void) {
  return Queue_dequeue(samples);
}

static struct Order *coinbase_order_await(void) {
  return Queue_dequeue(orders);
}

static struct Product *
parse_product(const struct wcjson_document *restrict const doc,
              const struct wcjson_value *restrict const prod) {
  char p_uuid[DATABASE_UUID_MAX_LENGTH] = {0};
  char errbuf[WCJSON_BODY_MAX + 1] = {0};
  struct Product *restrict p = NULL;
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

  db_id_to_internal(p_uuid, COINBASE_DBCON, COINBASE_UUID,
                    j_product_id->mbstring);

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
    if (qa->mtx != NULL)
      mutex_unlock(qa->mtx);
    qa = NULL;
  }

  if (ba != NULL) {
    ba_id = String_copy(ba->id);
    ba_active_and_ready = ba->is_active && ba->is_ready;
    if (ba->mtx != NULL)
      mutex_unlock(ba->mtx);
    ba = NULL;
  }

  const enum product_status status_value = product_status(j_status->mbstring);
  const enum product_type type_value = product_type(j_product_type->mbstring);

  p = Product_new();
  p->id = String_cnew(p_uuid);
  p->nm = String_cnew(j_product_id->mbstring);
  p->type = type_value;
  p->status = status_value;
  p->b_id = b_id;
  p->ba_id = ba_id;
  p->q_id = q_id;
  p->qa_id = qa_id;
  p->p_sc = p_dot ? strlen(p_dot + 1) : 0;
  p->p_inc = j_price_increment_num;
  p->b_sc = b_dot ? strlen(b_dot + 1) : 0;
  p->b_inc = j_base_increment_num;
  p->q_sc = q_dot ? strlen(q_dot + 1) : 0;
  p->q_inc = j_quote_increment_num;
  p->is_tradeable =
      qa_id != NULL && ba_id != NULL && type_value == PRODUCT_TYPE_SPOT;

  p->is_active = !(j_is_disabled->is_true || j_cancel_only->is_true ||
                   j_post_only->is_true || j_trading_disabled->is_true ||
                   j_new->is_true) &&
                 status_value == PRODUCT_STATUS_ONLINE;

  if (p->type == PRODUCT_TYPE_UNKNOWN) {
    werr("coinbase: %s->%s: %s: Unsupported product type: %s\n",
         j_quote_currency_id->mbstring, j_base_currency_id->mbstring,
         j_product_type->mbstring,
         wcjsondoc_string(errbuf, sizeof(errbuf), doc, prod, NULL));
  }

  if (p->status == PRODUCT_STATUS_UNKNOWN) {
    werr("coinbase: %s->%s: %s: Unsupported product status: %s\n",
         j_quote_currency_id->mbstring, j_base_currency_id->mbstring,
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
  p->is_tradeable = p->is_tradeable && qa_active_and_ready;
  p->is_tradeable = p->is_tradeable && ba_active_and_ready;

#ifdef ABAG_COINBASE_DEBUG
  if (!p->is_active) {
    wdebug("coinbase: %s->%s: Product not active: %s\n",
           j_quote_currency_id->mbstring, j_base_currency_id->mbstring,
           wcjsondoc_string(errbuf, sizeof(errbuf), doc, prod, NULL));
  }
#endif
ret:
  return p;
}

static struct Array *
parse_products(struct Array *restrict const p,
               const struct wcjson_document *restrict const doc) {
  char errbuf[WCJSON_BODY_MAX + 1] = {0};
  WCJSON_DECLARE_ARRAY_ITEM(products)

  WCJSON_ARRAY_ITEM(doc, doc->values, products, 8, errbuf, ret)

  const struct wcjson_value *restrict j_product = NULL;
  wcjson_value_foreach(j_product, doc, j_products) {
    struct Product *restrict const parsed = parse_product(doc, j_product);

    if (parsed != NULL)
      Array_add_tail(p, parsed);
  }

ret:
  return p;
}

static struct Array *coinbase_products(void) {
  struct wcjson_document doc = WCJSON_DOCUMENT_INITIALIZER;
  char url[URL_MAX_LENGTH];
  void **items;

  Array_lock(products);

  if (products_reload) {
    products_reload = false;
    accounts_reload = true;
    int r =
        snprintf(url, sizeof(url), "%s", ABAG_COINBASE_PRODUCTS_RESOURCE_URL);

    if (r < 0 || (size_t)r >= sizeof(url)) {
      werr("%s: %d: %s\n", __FILE__, __LINE__, __func__);
      fatal();
    }

    if (http_req(&doc, url, ABAG_COINBASE_PRODUCTS_PATH, NULL, 0) == 0) {
      Array_clear(products, Product_delete);
      parse_products(products, &doc);
      Array_shrink(products);
      Map_delete(products_by_name, NULL);
      Map_delete(products_by_id, NULL);
      products_by_name = Map_new(Array_size(products));
      products_by_id = Map_new(Array_size(products));

      items = Array_items(products);
      for (size_t i = Array_size(products); i > 0; i--) {
        if (Map_put(products_by_name, ((struct Product *)items[i - 1])->nm,
                    items[i - 1])) {
          werr("%s: %d: %s: %s: Duplicate product\n", __FILE__, __LINE__,
               __func__, String_chars(((struct Product *)items[i - 1])->nm));
          fatal();
        }
        if (Map_put(products_by_id, ((struct Product *)items[i - 1])->id,
                    items[i - 1])) {
          werr("%s: %d: %s: %s: Duplicate product\n", __FILE__, __LINE__,
               __func__, String_chars(((struct Product *)items[i - 1])->id));
          fatal();
        }
      }
    }
  }

  heap_free(doc.values);
  heap_free(doc.strings);
  heap_free(doc.mbstrings);
  heap_free(doc.esc);
  return products;
}

static struct Product *
coinbase_product(const struct String *restrict const id) {
  struct Array *restrict const p_array = coinbase_products();
  struct Product *restrict p = Map_get(products_by_id, id);
  p->mtx = Array_mutex(p_array);
  return p;
}

static struct Product *
coinbase_product_name(const struct String *restrict const name) {
  struct Array *restrict const p_array = coinbase_products();
  struct Product *restrict const p = Map_get(products_by_name, name);
  p->mtx = Array_mutex(p_array);
  return p;
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
    werr("coinbase: %s: Account type unsupported\n", j_type->mbstring);

ret:
  return a;
}

static struct Array *
parse_accounts(struct Array *restrict const a,
               const struct wcjson_document *restrict const doc) {
  char errbuf[WCJSON_BODY_MAX + 1] = {0};
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
ret:
  return a;
}

static struct Array *accounts_with_cursor(struct Array *restrict result,
                                          const char *restrict const cursor) {
  char url[URL_MAX_LENGTH];
  struct wcjson_document doc = WCJSON_DOCUMENT_INITIALIZER;
  char errbuf[WCJSON_BODY_MAX + 1] = {0};
  int r;
  WCJSON_DECLARE_BOOL_ITEM(has_next)
  WCJSON_DECLARE_STRING_ITEM(cursor)

  if (cursor) {
    r = snprintf(url, sizeof(url), "%s?limit=%d&cursor=%s",
                 ABAG_COINBASE_ACCOUNTS_RESOURCE_URL,
                 ABAG_COINBASE_ACCOUNTS_RESOURCE_LIMIT, cursor);

    if (r < 0 || (size_t)r >= sizeof(url)) {
      werr("%s: %d: %s\n", __FILE__, __LINE__, __func__);
      fatal();
    }
  } else {
    r = snprintf(url, sizeof(url), "%s?limit=%d",
                 ABAG_COINBASE_ACCOUNTS_RESOURCE_URL,
                 ABAG_COINBASE_ACCOUNTS_RESOURCE_LIMIT);

    if (r < 0 || (size_t)r >= sizeof(url)) {
      werr("%s: %d: %s\n", __FILE__, __LINE__, __func__);
      fatal();
    }
  }

  if (http_req(&doc, url, ABAG_COINBASE_ACCOUNTS_PATH, NULL, 0) == 0) {
    result = parse_accounts(result, &doc);
    WCJSON_BOOL_ITEM(&doc, doc.values, has_next, 8, errbuf, ret)

    if (j_has_next->is_true) {
      WCJSON_STRING_ITEM(&doc, doc.values, cursor, 6, errbuf, ret)
      result = accounts_with_cursor(result, j_cursor->mbstring);
    }
  }

ret:
  heap_free(doc.values);
  heap_free(doc.strings);
  heap_free(doc.mbstrings);
  heap_free(doc.esc);
  return result;
}

static struct Array *coinbase_accounts(void) {
  void **items;

  Array_lock(accounts);

  if (accounts_reload) {
    accounts_reload = false;
    Array_clear(accounts, Account_delete);
    Map_delete(accounts_by_id, NULL);
    Map_delete(accounts_by_currency, NULL);
    accounts_with_cursor(accounts, NULL);
    Array_shrink(accounts);

    accounts_by_id = Map_new(Array_size(accounts));
    accounts_by_currency = Map_new(Array_size(accounts));

    items = Array_items(accounts);
    for (size_t i = Array_size(accounts); i > 0; i--) {
      if (Map_put(accounts_by_currency, ((struct Account *)items[i - 1])->c_id,
                  items[i - 1]) != NULL) {
        werr("%s: %d: %s: %s: Duplicate account\n", __FILE__, __LINE__,
             __func__, String_chars(((struct Account *)items[i - 1])->c_id));
        fatal();
      }
      if (Map_put(accounts_by_id, ((struct Account *)items[i - 1])->id,
                  items[i - 1]) != NULL) {
        werr("%s: %d: %s: %s: Duplicate account\n", __FILE__, __LINE__,
             __func__, String_chars(((struct Account *)items[i - 1])->id));
        fatal();
      }
    }
  }

  return accounts;
}

static struct Account *
coinbase_account_currency(const struct String *restrict const currency) {
  const struct Array *restrict const haystack = coinbase_accounts();
  struct Account *restrict needle = NULL;
  void **items = Array_items(haystack);

  for (size_t i = Array_size(haystack); i > 0; i--)
    if (String_equals(((struct Account *)items[i - 1])->c_id, currency)) {
      needle = items[i - 1];
      needle->mtx = Array_mutex(accounts);
      break;
    }

  return needle;
}

static struct Account *
coinbase_account(const struct String *restrict const id) {
  char path[URL_MAX_LENGTH] = {0};
  char url[URL_MAX_LENGTH] = {0};
  char errbuf[WCJSON_BODY_MAX + 1] = {0};
  struct Account *restrict a = NULL;
  struct wcjson_document doc = WCJSON_DOCUMENT_INITIALIZER;
  WCJSON_DECLARE_OBJECT_ITEM(account)
  int r = snprintf(path, sizeof(path), ABAG_COINBASE_ACCOUNT_PATH,
                   String_chars(id));

  if (r < 0 || (size_t)r >= sizeof(path)) {
    werr("%s: %d: %s\n", __FILE__, __LINE__, __func__);
    fatal();
  }

  r = snprintf(url, sizeof(url), "%s%s", ABAG_COINBASE_ADVANCED_API_URI, path);
  if (r < 0 || (size_t)r >= sizeof(url)) {
    werr("%s: %d: %s\n", __FILE__, __LINE__, __func__);
    fatal();
  }

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
  WCJSON_DECLARE_PRODUCT_ITEM(product_id)
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
  //      size_in_quote boolean
  //      Not available at product level and via user channel events.
  WCJSON_DECLARE_BOOL_ITEM_OPT(size_inclusive_of_fees);
  WCJSON_DECLARE_BOOL_ITEM_OPT(size_in_quote)

  WCJSON_STRING_ITEM(doc, order, order_id, 8, errbuf, ret)
  WCJSON_PRODUCT_ITEM(doc, order, product_id, 10, errbuf, ret)
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
  o->p_id = String_copy(j_product_id_p->id);
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
  if (j_product_id_p != NULL)
    mutex_unlock(j_product_id_p->mtx);

  return o;
}

static struct Order *coinbase_order(const struct String *restrict const id) {
  char path[URL_MAX_LENGTH] = {0};
  char url[URL_MAX_LENGTH] = {0};
  char errbuf[WCJSON_BODY_MAX + 1] = {0};
  struct Order *restrict o = NULL;
  struct wcjson_document doc = WCJSON_DOCUMENT_INITIALIZER;
  WCJSON_DECLARE_OBJECT_ITEM(order)
  int r =
      snprintf(path, sizeof(path), ABAG_COINBASE_ORDER_PATH, String_chars(id));

  if (r < 0 || (size_t)r >= sizeof(path)) {
    werr("%s: %d: %s\n", __FILE__, __LINE__, __func__);
    fatal();
  }

  r = snprintf(url, sizeof(url), "%s%s", ABAG_COINBASE_ADVANCED_API_URI, path);
  if (r < 0 || (size_t)r >= sizeof(url)) {
    werr("%s: %d: %s\n", __FILE__, __LINE__, __func__);
    fatal();
  }

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

static bool coinbase_cancel(const struct String *restrict const id) {
  bool ret = false;
  bool found = false;
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
  if (wcjsondocstrings(&wcjson, b_doc) < 0) {
    werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__,
         strerror(wcjsontoerrno(&wcjson)));
    fatal();
  }

  char mbbody[WCJSON_BODY_MAX + 1] = {0};
  size_t mb_len = 0;
  wcjsondoc_string(mbbody, sizeof(mbbody), b_doc, b_doc->values, &mb_len);

  if (http_req(&doc, ABAG_COINBASE_ORDER_CANCEL_RESOURCE_URL,
               ABAG_COINBASE_ORDER_CANCEL_PATH, mbbody, mb_len) == 0) {
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
      werr("coinbase: %s: Order id not found: %s\n", String_chars(id),
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
                              const struct String *restrict const p_id,
                              const char *restrict const side,
                              const char *restrict const base_amount,
                              const char *restrict const price,
                              size_t *restrict mb_len) {
  char client_id[DATABASE_UUID_MAX_LENGTH] = {0};
  char ext_id[DATABASE_UUID_MAX_LENGTH] = {0};
  struct wcjson_value *restrict const body = wcjson_object(doc);
  struct wcjson_value *restrict const conf = wcjson_object(doc);
  struct wcjson_value *restrict const llgtc = wcjson_object(doc);

  db_uuid(client_id, COINBASE_DBCON);
  db_id_to_external(ext_id, COINBASE_DBCON, COINBASE_UUID, String_chars(p_id));

  struct wcjson_value *restrict const j_client_id =
      wcjson_string(doc, client_id);

  struct wcjson_value *restrict const j_p_id = wcjson_string(doc, ext_id);
  struct wcjson_value *restrict const j_side = wcjson_string(doc, side);
  struct wcjson_value *restrict const j_base = wcjson_string(doc, base_amount);
  struct wcjson_value *restrict const j_price = wcjson_string(doc, price);
  struct wcjson_value *restrict const j_post_only = wcjson_bool(doc, false);

  wcjson_object_add(doc, body, L"order_configuration", 19, conf);
  wcjson_object_add(doc, conf, L"limit_limit_gtc", 15, llgtc);
  wcjson_object_add(doc, body, L"client_order_id", 15, j_client_id);
  wcjson_object_add(doc, body, L"product_id", 10, j_p_id);
  wcjson_object_add(doc, body, L"side", 4, j_side);
  wcjson_object_add(doc, llgtc, L"post_only", 9, j_post_only);
  wcjson_object_add(doc, llgtc, L"base_size", 9, j_base);
  wcjson_object_add(doc, llgtc, L"limit_price", 11, j_price);

  wcjsondoc_string(mbbody, mbbody_nitems, doc, doc->values, mb_len);
}

static struct String *coinbase_order_post(
    const struct String *restrict const p_id, const char *restrict const side,
    const char *restrict const base_amount, const char *restrict const price) {
  struct String *restrict o_id = NULL;
  char body[WCJSON_BODY_MAX + 1] = {0};
  char errbuf[WCJSON_BODY_MAX + 1] = {0};
  size_t b_len = 0;
  struct wcjson_document doc = WCJSON_DOCUMENT_INITIALIZER;
  struct wcjson_document *restrict const b_doc = wcjsondoc_new();
  WCJSON_DECLARE_BOOL_ITEM(success)
  WCJSON_DECLARE_OBJECT_ITEM(success_response)
  WCJSON_DECLARE_STRING_ITEM(order_id)

  order_create_body(body, sizeof(body), b_doc, p_id, side, base_amount, price,
                    &b_len);

  if (http_req(&doc, ABAG_COINBASE_ORDER_CREATE_RESOURCE_URL,
               ABAG_COINBASE_ORDER_CREATE_PATH, body, b_len) == 0) {
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

static struct String *coinbase_buy(const struct String *restrict const p_id,
                                   const char *restrict const base_amount,
                                   const char *restrict const price) {
  return coinbase_order_post(p_id, "BUY", base_amount, price);
}

static struct String *coinbase_sell(const struct String *restrict const p_id,
                                    const char *restrict const base_amount,
                                    const char *restrict const price) {
  return coinbase_order_post(p_id, "SELL", base_amount, price);
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
  char url[URL_MAX_LENGTH];
  struct wcjson_document doc = WCJSON_DOCUMENT_INITIALIZER;

  mutex_lock(&pricing_mutex);

  if (pricing != NULL)
    goto ret;

  int r = snprintf(url, sizeof(url), "%s?product_type=SPOT",
                   ABAG_COINBASE_FEES_RESOURCE_URL);

  if (r < 0 || (size_t)r >= sizeof(url)) {
    werr("%s: %d: %s\n", __FILE__, __LINE__, __func__);
    fatal();
  }

  if (http_req(&doc, url, ABAG_COINBASE_FEES_PATH, NULL, 0) == 0)
    pricing = parse_pricing(&doc, doc.values);

ret:
  mutex_unlock(&pricing_mutex);
  heap_free(doc.values);
  heap_free(doc.strings);
  heap_free(doc.mbstrings);
  heap_free(doc.esc);
  return pricing;
}
