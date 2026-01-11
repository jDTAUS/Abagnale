/*	$OpenBSD: parse.y,v 1.299 2024/02/19 21:00:19 gilles Exp $	*/
/* $SchulteIT: config.y 15277 2025-11-05 02:51:30Z schulte $ */
/* $JDTAUS$ */

/*
 * Copyright (c) 2025 - 2026 Christian Schulte <cs@schulte.it>
 * Copyright (c) 2008 Gilles Chehade <gilles@poolp.org>
 * Copyright (c) 2008 Pierre-Yves Ritschard <pyr@openbsd.org>
 * Copyright (c) 2002, 2003, 2004 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2001 Markus Friedl.  All rights reserved.
 * Copyright (c) 2001 Daniel Hartmeier.  All rights reserved.
 * Copyright (c) 2001 Theo de Raadt.  All rights reserved.
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

%{
#include "abagnale.h"
#include "config.h"
#include "heap.h"
#include "patterns.h"
#include "proc.h"
#include "string.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#include <wcjson-document.h>

extern struct String *restrict const progname;

extern const struct Exchange *restrict const all_exchanges;
extern const size_t all_exchanges_nitems;

extern const struct Algorithm *restrict const all_algorithms;
extern const size_t all_algorithms_nitems;

extern const struct Numeric *restrict const zero;
extern const struct Numeric *restrict const one;
extern const struct Numeric *restrict const second_nanos;
extern const struct Numeric *restrict const minute_nanos;
extern const struct Numeric *restrict const hour_nanos;
extern const struct Numeric *restrict const day_nanos;
extern const struct Numeric *restrict const week_nanos;

static struct file {
  FILE *stream;
  struct String *restrict name;
  size_t idx;
  size_t ungetpos;
  size_t ungetsize;
  unsigned char *ungetbuf;
  int eof_reached;
  int lineno;
  int errors;
} *file;

struct file *pushfile(struct String *restrict const);
int popfile(void);

int yyparse(void);
int yylex(void);
int kw_cmp(const void *, const void *);
int lookup(char *);
int igetc(void);
int lgetc(int);
void lungetc(int);
int findeol(void);
int yyerror(const char *, ...)
  __attribute__((__format__(printf, 1, 2)))
  __attribute__((__nonnull__(1)));

struct sym {
  int used;
  int persist;
  struct String *nam;
  struct String *val;
};

int symset(struct String *restrict const, struct String *restrict const, int);
struct String *symget(const struct String *);

static struct Numeric *parse_nanos(const struct String *restrict const);

static struct Config *conf = NULL;
static struct ExchangeConfig *e_cnf = NULL;
static struct MarketConfig *m_cnf = NULL;

static int errors = 0;

static struct Map *restrict symbols;
static struct Array *restrict files;

typedef struct {
  union {
    struct Numeric *number;
    struct String *string;
    bool flag;
  } v;
  int lineno;
} YYSTYPE;

static void file_delete(void *restrict const v) {
  struct file *restrict const f = v;
  String_delete(f->name);
  heap_free(f->ungetbuf);
  heap_free(f);
}

static void sym_delete(void *restrict const v) {
  struct sym *restrict const s = v;
  String_delete(s->nam);
  String_delete(s->val);
  heap_free(s);
}
%}

%token AT CDP DATABASE DEMANDDURMAX DEMANDDURMIN DNSTO DNSV4 DNSV6 ERROR
%token EXCHANGE INCLUDE MARKET NOT PLOTS RATEMAX RATEMIN RETURN STOPLOSSDELAY
%token SUPPLYDURMAX SUPPLYDURMIN TAKELOSSDELAY TARGET TRADE USER USING
%token VOLATILITY WINDOW

%token <v.string> STRING
%token <v.number> NUMBER

%type <v.number> nanos
%type <v.flag> negate
%%

grammar : /* empty */
        | grammar '\n'
        | grammar include '\n'
        | grammar varset '\n'
        | grammar main '\n'
        | grammar exchange '\n'
        | grammar database '\n'
        | grammar trade '\n'
        | grammar error '\n' { file->errors++; };

include : INCLUDE STRING {
          struct file *nfile;

          if ((nfile = pushfile($2)) == NULL) {
            yyerror("failed to include file %s", String_chars($2));
            String_delete($2);
            YYERROR;
          }

          file = nfile;
          lungetc('\n');
        }
        ;

varset  : STRING '=' STRING {
          const char *restrict s = String_chars($1);
          while (*s++) {
            if (isspace((unsigned char)*s)) {
              yyerror("macro name cannot contain whitespace");
              String_delete($1);
              String_delete($3);
              YYERROR;
            }
          }
          if (symset($1, $3, 0) == -1) {
            werr("%s: %d: %s\n", __FILE__, __LINE__, __func__);
            String_delete($1);
            String_delete($3);
            fatal();
          }
        }
        ;

