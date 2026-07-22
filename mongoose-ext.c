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

#include "mongoose-ext.h"

#include "config.h"
#include "string.h"

extern const struct Numeric *restrict const milli_nanos;
extern const struct Config *restrict const cnf;

void mg_mgr_config(struct mg_mgr *restrict const mgr) {
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
