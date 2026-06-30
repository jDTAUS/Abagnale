# $SchulteIT: Makefile 15282 2025-11-05 22:54:21Z schulte $
# $JDTAUS: Makefile 9605 2026-06-30 08:27:51Z schulte $

#
# Copyright (c) 2018 - 2026 Christian Schulte <cs@schulte.it>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#

.SUFFIXES: .c .pgc .o .y

CC=cc

# ECPG - Embedded SQL in C
#   https://www.postgresql.org/docs/18/ecpg.html
ECPG=ecpg

# yacc - An LALR(1) parser generator
#   https://man.openbsd.org/OpenBSD-7.8/yacc
YACC=yacc

# clang-format - A tool to format code
#   https://clang.llvm.org/docs/ClangFormat.html
FORMAT=clang-format

INCLUDES=-I/usr/include
INCLUDES+=-I/usr/include/postgresql
INCLUDES+=-I/usr/local/include
INCLUDES+=-I/usr/local/include/postgresql

DEBUG=
#DEBUG+=-g
#DEBUG+=-O0
#DEBUG+=-DABAG_MATH_DEBUG
#DEBUG+=-DABAG_SQL_DEBUG
#DEBUG+=-DABAG_COINBASE_DEBUG

CONFIG=
#CONFIG+=-DDEFAULT_ABAG_CONFIG_FILE=\"/etc/abagnale/abagnale.conf\"
#CONFIG+=-DDEFAULT_ABAG_DATABASE_TARGET=\"ABAGNALE\"
#CONFIG+=-DDEFAULT_ABAG_DATABASE_USER=\"abagnale\"
#CONFIG+=-DDEFAULT_ABAG_TICKER_WORKERS=12
#CONFIG+=-DDEFAULT_ABAG_TRADE_WORKERS=6
#CONFIG+=-DDEFAULT_CDP_REST_URI=\"https://api.coinbase.com\"
#CONFIG+=-DDEFAULT_CDP_WS_URI=\"wss://advanced-trade-ws.coinbase.com\"
#CONFIG+=-DDEFAULT_CDP_ACCOUNT_PATH=\"/api/v3/brokerage/accounts/%s\"
#CONFIG+=-DDEFAULT_CDP_ACCOUNTS_PATH=\"/api/v3/brokerage/accounts\"
#CONFIG+=-DDEFAULT_CDP_FEES_PATH=\"/api/v3/brokerage/transaction_summary\"
#CONFIG+=-DDEFAULT_CDP_ORDER_PATH=\"/api/v3/brokerage/orders/historical/%s\"
#CONFIG+=-DDEFAULT_CDP_ORDER_CANCEL_PATH=\"/api/v3/brokerage/orders/batch_cancel\"
#CONFIG+=-DDEFAULT_CDP_ORDER_CREATE_PATH=\"/api/v3/brokerage/orders\"
#CONFIG+=-DDEFAULT_CDP_PRODUCTS_PATH=\"/api/v3/brokerage/products\"
#CONFIG+=-DDEFAULT_CDP_HTTP_REQUESTS_PER_SECOND=30
#CONFIG+=-DDEFAULT_CDP_HTTP_RETRY_SECONDS=3
#CONFIG+=-DDEFAULT_CDP_HTTP_STALL_MILLIS=3600000L
#CONFIG+=-DDEFAULT_CDP_HTTP_TIMEOUT_MILLIS=60000L

PROFILE=
#PROFILE+=-pg

LTO=
LTO+=-flto=auto

STANDARD=-std=c2x

WARNINGS=-Wall
WARNINGS+=-Werror
WARNINGS+=-Wpedantic
WARNINGS+=-Wstrict-prototypes
WARNINGS+=-Wmissing-prototypes
WARNINGS+=-Wmissing-declarations
WARNINGS+=-Wpointer-arith
WARNINGS+=-Wsign-compare
WARNINGS+=-Wformat-signedness
WARNINGS+=-Wformat-truncation
WARNINGS+=-Wuninitialized
WARNINGS+=-Wshadow
#WARNINGS+=-Wcast-qual