negate  : NOT {
          $$ = true;
        }
        | /* empty */ {
          $$ = false;
        }
        ;

main  : PLOTS STRING {
        if (conf->plts_dir != NULL) {
          yyerror("plots already specified\n");
          String_delete($2);
          YYERROR;
        }
        conf->plts_dir = $2;
      }
      | DNSV4 STRING {
        if (conf->dns_v4 != NULL) {
          yyerror("dns-v4 already specified\n");
          String_delete($2);
          YYERROR;
        }
        conf->dns_v4 = $2;
      }
      | DNSV6 STRING {
        if (conf->dns_v6 != NULL) {
          yyerror("dns-v6 already specified\n");
          String_delete($2);
          YYERROR;
        }
        conf->dns_v6 = $2;
      }
      | DNSTO nanos {
        if (conf->dns_to != NULL) {
          yyerror("dns-timeout already specified\n");
          Numeric_delete($2);
          YYERROR;
        }
        conf->dns_to = $2;
      }
      ;

conf_db : TARGET STRING {
          if (conf->db_tgt != NULL) {
            yyerror("target already specified\n");
            String_delete($2);
            YYERROR;
          }
          conf->db_tgt = $2;
        }
        | USER STRING {
          if (conf->db_usr != NULL) {
            yyerror("user already specified\n");
            String_delete($2);
            YYERROR;
          }
          conf->db_usr = $2;
        }
        ;

dbconf  : conf_db
        | conf_db dbconf
        ;

database  : DATABASE dbconf {
            if (conf->db_tgt == NULL)
              conf->db_tgt = String_cnew(ABAG_DATABASE_TARGET);

            if (conf->db_usr == NULL)
              conf->db_usr = String_cnew(ABAG_DATABASE_USER);

          }
          ;
 
nanos : STRING {
        $$ = parse_nanos($1);
        if ($$ == NULL) {
          yyerror("%s\n", String_chars($1));
          String_delete($1);
          YYERROR;
        }
        String_delete($1);
      }
      ;

exchangeconf  : conf_exchange
              | conf_exchange exchangeconf
              ;

