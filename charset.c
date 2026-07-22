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
#include "proc.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>

int mbsntowcsn(wchar_t *restrict const wc, size_t *restrict const wc_len,
               const char *restrict const mb, size_t mb_len) {
  size_t d_len = 0;
  size_t w_len = *wc_len;
  mbstate_t mbs = {0};

  for (size_t i = 0, len = 0; w_len-- != 0 && mb_len > 0;
       i += len, mb_len -= len, d_len++) {
    switch (len = mbrtowc(wc + d_len, mb + i, mb_len, &mbs)) {
    case 0:
      *wc_len = d_len;
      return 0;
    case (size_t)-1:
      werr("%s: Illegal byte sequence: %zu: 0x%.2hhx: %.*s\n", __func__, i,
           *(mb + i), (int)mb_len, mb);
      errno = EILSEQ;
      return -1;
    case (size_t)-2:
      werr("%s: Incomplete byte sequence: %zu: %.*s\n", __func__, i,
           (int)mb_len, mb);
      errno = EILSEQ;
      return -1;
    case (size_t)-3:
      // Not in ISO/IEC 9899:202y (en) - 7.33.6.4.3
      panic();
    }

    if (i > SIZE_MAX - len || mb_len < len || d_len > SIZE_MAX - 1) {
      errno = ERANGE;
      return -1;
    }
  }

  if (d_len >= *wc_len) {
    errno = ERANGE;
    return -1;
  }

  wc[d_len] = '\0';
  *wc_len = d_len;
  return 0;
}

int wcsntombsn(char *restrict mb, size_t *restrict const mb_len,
               const wchar_t *restrict const wc, size_t wc_len) {
  size_t d_len = 0;
  size_t m_len = *mb_len;
  mbstate_t mbs = {0};

  for (size_t i = 0, len = 0; m_len != 0 && wc_len-- != 0;
       i++, m_len -= len, d_len += len) {
    switch (len = wcrtomb(mb + d_len, *(wc + i), &mbs)) {
    case (size_t)-1:
      werr("%s: Illegal codepoint: %zu: 0x%" PRIxMAX ": %.*ls\n", __func__, i,
           (uintmax_t) * (wc + i), (int)wc_len + 1, wc);
      return -1;
    }

    if (*(wc + i) == L'\0') {
      *mb_len = d_len;
      return 0;
    }

    if (i > SIZE_MAX - 1 || m_len < len || d_len > SIZE_MAX - len) {
      errno = ERANGE;
      return -1;
    }
  }

  if (d_len >= *mb_len) {
    errno = ERANGE;
    return -1;
  }

  mb[d_len] = '\0';
  *mb_len = d_len;
  return 0;
}
