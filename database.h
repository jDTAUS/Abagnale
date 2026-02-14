/* $SchulteIT: database.h 15189 2025-10-27 05:41:45Z schulte $ */
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

#ifndef ABAG_DATABASE_H
#define ABAG_DATABASE_H

#include "math.h"

#include <stdbool.h>
#include <stddef.h>

#define DATABASE_CONNECTION_NAME_MAX_LENGTH (size_t)64
#define DATABASE_UUID_MAX_LENGTH (size_t)64
#define DATABASE_CID_MAX_LENGTH (size_t)255
#define DATABASE_TRADE_STATUS_MAX_LENGTH (size_t)7
#define DATABASE_CANDLE_TREND_MAX_LENGTH (size_t)4

struct db_sample_rec {
  struct Numeric *nanos;
  struct Numeric *price;
};

struct db_stats_rec {
  bool bd_min_null;
  bool bd_max_null;
  bool bd_avg_null;
  bool sd_min_null;
  bool sd_max_null;
  bool sd_avg_null;
  struct Numeric *bd_min;
  struct Numeric *bd_max;
  struct Numeric *bd_avg;
  struct Numeric *sd_min;
  struct Numeric *sd_max;
  struct Numeric *sd_avg;
  struct Numeric *bcl_factor;
  struct Numeric *scl_factor;
};

struct db_trade_rec {
  bool bo_id_null;
  bool so_id_null;
  bool b_cnanos_null;
  bool b_dnanos_null;
  bool b_price_null;
  bool b_b_ordered_null;
  bool b_b_filled_null;
  bool b_q_fees_null;
  bool b_q_filled_null;
  bool s_cnanos_null;
  bool s_dnanos_null;
  bool s_price_null;
  bool s_b_ordered_null;
  bool s_b_filled_null;
  bool s_q_fees_null;
  bool s_q_filled_null;
  char id[DATABASE_UUID_MAX_LENGTH + 1];
  char e_id[DATABASE_UUID_MAX_LENGTH + 1];
  char m_id[DATABASE_UUID_MAX_LENGTH + 1];
  char b_id[DATABASE_CID_MAX_LENGTH + 1];
  char q_id[DATABASE_CID_MAX_LENGTH + 1];
  char status[DATABASE_TRADE_STATUS_MAX_LENGTH + 1];
  char bo_id[DATABASE_UUID_MAX_LENGTH + 1];
  char so_id[DATABASE_UUID_MAX_LENGTH + 1];
  struct Numeric *b_cnanos;
  struct Numeric *b_dnanos;
  struct Numeric *b_price;
  struct Numeric *b_b_ordered;
  struct Numeric *b_b_filled;
  struct Numeric *b_q_fees;
  struct Numeric *b_q_filled;
  struct Numeric *s_cnanos;
  struct Numeric *s_dnanos;
  struct Numeric *s_price;
  struct Numeric *s_b_ordered;
  struct Numeric *s_b_filled;
  struct Numeric *s_q_fees;
  struct Numeric *s_q_filled;
};

struct db_balance_rec {
  struct Numeric *q;
  struct Numeric *b;
};

struct db_plot_rec {
  char id[DATABASE_UUID_MAX_LENGTH + 1];
  struct Numeric *snanos;
  struct Numeric *enanos;
};

struct db_datapoint_rec {
  struct Numeric *x;
  struct Numeric *y;
};

struct db_candle_rec {
  struct Numeric *o;
  struct Numeric *h;
  struct Numeric *l;
  struct Numeric *c;
  struct Numeric *onanos;
  struct Numeric *hnanos;
  struct Numeric *lnanos;
  struct Numeric *cnanos;
};

struct db_trend_state_rec {
  struct Numeric *cd_lnanos;
  struct Numeric *cd_langle;
  char cd_ltrend[DATABASE_CANDLE_TREND_MAX_LENGTH + 1];
};

void *db_connect(const char *const);
void db_disconnect(void *const);

void db_tx_begin(const void *const);
void db_tx_commit(const void *const);
void db_tx_rollback(const void *const);

bool db_vacuum(const void *const, const char *const);

void db_uuid(char *const, const void *const);

void db_symbol_to_id(char *const, const void *const, const char *const,
                     const char *const);