conf_exchange : CDP STRING {
#define CDP_SIZE_MAX 4096
                if (e_cnf->jwt_kid != NULL || e_cnf->jwt_key != NULL) {
                  yyerror("cdp-api-key already specified\n");
                  String_delete($2);
                  YYERROR;
                }

                FILE *restrict const f = fopen(String_chars($2), "r");

                if (f == NULL) {
                  yyerror("%s: %s\n", String_chars($2), strerror(errno));
                  String_delete($2);
                  YYERROR;
                }

                int r = fseek(f, 0, SEEK_END);

                if (r == -1) {
                  yyerror("%s: %s\n", String_chars($2), strerror(errno));
                  String_delete($2);
                  fclose(f);
                  YYERROR;
                }

                long f_len = ftell(f);

                if (f_len == -1) {
                  yyerror("%s: %s\n", String_chars($2), strerror(errno));
                  String_delete($2);
                  fclose(f);
                  YYERROR;
                }

                if (f_len > CDP_SIZE_MAX) {
                  yyerror("%s: max file size %d exceeded\n", String_chars($2),
                          CDP_SIZE_MAX);
                  String_delete($2);
                  fclose(f);
                  YYERROR;
                }

                r = fseek(f, 0, SEEK_SET);

                if (r == -1) {
                  yyerror("%s: %s\n", String_chars($2), strerror(errno));
                  String_delete($2);
                  fclose(f);
                  YYERROR;
                }

                char *mb_buf = heap_calloc(f_len + 1, sizeof(char));
                size_t read = fread(mb_buf, f_len, 1, f);

                if (read != 1 || ferror(f)) {
                  yyerror("%s: %s\n", String_chars($2), strerror(errno));
                  String_delete($2);
                  heap_free(mb_buf);
                  fclose(f);
                  YYERROR;
                }

                fclose(f);

                size_t w_sz = mbstowcs(NULL, mb_buf, 0);

                if (w_sz == (size_t)-1) {
                  yyerror("%s: %s\n", String_chars($2), strerror(errno));
                  String_delete($2);
                  heap_free(mb_buf);
                  YYERROR;
                }

                wchar_t *w_buf = heap_calloc(w_sz + 1, sizeof(wchar_t));

                if (mbstowcs(w_buf, mb_buf, w_sz) == (size_t)-1) {
                  yyerror("%s: %s\n", String_chars($2), strerror(errno));
                  String_delete($2);
                  heap_free(mb_buf);
                  heap_free(w_buf);
                  YYERROR;
                }

                struct wcjson wcjson = WCJSON_INITIALIZER;
                struct wcjson_document doc = WCJSON_DOCUMENT_INITIALIZER;

                if (wcjsondocvalues(&wcjson, &doc, w_buf, w_sz) < 0) {
                  yyerror("%s: %s\n", String_chars($2), strerror(wcjson.errnum));
                  String_delete($2);
                  heap_free(mb_buf);
                  heap_free(w_buf);
                  YYERROR;
                }

                doc.values = heap_calloc(doc.v_nitems_cnt,
                  sizeof(struct wcjson_value));

                doc.v_nitems = doc.v_nitems_cnt;
                doc.v_next = 0;
                doc.strings = heap_calloc(doc.s_nitems_cnt, sizeof(wchar_t));
                doc.s_nitems = doc.s_nitems_cnt;
                doc.s_next = 0;
                  
                (void)wcjsondocvalues(&wcjson, &doc, w_buf, w_sz);

                if (wcjsondocstrings(&wcjson, &doc) < 0) {
                  yyerror("%s: %s\n", String_chars($2), strerror(wcjson.errnum));
                  String_delete($2);
                  heap_free(mb_buf);
                  heap_free(w_buf);
                  heap_free(doc.values);
                  heap_free(doc.strings);
                  YYERROR;
                }

                doc.mbstrings = heap_calloc(doc.mb_nitems_cnt, sizeof(char));
                doc.mb_nitems = doc.mb_nitems_cnt;
                doc.mb_next = 0;
                doc.esc = heap_calloc(doc.e_nitems_cnt, sizeof(wchar_t));
                doc.e_nitems = doc.e_nitems_cnt;
               
                if (wcjsondocmbstrings(&wcjson, &doc) < 0) {
                  yyerror("%s: %s\n", String_chars($2), strerror(wcjson.errnum));
                  String_delete($2);
                  heap_free(mb_buf);
                  heap_free(w_buf);
                  heap_free(doc.values);
                  heap_free(doc.strings);
                  heap_free(doc.mbstrings);
                  heap_free(doc.esc);
                  YYERROR;
                }
    
                struct wcjson_value *restrict const nm =
                  wcjson_object_get(&doc, doc.values, L"name", 4);

                struct wcjson_value *restrict const key =
                  wcjson_object_get(&doc, doc.values, L"privateKey", 10);

                if (nm == NULL || key == NULL) {
                  yyerror("%s: name or privateKey not found\n", String_chars($2));
                  String_delete($2);
                  heap_free(mb_buf);
                  heap_free(w_buf);
                  heap_free(doc.values);
                  heap_free(doc.strings);
                  heap_free(doc.mbstrings);
                  heap_free(doc.esc);
                  YYERROR;
                }

                e_cnf->jwt_kid = String_cnew(nm->mbstring);
                e_cnf->jwt_key = String_cnew(key->mbstring);

                String_delete($2);
                heap_free(mb_buf);
                heap_free(w_buf);
                heap_free(doc.values);
                heap_free(doc.strings);
                heap_free(doc.mbstrings);
                heap_free(doc.esc);
              }
              ;

exchange  : EXCHANGE STRING {
            bool e_found = false;
            for (size_t i = all_exchanges_nitems; i > 0; i--)
              if (String_equals($2, all_exchanges[i - 1].nm)) {
                e_found = true;
                break;
              }

            if (!e_found) {
              yyerror("%s: not found\n", String_chars($2));
              String_delete($2);
              YYERROR;
            }

            e_cnf = Map_get(conf->e_cnf, $2);
            if (e_cnf == NULL) {
              e_cnf = ExchangeConfig_new();
              Map_put(conf->e_cnf, $2, e_cnf);
            }

            String_delete($2);
          } exchangeconf {
            e_cnf = NULL;
          }
          ;

conf_market : negate STRING {
              const char *errstr = NULL;
              struct str_find sm[MAXCAPTURES] = {0};
              str_find("", String_chars($2), sm, MAXCAPTURES, &errstr);
              if (errstr != NULL) {
                yyerror("%s", errstr);
                String_delete($2);
                YYERROR;
              }
              struct Pattern *restrict const pat = Pattern_new();
              pat->pat = $2;
              pat->neg = $1;
              Array_add_tail(m_cnf->m_pats, pat);
            }
            ;

marketconf  : conf_market
            | conf_market marketconf
            ;

