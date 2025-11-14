# $SchulteIT: Makefile 15282 2025-11-05 22:54:21Z schulte $
# $JDTAUS$

#
# Copyright (c) 2018 - 2025 Christian Schulte <cs@schulte.it>
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

INCLUDES=-I/usr/include/postgresql
INCLUDES+=-I/usr/local/include
INCLUDES+=-I/home/schulte/wcjson/include

DEBUG=
DEBUG+=-g
#DEBUG+=-O0 -DABAG_DEBUG
#DEBUG+=-DABAG_MATH_DEBUG
#DEBUG+=-DABAG_SQL_DEBUG
#DEBUG+=-DABAG_COINBASE_DEBUG

CONFIG=-DMG_TLS=MG_TLS_OPENSSL
CONFIG+=-DMG_MAX_RECV_SIZE="(1024UL * 1024UL * 1024UL)"

PROFILE=
PROFILE+=-pg

LTO=-flto=auto

STANDARD=-std=c11

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
CFLAGS+=-pedantic
CFLAGS+=-O3
CFLAGS+=-march=native
CFLAGS+=-mtune=native
CFLAGS+=-fverbose-asm
CFLAGS+=-mmmx
CFLAGS+=-msse
CFLAGS+=-msse2
CFLAGS+=-msse3
CFLAGS+=-msse4
CFLAGS+=-msse4a
CFLAGS+=-msse4.1
CFLAGS+=-msse4.2
CFLAGS+=-mavx
#CFLAGS+=-mavxvnni
#CFLAGS+=-mavx2
#CFLAGS+=-mavx512f
#CFLAGS+=-mavx512cd
#CFLAGS+=-mavx512vl
#CFLAGS+=-mavx512bw
#CFLAGS+=-mavx512dq
#CFLAGS+=-mavx512ifma
#CFLAGS+=-mavx512vbmi
#CFLAGS+=-mavx512vbmi2
#CFLAGS+=-mavx512bf16
#CFLAGS+=-mavx512fp16
#CFLAGS+=-mavx512bitalg
#CFLAGS+=-mavx512vpopcntdq
#CFLAGS+=-mavx512vp2intersect
#CFLAGS+=-mavx512vnni
#CFLAGS+=-mavx10.1
#CFLAGS+=-mavx10.1-256
#CFLAGS+=-mavx10.1-512
CFLAGS+=-msha
#CFLAGS+=-msha512
CFLAGS+=-maes

LDFLAGS=$(DEBUG) $(PROFILE) $(LTO)
LDFLAGS+=-L/usr/local/lib
LDFLAGS+=-L/home/schulte/wcjson/lib

# ECPG - Embedded SQL in C
#   https://www.postgresql.org/docs/18/ecpg.html
LDFLAGS+=-lecpg

# ECPG - Embedded SQL in C
#   https://www.postgresql.org/docs/18/ecpg-pgtypes.html
LDFLAGS+=-lpgtypes

# LibJWT - The C JSON Webtoken Library
#   https://libjwt.io
#   https://github.com/benmcollins/libjwt/tree/v1.18.4
LDFLAGS+=-ljwt

# Wide Character JSON for C
#   https://wcjson.de
LDFLAGS+=-lwcjson

LDFLAGS+=-lm
LDFLAGS+=-lcrypto
LDFLAGS+=-lssl
#LDFLAGS+=-lstdthreads
#LDFLAGS+=-lpthread

YACCFLAGS=

HEADERS=abagnale.h 
HEADERS+=array.h
HEADERS+=config.h
HEADERS+=database.h
HEADERS+=exchange.h
HEADERS+=heap.h
HEADERS+=map.h
HEADERS+=math.h
HEADERS+=object.h
HEADERS+=proc.h
HEADERS+=queue.h
HEADERS+=string.h
HEADERS+=thread.h
HEADERS+=time.h

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
OBJS+=object.o
OBJS+=patterns.o
OBJS+=proc.o
OBJS+=queue.o
OBJS+=string.o
OBJS+=thread.o
OBJS+=time.o

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
FORMATSRC+=object.c
FORMATSRC+=proc.c
FORMATSRC+=queue.c
FORMATSRC+=string.c
FORMATSRC+=thread.c
FORMATSRC+=time.c

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

mongoose.o:
	$(CC) $(CFLAGS) -c -o $@ $<

.pgc.c:
	$(ECPG) $(INCLUDES) $<

.y.c:
	$(YACC) $(YACCFLAGS) $@ $<

$(OBJS): $(HEADERS)

