/* $SchulteIT: config.h 15252 2025-11-03 01:36:59Z schulte $ */
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

#ifndef ABAG_CONF_H
#define ABAG_CONF_H

#include "array.h"
#include "exchange.h"
#include "map.h"
#include "math.h"
#include "string.h"

#ifndef ABAG_CONFIG_FILE
#define ABAG_CONFIG_FILE "/etc/abagnale/abagnale.conf"
#endif

#ifndef ABAG_DATABASE_TARGET
#define ABAG_DATABASE_TARGET "ABAGNALE"
#endif

#ifndef ABAG_DATABASE_USER
#define ABAG_DATABASE_USER "abagnale"
#endif

struct Config {
  struct String *restrict db_tgt;
  struct String *restrict db_usr;
  struct String *restrict plts_dir;
  struct String *restrict dns_v4;
  struct String *restrict dns_v6;
  struct Numeric *restrict dns_to;
  struct Numeric *restrict wnanos_max;
  struct Map *restrict e_cnf;
  struct Array *restrict m_cnf;
};

struct Pattern {
  struct String *restrict pat;
  bool neg;
};

enum market_config_origin {
  MARKET_CONFIG_ORIGIN_SYSTEM = 1,
  MARKET_CONFIG_ORIGIN_USER
};

struct MarketConfig {
  struct String *restrict e_nm;
  struct String *restrict a_nm;
  struct Array *restrict m_pats;
  struct Numeric *restrict q_tgt;
  struct String *restrict q_id;
  struct Numeric *restrict v_pc;
  struct Numeric *restrict v_wnanos;
  struct Numeric *restrict wnanos;
  struct Numeric *restrict sr_min;
  struct Numeric *restrict sr_max;
  struct Numeric *restrict bo_minnanos;
  struct Numeric *restrict bo_maxnanos;
  struct Numeric *restrict so_minnanos;
  struct Numeric *restrict so_maxnanos;
  struct Numeric *restrict sl_dlnanos;
  struct Numeric *restrict tl_dlnanos;
  struct Numeric *restrict tp_dlnanos;
  enum market_config_origin origin;
};

void config_init(void);
void config_destroy(void);

struct Config *Config_new(void);
void Config_delete(void *restrict const);

struct Pattern *Pattern_new(void);
void Pattern_delete(void *restrict const);

struct MarketConfig *MarketConfig_new(void);
void MarketConfig_delete(void *restrict const);
bool MarketConfig_match(const struct MarketConfig *restrict const,
                        const struct String *restrict const);

int config_symset(char *restrict const);
int config_fparse(struct Config *const restrict, const char *restrict const);
#endif