opt_trade : TAKELOSSDELAY nanos {
            if (m_cnf->tl_dlnanos != NULL) {
              yyerror("take-loss-delay already specified");
              Numeric_delete($2);
              YYERROR;
            }
            m_cnf->tl_dlnanos = $2;
          }
          | MARKET marketconf {
          }
          | SUPPLYDURMAX nanos {
            if (m_cnf->so_maxnanos != NULL) {
              yyerror("supply-duration-max already specified\n");
              Numeric_delete($2);
              YYERROR;
            }
            m_cnf->so_maxnanos = $2;
          }
          | SUPPLYDURMIN nanos {
            if (m_cnf->so_minnanos != NULL) {
              yyerror("supply-duration-min already specified\n");
              Numeric_delete($2);
              YYERROR;
            }
            m_cnf->so_minnanos = $2;
          }
          | RATEMAX NUMBER {
            if (m_cnf->sr_max != NULL) {
              yyerror("rate-max already specified\n");
              Numeric_delete($2);
              YYERROR;
            }
            m_cnf->sr_max = $2;

            if (Numeric_cmp(m_cnf->sr_max, zero) <= 0) {
              Numeric_delete(m_cnf->sr_max);
              yyerror("rate-max must be positive\n");
              YYERROR;
            }
          }
          | RATEMIN NUMBER {
            if (m_cnf->sr_min != NULL) {
              yyerror("rate-min alrady specified\n");
              Numeric_delete($2);
              YYERROR;
            }
            m_cnf->sr_min = $2;

            if (Numeric_cmp(m_cnf->sr_min, zero) <= 0) {
              Numeric_delete(m_cnf->sr_min);
              yyerror("rate-min must be positive\n");
              YYERROR;
            }
          }
          | DEMANDDURMAX nanos {
            if (m_cnf->bo_maxnanos != NULL) {
              yyerror("demand-duration-max already specified\n");
              Numeric_delete($2);
              YYERROR;
            }
            m_cnf->bo_maxnanos = $2;
          }
          | DEMANDDURMIN nanos {
            if (m_cnf->bo_minnanos != NULL) {
              yyerror("demand-duration-min already specified\n");
              Numeric_delete($2);
              YYERROR;
            }
            m_cnf->bo_minnanos = $2;
          }
          | STOPLOSSDELAY nanos {
            if (m_cnf->sl_dlnanos != NULL) {
              yyerror("stop-loss-delay already specified\n");
              Numeric_delete($2);
              YYERROR;
            }
            m_cnf->sl_dlnanos = $2;
          }
          | VOLATILITY NUMBER {
            if (m_cnf->v_pc != NULL) {
              yyerror("volatility already specified\n");
              Numeric_delete($2);
              YYERROR;
            }
            if (m_cnf->v_wnanos != NULL) {
              yyerror("volatility already specified\n");
              Numeric_delete($2);
              YYERROR;
            }
            m_cnf->v_pc = $2;

            if (Numeric_cmp(m_cnf->v_pc, zero) <= 0) {
              Numeric_delete(m_cnf->v_pc);
              yyerror("volatility must be positive\n");
              YYERROR;
            }
          }
          | VOLATILITY nanos {
            if (m_cnf->v_pc != NULL) {
              yyerror("volatility already specified\n");
              Numeric_delete($2);
              YYERROR;
            }
            if (m_cnf->v_wnanos != NULL) {
              yyerror("volatility already specified\n");
              Numeric_delete($2);
              YYERROR;
            }
            m_cnf->v_wnanos = $2;

            if (Numeric_cmp(m_cnf->v_wnanos, zero) <= 0) {
              Numeric_delete(m_cnf->v_wnanos);
              yyerror("volatility must be positive\n");
              YYERROR;
            }
          }
          ;

tradeopts : opt_trade tradeopts
          | /* empty */
          ;

conf_trade  : WINDOW nanos {
              if (m_cnf->wnanos != NULL) {
                yyerror("window allready specified\n");
                Numeric_delete($2);
                YYERROR;
              }
              m_cnf->wnanos = $2;
            }
            | RETURN NUMBER STRING {
              if (m_cnf->q_tgt != NULL) {
                yyerror("return already specified\n");
                Numeric_delete($2);
                String_delete($3);
                YYERROR;
              }
              m_cnf->q_tgt = $2;
              m_cnf->q_id = $3;

              if (Numeric_cmp(m_cnf->q_tgt, zero) <= 0) {
                Numeric_delete(m_cnf->q_tgt);
                String_delete(m_cnf->q_id);
                yyerror("return must be positive\n");
                YYERROR;
              }
            }
            | USING STRING {
              if (m_cnf->a_nm != NULL) {
                yyerror("using already specified\n");
                String_delete($2);
                YYERROR;
              }
              m_cnf->a_nm = $2;

              bool a_found = false;
              for (size_t i = all_algorithms_nitems; i > 0; i--)
                if (String_equals(m_cnf->a_nm, all_algorithms[i - 1].nm)) {
                  a_found = true;
                  break;
                }

              if (!a_found) {
                yyerror("%s not found\n", String_chars(m_cnf->a_nm));
                String_delete(m_cnf->a_nm);
                m_cnf->a_nm = NULL;
                YYERROR;
              }
            }
            ;