void db_id_to_symbol(char *const, const void *const, const char *const,
                     const char *const);

void db_sample_create(const void *const, const char *const, const char *const,
                      const struct Numeric *const, const struct Numeric *const);
void db_samples_stddev(struct Numeric *const, const void *const,
                       const char *const, const char *const,
                       const struct Numeric *const);

void db_samples_open(const void *const, const char *const, const char *const,
                     const struct Numeric *const);
bool db_samples_next(struct db_sample_rec *, const void *const);
void db_samples_close(const void *const);

bool db_stats(struct db_stats_rec *, const void *const, const char *const,
              const char *const);
void db_stats_bcl_factor(const void *const, const char *const,
                         const char *const, struct Numeric *const);
void db_stats_scl_factor(const void *const, const char *const,
                         const char *const, struct Numeric *const);

void db_trades_open(const void *const, const char *const, const char *const);
bool db_trades_next(struct db_trade_rec *const, const void *const);
void db_trades_close(const void *const);

void db_trades_hold(struct db_balance_rec *, const void *const,
                    const char *const, const char *const, const char *const);

void db_trade_bcreate(char *const, const void *const, const char *const,
                      const char *const, const char *const, const char *const,
                      const char *const, const struct Numeric *const,
                      const struct Numeric *const);
void db_trade_bupdate(const void *const, const char *const, const char *const,
                      const struct Numeric *const, const struct Numeric *const);
void db_trade_breset(const void *const, const char *const);
void db_trade_bopen(const void *const, const char *const,
                    const struct Numeric *const, const struct Numeric *const,
                    const struct Numeric *const, const struct Numeric *const);
void db_trade_bfill(const void *const, const char *const,
                    const struct Numeric *const, const struct Numeric *const,
                    const struct Numeric *const, const struct Numeric *const,
                    const struct Numeric *const, const bool);

void db_trade_screate(char *const, const void *const, const char *const,
                      const char *const, const char *const, const char *const,
                      const char *const, const struct Numeric *const,
                      const struct Numeric *const);
void db_trade_supdate(const void *const, const char *const, const char *const,
                      const struct Numeric *const, const struct Numeric *const);
void db_trade_sreset(const void *const, const char *const);
void db_trade_sopen(const void *const, const char *const,
                    const struct Numeric *const, const struct Numeric *const,
                    const struct Numeric *const, const struct Numeric *const);
void db_trade_sfill(const void *const, const char *const,
                    const struct Numeric *const, const struct Numeric *const,
                    const struct Numeric *const, const struct Numeric *const,
                    const struct Numeric *const, const bool);

void db_trade_delete(const void *const, const char *const);

void db_tx_plot_enanos(const void *const, const char *const,
                       const struct Numeric *const);
void db_tx_plot_datapoint(const void *const, const char *const,
                          const struct Numeric *const,
                          const struct Numeric *const);

void db_tx_trend_plot(struct db_plot_rec *const, const void *const,
                      const char *const, const char *const);

void db_tx_trend_plot_candle(const void *const, const char *const,
                             const char *const,
                             const struct db_candle_rec *const);

void db_tx_trend_plot_marker(const void *const, const char *const,
                             const char *const, const struct Numeric *const,
                             const struct Numeric *const);

void db_tx_trend_plot_samples_open(const void *const, const char *const,
                                   const char *const);
bool db_tx_trend_plot_samples_next(struct db_datapoint_rec *const,
                                   const void *const);
void db_tx_trend_plot_samples_close(const void *const);

void db_tx_trend_plot_candles_open(const void *const, const char *const,
                                   const char *const);
bool db_tx_trend_plot_candles_next(struct db_candle_rec *const,
                                   const void *const);
void db_tx_trend_plot_candles_close(const void *const);

void db_tx_trend_plot_markers_open(const void *const, const char *const,
                                   const char *const);
bool db_tx_trend_plot_markers_next(struct db_datapoint_rec *const,
                                   const void *const);
void db_tx_trend_plot_markers_close(const void *const);

void db_trend_state(struct db_trend_state_rec *const, const void *const,
                    const char *const, const char *const);
void db_trend_state_update(const void *const, const char *const,
                           const char *const,
                           const struct db_trend_state_rec *const);

#endif