CFLAGS=$(INCLUDES) $(DEBUG) $(PROFILE) $(LTO) $(CONFIG) $(WARNINGS)
CFLAGS+=-DMULTI_THREADED
CFLAGS+=-DSTRING_INTERNING
CLFAGS+=-DMG_TLS=MG_TLS_OPENSSL
CFLAGS+=-DMG_MAX_RECV_SIZE="(1024UL * 1024UL * 1024UL)"
CFLAGS+=-DWCHAR_T_UTF32
#CFLAGS+=-DWCHAR_T_UTF16
#CFLAGS+=-DWCHAR_T_UTF8
CFLAGS+=-pedantic
CFLAGS+=-O3

LDFLAGS=$(DEBUG) $(PROFILE) $(LTO)
LDFLAGS+=-L/usr/local/lib

# ECPG - Embedded SQL in C
#   https://www.postgresql.org/docs/18/ecpg.html
LDFLAGS+=-lecpg

# ECPG - Embedded SQL in C
#   https://www.postgresql.org/docs/18/ecpg-pgtypes.html
LDFLAGS+=-lpgtypes

LDFLAGS+=-lm
LDFLAGS+=-lcrypto
LDFLAGS+=-lssl
#LDFLAGS+=-lstdthreads
#LDFLAGS+=-lpthread

YACCFLAGS=
YACCFLAGS+=-o

HEADERS=abagnale.h
HEADERS+=array.h
HEADERS+=config.h
HEADERS+=database.h
HEADERS+=exchange.h
HEADERS+=heap.h
HEADERS+=host.h
HEADERS+=map.h
HEADERS+=math.h
HEADERS+=proc.h
HEADERS+=queue.h
HEADERS+=string.h
HEADERS+=thread.h
HEADERS+=time.h
HEADERS+=wcjson.h
HEADERS+=wcjson-document.h

OBJS=abagnale.o
OBJS+=abagnalectl.o
OBJS+=algorithm-trend.o
OBJS+=array.o
OBJS+=config.o
OBJS+=exchange.o
OBJS+=exchange-coinbase.o
OBJS+=heap.o
OBJS+=main.o
OBJS+=map.o
OBJS+=patterns.o
OBJS+=proc.o
OBJS+=queue.o
OBJS+=string.o
OBJS+=thread.o
OBJS+=time.o
OBJS+=wcjson.o
OBJS+=wcjson-document.o

OBJS+=database-postgresql.o
OBJS+=math-postgresql.o

OBJS+=mongoose.o

FORMATSRC=abagnale.c
FORMATSRC+=abagnalectl.c
FORMATSRC+=algorithm-trend.c
FORMATSRC+=array.c
FORMATSRC+=exchange.c
FORMATSRC+=exchange-coinbase.c
FORMATSRC+=heap.c
FORMATSRC+=main.c
FORMATSRC+=map.c
FORMATSRC+=proc.c
FORMATSRC+=queue.c
FORMATSRC+=string.c
FORMATSRC+=thread.c
FORMATSRC+=time.c
FORMATSRC+=wcjson.c
FORMATSRC+=wcjson-document.c

FORMATSRC+=database-postgresql.pgc
FORMATSRC+=math-postgresql.c

CLEAN=$(OBJS) database-postgresql.c y.tab.h

all: abagnale abagnalectl

clean:
	rm -f $(CLEAN)

format:
	$(FORMAT) -i $(FORMATSRC) $(HEADERS)

abagnale: $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

abagnalectl: $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

.c.o:
	$(CC) $(STANDARD) $(CFLAGS) -c -o $@ $<

mongoose.o: mongoose.c
	$(CC) $(CFLAGS) -c -o $@ $<

.pgc.c:
	$(ECPG) $(INCLUDES) $<

.y.c:
	$(YACC) $(YACCFLAGS) $@ $<

$(OBJS): $(HEADERS) version.h