tradeconf : conf_trade
          | conf_trade tradeconf
          ;

trade : TRADE AT STRING {
        m_cnf  = MarketConfig_new();
        m_cnf->e_nm = $3;

        bool e_found = false;
        for (size_t i = all_exchanges_nitems; i > 0; i--)
          if (String_equals(m_cnf->e_nm, all_exchanges[i - 1].nm)) {
            e_found = true;
            break;
          }

        if (!e_found) {
          yyerror("%s not found\n", String_chars(m_cnf->e_nm));
          String_delete($3);
          MarketConfig_delete(m_cnf);
          m_cnf = NULL;
          YYERROR;
        }

        Array_add_head(conf->m_cnf, m_cnf);
      } tradeconf {
        if (m_cnf->wnanos == NULL || m_cnf->q_tgt == NULL || m_cnf->a_nm == NULL) {
          yyerror("return, using and window required\n");
          MarketConfig_delete(Array_remove_tail(conf->m_cnf));
          m_cnf = NULL;
          YYERROR;
        }
      } tradeopts {
        m_cnf = NULL;
      }
      ;

%%

struct keywords {
  const char *k_name;
  int k_val;
};

int yyerror(const char *fmt, ...) {
  va_list ap;
  char msg[1024];
  int r;

  file->errors++;
  va_start(ap, fmt);
  r = vsnprintf(msg, sizeof(msg), fmt, ap);
  if(r < 0 || (size_t)r >= sizeof(msg)) {
    werr("%s: %d: %s\n", __FILE__, __LINE__, __func__);
    fatal();
  }
  va_end(ap);
  werr("%s: %d: %s\n", String_chars(file->name), yylval.lineno, msg);
  return (0);
}

int kw_cmp(const void *k, const void *e) {
  return (strcmp(k, ((const struct keywords *)e)->k_name));
}

int lookup(char *s) {
  /* this has to be sorted always */
  static const struct keywords keywords[] = {
      {"at", AT},
      {"cdp-api-key", CDP},
      {"database", DATABASE},
      {"demand-duration-max", DEMANDDURMAX},
      {"demand-duration-min", DEMANDDURMIN},
      {"dns-timeout", DNSTO},
      {"dns-v4", DNSV4},
      {"dns-v6", DNSV6},
      {"exchange", EXCHANGE},
      {"include", INCLUDE},
      {"market", MARKET},
      {"not", NOT}, 
      {"plots", PLOTS},
      {"return", RETURN},
      {"stop-loss-delay", STOPLOSSDELAY},
      {"supply-duration-max", SUPPLYDURMAX},
      {"supply-duration-min", SUPPLYDURMIN},
      {"take-loss-delay", TAKELOSSDELAY},
      {"target", TARGET},
      {"tick-rate-max", RATEMAX},
      {"tick-rate-min", RATEMIN},
      {"trade", TRADE},
      {"user", USER},
      {"using", USING},
      {"volatility", VOLATILITY},
      {"window", WINDOW},
  };
  const struct keywords *p;

  p = bsearch(s, keywords, sizeof(keywords) / sizeof(keywords[0]),
              sizeof(keywords[0]), kw_cmp);

  if (p)
    return (p->k_val);
  else
    return (STRING);
}

#define START_EXPAND 1
#define DONE_EXPAND 2

static int expanding;

int igetc(void) {
  int c;

  while (1) {
    if (file->ungetpos > 0)
      c = file->ungetbuf[--file->ungetpos];
    else
      c = getc(file->stream);

    if (c == START_EXPAND)
      expanding = 1;
    else if (c == DONE_EXPAND)
      expanding = 0;
    else
      break;
  }
  return (c);
}

int lgetc(int quotec) {
  int c, next;

  if (quotec) {
    if ((c = igetc()) == EOF) {
      yyerror("reached end of file while parsing quoted string");
      if (file == Array_head(files) || popfile() == EOF)
        return (EOF);
      return (quotec);
    }
    return (c);
  }

  while ((c = igetc()) == '\\') {
    next = igetc();
    if (next != '\n') {
      c = next;
      break;
    }
    yylval.lineno = file->lineno;
    file->lineno++;
  }

  if (c == EOF) {
    /*
     * Fake EOL when hit EOF for the first time. This gets line
     * count right if last line in included file is syntactically
     * invalid and has no newline.
     */
    if (file->eof_reached == 0) {
      file->eof_reached = 1;
      return ('\n');
    }
    while (c == EOF) {
      if (file == Array_head(files) || popfile() == EOF)
        return (EOF);
      c = igetc();
    }
  }
  return (c);
}

