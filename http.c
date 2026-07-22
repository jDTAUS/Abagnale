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

#include "charset.h"
#include "heap.h"
#include "http.h"
#include "mongoose-ext.h"
#include "proc.h"
#include "version.h"

#ifndef DEFAULT_ABAG_HTTP_TIMEOUT_MILLIS
#define DEFAULT_ABAG_HTTP_TIMEOUT_MILLIS 60000L
#endif

extern const bool verbose;

static unsigned long http_timeout_ms;

struct http_ctx {
  const char *restrict url;
  const struct Map *restrict headers;
  const char *restrict body;
  const size_t body_len;
  char *restrict rsp;
  size_t rsp_len;
  uint64_t timeout_ms;
  bool success;
  bool done;
};

void http_init(void) {
  http_timeout_ms =
      envul("ABAG_HTTP_TIMEOUT_MILLIS", DEFAULT_ABAG_HTTP_TIMEOUT_MILLIS);

  if (verbose)
    wout("\tABAG_HTTP_TIMEOUT_MILLIS=%lu\n", http_timeout_ms);
}

void http_destroy(void) {}

static void http_evt_handler(struct mg_connection *c, int ev, void *ev_data) {
  struct http_ctx *restrict const http_ctx = c->fn_data;
  const char *restrict const method = http_ctx->body_len > 0 ? "POST" : "GET";

  switch (ev) {
  case MG_EV_OPEN:
    http_ctx->timeout_ms = mg_millis() + http_timeout_ms;
    break;
  case MG_EV_CLOSE:
    http_ctx->done = true;
    break;
  case MG_EV_POLL: {
    if (mg_millis() > http_ctx->timeout_ms &&
        (c->is_connecting || c->is_resolving)) {
      mg_error(c, "Connect timeout");
    }
    break;
  }
  case MG_EV_ERROR: {
    c->is_draining = 1;
    http_ctx->success = false;
    werr("%s: %s\n", http_ctx->url, (char *)ev_data);
    break;
  }
  case MG_EV_CONNECT: {
    struct mg_str host = mg_url_host(http_ctx->url);

    if (c->is_tls) {
      struct mg_tls_opts http_tls_opts = {0};
      http_tls_opts.name = host;
      mg_tls_init(c, &http_tls_opts);
    }

    mg_printf(c, "%s %s HTTP/1.0\r\n", method, mg_url_uri(http_ctx->url));

    if (http_ctx->headers != NULL) {
      struct MapIterator *restrict const it =
          MapIterator_new(http_ctx->headers);

      while (MapIterator_next(it))
        mg_printf(c, "%s: %s\r\n", String_chars(MapIterator_key(it)),
                  String_chars(MapIterator_value(it)));

      MapIterator_delete(it);
    }

    mg_printf(c,
              "Host: %.*s\r\n"
              "Content-Type: application/json\r\n"
              "Content-Length: %u\r\n"
              "Connection: close\r\n"
              "User-Agent: Abagnale; %s\r\n"
              "\r\n",
              (int)host.len, host.buf, http_ctx->body_len, ABAG_REVISION);

    if (!mg_send(c, http_ctx->body, http_ctx->body_len))
      mg_error(c, "OOM");

    break;
  }
  case MG_EV_HTTP_MSG: {
    struct mg_http_message *restrict const msg = ev_data;
    http_ctx->success = mg_http_status(msg) == 200;
    c->is_draining = 1;

#ifdef ABAG_HTTP_DEBUG
    wout("%s: %.*s\n", http_ctx->url, (int)msg->message.len, msg->message.buf);
#endif

    if (!http_ctx->success) {
      werr("%s: HTTP %d: %.*s\n", http_ctx->url, mg_http_status(msg),
           (int)msg->body.len, msg->body.buf);
      return;
    }

    http_ctx->rsp_len = msg->body.len;
    http_ctx->rsp = heap_malloc(http_ctx->rsp_len);
    memcpy(http_ctx->rsp, msg->body.buf, http_ctx->rsp_len);
    break;
  }
  }
}

int http_request_json(struct wcjson_document *restrict rsp_doc,
                      const char *restrict const url,
                      const struct Map *restrict const headers,
                      const char *restrict const body, const size_t body_len) {
  int r = -1;
  const int saved_errno = errno;
  struct mg_mgr mgr = {0};
  struct mg_connection *restrict c = NULL;
  struct http_ctx http_ctx = {
      .success = false,
      .done = false,
      .url = url,
      .body = body,
      .body_len = body_len,
      .rsp = NULL,
      .rsp_len = 0,
      .headers = headers,
  };

#ifdef ABAG_HTTP_DEBUG
  wout("HTTP %s %s\n", body != NULL ? "POST" : "GET", url);
  if (body != NULL)
    wout("%.*s\n", (int)body_len, body);
#endif

  mg_mgr_init(&mgr);
  mg_mgr_config(&mgr);

  c = mg_http_connect(&mgr, url, http_evt_handler, &http_ctx);

  if (c == NULL) {
    werr("%s: Failure creating connection\n", url);
    goto err;
  }

  while (!http_ctx.done)
    mg_mgr_poll(&mgr, http_timeout_ms);

  if (!http_ctx.success)
    goto err;

  if (json_mbparse(rsp_doc, http_ctx.rsp, http_ctx.rsp_len) < 0)
    goto err;

  r = 0;
err:
  mg_mgr_free(&mgr);
  heap_free(http_ctx.rsp);
  errno = saved_errno;
  return r;
}