void lungetc(int c) {
  if (c == EOF)
    return;

  if (file->ungetpos >= file->ungetsize) {
    // file->ungetsize * 2 <= SIZE_MAX
    // =>file->ungetsize <= SIZE_MAX / 2
    // =>2 <= SIZE_MAX / file->ungetsize
    if (file->ungetsize > SIZE_MAX / 2
        || (file->ungetsize > 0 && 2 > SIZE_MAX / file->ungetsize)) {
      werr("%s: %d: %s\n", __FILE__, __LINE__, __func__);
      fatal();
    }
    void *p = realloc(file->ungetbuf, file->ungetsize * 2);
    if (p == NULL) {
      werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, strerror(errno));
      fatal();
    }
    file->ungetbuf = p;
    file->ungetsize *= 2;
  }
  file->ungetbuf[file->ungetpos++] = c;
}

int findeol(void) {
  int c;

  /* skip to either EOF or the first real EOL */
  while (1) {
    c = lgetc(0);
    if (c == '\n') {
      file->lineno++;
      break;
    }
    if (c == EOF)
      break;
  }
  return (ERROR);
}

int yylex(void) {
  char buf[8096];
  char *p;
  struct String *nam;
  struct String *val;
  int quotec, next, last, c;
  int token;

top:
  p = buf;
  while ((c = lgetc(0)) == ' ' || c == '\t')
    ; /* nothing */

  yylval.lineno = file->lineno;
  if (c == '#')
    while ((c = lgetc(0)) != '\n' && c != EOF)
      ; /* nothing */
  if (c == '$' && !expanding) {
    while (1) {
      if ((c = lgetc(0)) == EOF)
        return (0);

      if (p + 1 >= buf + sizeof(buf) - 1) {
        yyerror("string too long");
        return (findeol());
      }
      if (isalnum(c) || c == '_') {
        *p++ = c;
        continue;
      }
      *p = '\0';
      lungetc(c);
      break;
    }

    nam = String_cnew(buf);
    val = symget(nam);
    String_delete(nam);

    if (val == NULL) {
      yyerror("macro '%s' not defined", buf);
      return (findeol());
    }

    lungetc(DONE_EXPAND);
    for (size_t i = String_length(val); i > 0; i--)
      lungetc((unsigned char)String_chars(val)[i - 1]);

    lungetc(START_EXPAND);
    goto top;
  }

  switch (c) {
  case '\'':
  case '"':
    quotec = c;
    while (1) {
      if ((c = lgetc(quotec)) == EOF)
        return (0);
      if (c == '\n') {
        file->lineno++;
        continue;
      } else if (c == '\\') {
        if ((next = lgetc(quotec)) == EOF)
          return (0);
        if (next == quotec || next == ' ' || next == '\t')
          c = next;
        else if (next == '\n') {
          file->lineno++;
          continue;
        } else
          lungetc(next);
      } else if (c == quotec) {
        *p = '\0';
        break;
      } else if (c == '\0') {
        yyerror("syntax error");
        return (findeol());
      }
      if (p + 1 >= buf + sizeof(buf) - 1) {
        yyerror("string too long");
        return (findeol());
      }
      *p++ = c;
    }
    yylval.v.string = String_cnew(buf);
    return (STRING);
  }

#define allowed_to_end_number(x)  (isspace(x)) 

  if (c == '-' || isdigit(c)) {
    last = '\0';
    do {
      *p++ = c;
      if ((size_t)(p - buf) >= sizeof(buf)) {
        yyerror("string too long");
        return (findeol());
      }
      if(last == c)
        break;
      if(c == '.')
        last = c;
    } while ((c = lgetc(0)) != EOF && (isdigit(c) || c == '.'));
    lungetc(c);
    if (p == buf + 1 && buf[0] == '-')
      goto nodigits;
    if (c == EOF || allowed_to_end_number(c)) {
      *p = '\0';
      yylval.v.number = Numeric_from_char(buf);
      if (yylval.v.number == NULL) {
        yyerror("invalid number: \"%s\"", buf);
        return (findeol());
      }
      return (NUMBER);
    } else {
    nodigits:
      while (p > buf + 1)
        lungetc((unsigned char)*--p);
      c = (unsigned char)*--p;
      if (c == '-')
        return (c);
    }
  }

#define allowed_in_string(x) (isalnum(x) || (ispunct(x) && x != '=' && x != '#'))

  if (allowed_in_string(c)) {
    do {
      *p++ = c;
      if ((size_t)(p - buf) >= sizeof(buf)) {
        yyerror("string too long");
        return (findeol());
      }
    } while ((c = lgetc(0)) != EOF && (allowed_in_string(c)));
    lungetc(c);
    *p = '\0';
    if ((token = lookup(buf)) == STRING)
      yylval.v.string = String_cnew(buf);
    return (token);
  }
  if (c == '\n') {
    yylval.lineno = file->lineno;
    file->lineno++;
  }
  if (c == EOF)
    return (0);
  return (c);
}

struct file *pushfile(struct String *restrict const name) {
  struct file *nfile;

  nfile = heap_calloc(1, sizeof(struct file));
  nfile->name = name;
  if ((nfile->stream = fopen(String_chars(name), "r")) == NULL) {
    werr("%s: %s: %s\n", String_chars(progname), String_chars(name),
      strerror(errno));
    heap_free(nfile);
    return (NULL);
  }
  nfile->lineno = Array_size(files) == 0 ? 1 : 0;
  nfile->ungetsize = 16;
  nfile->ungetbuf = heap_calloc(nfile->ungetsize, sizeof(unsigned char));
  nfile->idx = Array_size(files);
  Array_add_tail(files, nfile);
  return (nfile);
}

int popfile(void) {
  struct file *restrict const prev = file->idx > 0
    ? Array_items(files)[file->idx - 1]
    : NULL;

  if (prev != NULL)
    prev->errors += file->errors;

  Array_remove_idx(files, file->idx);
  fclose(file->stream);
  String_delete(file->name);
  heap_free(file->ungetbuf);
  heap_free(file);

  file = prev;
  return (file ? 0 : EOF);
}

int config_fparse(struct Config *const x_conf,
                  const char *restrict const filename) {
  conf = x_conf;
  errors = 0;
  struct String *restrict const f_nm = String_cnew(filename);
  void **items;

  if ((file = pushfile(f_nm)) == NULL) {
    String_delete(f_nm);
    return (-1);
  }

  yyparse();
  errors = file->errors;
  popfile();


  struct MapIterator *restrict const it = MapIterator_new(symbols);
  while(MapIterator_next(it)) {
    const struct sym *restrict const sym = MapIterator_value(it);
    if (!sym->used)
      werr("%s: macro '%s' not used\n", String_chars(progname),
        String_chars(sym->nam));
    if (!sym->persist)
      sym_delete(MapIterator_remove(it));
  }
  MapIterator_delete(it);

  if (errors)
    return (-1);

  items = Array_items(conf->m_cnf);
  for (size_t i = Array_size(conf->m_cnf); i > 0; i--) {
    m_cnf = items[i - 1];
    if (conf->wnanos_max == NULL
        || Numeric_cmp(conf->wnanos_max, m_cnf->wnanos) < 0)
      conf->wnanos_max = m_cnf->wnanos;

    if (Map_get(conf->e_cnf, m_cnf->e_nm) == NULL) {
      werr("%s: %s: Exchange configuration required\n", String_chars(progname),
        String_chars(m_cnf->e_nm));
      errors++;
    }
  }

  if (conf->db_tgt == NULL || conf->db_usr == NULL) {
    werr("%s: Database target and user required\n", String_chars(progname));
    errors++;
  }

  if (errors)
    return (-1);

  return (0);
}

int symset(struct String *restrict const nam, struct String *restrict const val,
           int persist) {
  struct sym *restrict sym = Map_get(symbols, nam);

  if (sym == NULL) {
    sym = heap_calloc(1, sizeof(*sym));
    sym->nam = nam;
    sym->val = val;
    sym->used = 0;
    sym->persist = persist;

    Map_put(symbols, nam, sym);
    return (0);
  }

  if (sym->persist)
    return (0);

  String_delete(sym->nam);
  String_delete(sym->val);
  sym->nam = nam;
  sym->val = val;
  Map_put(symbols, nam, sym);
  return (0);
}

int config_symset(char *restrict const s) {
  char *restrict val;
  int r;

  if ((val = strrchr(s, '=')) == NULL)
    return (-1);

  *val = '\0';
  r = symset(String_cnew(s), String_cnew(val + 1), 1);
  *val = '=';
  return r;
}

struct String *symget(const struct String *restrict const nam) {
  struct sym *restrict const sym = Map_get(symbols, nam);
  
  if(sym != NULL) {
    sym->used = 1;
    return sym->val;
  }

  return (NULL);
}

static struct Numeric *parse_nanos(const struct String *restrict const str) {
  struct Numeric *restrict r = NULL;
  const struct Numeric *restrict f = one;
  struct Numeric *restrict r0 = NULL;

  if (String_length(str) > 0) {
    if (isdigit(String_chars(str)[String_length(str) - 1])) {
      r = Numeric_from_char(String_chars(str));

      if (r == NULL) {
        werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__, String_chars(str));
        fatal();
      }

      if (Numeric_cmp(r, zero) <= 0) {
        Numeric_delete(r);
        r = NULL;
      }
    } else {
      switch (String_chars(str)[String_length(str) - 1]) {
      case 's':
        f = second_nanos;
        break;
      case 'm':
        f = minute_nanos;
        break;
      case 'h':
        f = hour_nanos;
        break;
      case 'd':
        f = day_nanos;
        break;
      case 'w':
        f = week_nanos;
        break;
      default:
        return NULL;
      }

      if (String_length(str) > 1) {
        struct String *restrict const s =
          String_new(str, 0, String_length(str) - 1);

        r = Numeric_from_char(String_chars(s));
        String_delete(s);

        if (r == NULL) {
          werr("%s: %d: %s: %s\n", __FILE__, __LINE__, __func__,
               String_chars(str));
          fatal();
        }

        r0 = Numeric_mul(r, f);
        Numeric_delete(r);
        r = r0;

        if (Numeric_cmp(r, zero) <= 0) {
          Numeric_delete(r);
          r = NULL;
        }
      }
    }
  }

  return r;
}

void config_init(void) {
  symbols = Map_new(64);
  files = Array_new(64);
}

void config_destroy(void) {
  Array_delete(files, file_delete);
  Map_delete(symbols, sym_delete);
}

struct Config *Config_new(void) {
  struct Config *restrict const c = heap_calloc(1, sizeof(struct Config));
  c->db_tgt = NULL;
  c->db_usr = NULL;
  c->dns_v4 = NULL;
  c->dns_v6 = NULL;
  c->dns_to = NULL;
  c->plts_dir = NULL;
  c->wnanos_max = NULL;
  c->e_cnf = Map_new(4);
  c->m_cnf = Array_new(16);
  return c;
}

void Config_delete(void *restrict const c) {
  if (c == NULL)
    return;

  struct Config *restrict const cfg = c;
  String_delete(cfg->db_tgt);
  String_delete(cfg->db_usr);
  String_delete(cfg->dns_v4);
  String_delete(cfg->dns_v6);
  Numeric_delete(cfg->dns_to);
  String_delete(cfg->plts_dir);
  cfg->wnanos_max = NULL;
  Map_delete(cfg->e_cnf, ExchangeConfig_delete);
  Array_delete(cfg->m_cnf, MarketConfig_delete);
  heap_free(cfg);
}

struct Pattern *Pattern_new(void) {
 return heap_calloc(1, sizeof(struct Pattern));
}

void Pattern_delete(void *restrict const p) {
  heap_free(p);
}

struct MarketConfig *MarketConfig_new(void) {
  struct MarketConfig *restrict const c =
    heap_calloc(1, sizeof(struct MarketConfig));

  c->e_nm = NULL;
  c->a_nm = NULL;
  c->m_pats = Array_new(4);
  c->q_tgt = NULL;
  c->q_id = NULL;
  c->v_pc = NULL;
  c->v_wnanos = NULL;
  c->wnanos = NULL;
  c->sr_min = NULL;
  c->sr_max = NULL;
  c->bo_minnanos = NULL;
  c->bo_maxnanos = NULL;
  c->so_minnanos = NULL;
  c->so_maxnanos = NULL;
  c->sl_dlnanos = NULL;
  c->tl_dlnanos = NULL;
  return c;
}

void MarketConfig_delete(void *restrict const c) {
  if (c == NULL)
    return;

  struct MarketConfig *restrict const cfg = c;
  String_delete(cfg->e_nm);
  String_delete(cfg->a_nm);
  Array_delete(cfg->m_pats, Pattern_delete);
  Numeric_delete(cfg->q_tgt);
  String_delete(cfg->q_id);
  Numeric_delete(cfg->v_pc);
  Numeric_delete(cfg->v_wnanos);
  Numeric_delete(cfg->wnanos);
  Numeric_delete(cfg->sr_min);
  Numeric_delete(cfg->sr_max);
  Numeric_delete(cfg->bo_minnanos);
  Numeric_delete(cfg->bo_maxnanos);
  Numeric_delete(cfg->so_minnanos);
  Numeric_delete(cfg->so_maxnanos);
  Numeric_delete(cfg->sl_dlnanos);
  Numeric_delete(cfg->tl_dlnanos);
}

bool MarketConfig_match(const struct MarketConfig *restrict const c,
                        const struct String *restrict const m) {
  bool market = true;
  struct str_find sm[MAXCAPTURES] = {0};
  const char *errstr = NULL;
  void **items = Array_items(c->m_pats);
  const char *mk = String_chars(m);

  for (size_t i = Array_size(c->m_pats); i > 0 && market; i--) {
    market = str_find(mk, String_chars(((struct Pattern *)items[i - 1])->pat),
                      sm, MAXCAPTURES, &errstr)
             ? !((struct Pattern *)items[i - 1])->neg
             : ((struct Pattern *)items[i - 1])->neg;

    if (errstr != NULL) {
      werr("%s: %s: %s\n",
           mk, String_chars(((struct Pattern *)items[i - 1])->pat), errstr);
      market = false;
    }
  }

  return market;
}

