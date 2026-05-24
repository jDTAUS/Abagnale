/*
 * Copyright (c) 2025 Christian Schulte <cs@schulte.it>
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

/*
 * $JDTAUS$
 * Origin: https://github.com/wcjson/wcjson v0.35
 * Modifications: None.
 */

/**
 * @file wcjson.h
 * @brief C API header file.
 */
#ifndef WCJSON_WCJSON_H
#define WCJSON_WCJSON_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_ABAG_HOST_H
#include "host.h"
#endif

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

#ifdef HAVE_WCJSON_HOST_H
#include <wcjson-host.h>
#endif

#ifndef WCJSON_EXPORT
#define WCJSON_EXPORT
#endif

#ifndef WCJSON_NO_EXPORT
#define WCJSON_NO_EXPORT
#endif

#ifndef WCJSON_DEPRECATED
#define WCJSON_DEPRECATED
#endif

#ifndef WCJSON_DEPRECATED_EXPORT
#define WCJSON_DEPRECATED_EXPORT
#endif

#ifndef WCJSON_DEPRECATED_NO_EXPORT
#define WCJSON_DEPRECATED_NO_EXPORT
#endif

/**
 * Maximum number of wide characters escaping of a single wide character may
 * require. The worst case currently is when a single wide character needs to
 * be escaped to UTF 16 surrogates.
 */
#define WCJSON_ESCAPE_MAX 12

/** Status codes. */
enum wcjson_status {
  /** Operation completed successfully. */
  WCJSON_OK,
  /** Operation aborted due to an error - errnum is indicating the error. */
  WCJSON_ABORT_ERROR,
  /** Operation aborted due to invalid JSON text. */
  WCJSON_ABORT_INVALID,
  /** Operation aborted due to an unexpected end of input. */
  WCJSON_ABORT_END_OF_INPUT,
};

/** Library context. */
struct wcjson {
  /** Context status. */
  enum wcjson_status status;
  /** Error number in case of WCJSON_ABORT_ERROR. */
  int errnum;
};

/** Macro expanding to a rvalue expression initializing a wcjson structure. */
#define WCJSON_INITIALIZER                                                     \
  {                                                                            \
      .status = WCJSON_OK,                                                     \
      .errnum = 0,                                                             \
  }

/** Parser callback functions. */
struct wcjson_ops {
  /**
   * Called whenever an opening '{' character of a JSON object has been scanned.
   * @param ctx Library context.
   * @param doc Document being parsed - maybe NULL.
   * @param parent Node containing the object - maybe NULL.
   * @return Result node - maybe NULL.
   */
  void *(*object_start)(struct wcjson *ctx, void *doc, void *parent);
  /**
   * Called whenever a JSON object's key/value pair has been scanned.
   * @param ctx Library context.
   * @param doc Document being parsed - maybe NULL.
   * @param obj Node of the object being parsed - maybe NULL.
   * @param key Key of the pair to add to the object - maybe NULL.
   * @param value Value of the pair to add to the object - maybe NULL.
   */
  void (*object_add)(struct wcjson *ctx, void *doc, void *obj, void *key,
                     void *value);
  /**
   * Called whenever a closing '}' character of a JSON object has been scanned.
   * @param ctx Library context.
   * @param doc Document being parsed - maybe NULL.
   * @param obj Node of the object being parsed - maybe NULL.
   */
  void (*object_end)(struct wcjson *ctx, void *doc, void *obj);
  /**
   * Called whenever an opening '[' character of a JSON array has been scanned.
   * @param ctx Library context.
   * @param doc Document being parsed - maybe NULL.
   * @param parent Node containing the array - maybe NULL.
   * @return Result node - maybe NULL.
   */
  void *(*array_start)(struct wcjson *ctx, void *doc, void *parent);
  /**
   * Called whenever a value of a JSON array has been scanned.
   * @param ctx Library context.
   * @param doc Documemt being parsed - maybe NULL.
   * @param arr Node of the array being parsed - maybe NULL.
   * @param value Value to add to the array - maybe NULL.
   */
  void (*array_add)(struct wcjson *ctx, void *doc, void *arr, void *value);
  /**
   * Called whenever the closing ']' character of a JSON array has been scanned.
   * @param ctx Library context.
   * @param doc Document being parsed - maybe NULL.
   * @param arr Node of the array being parsed - maybe NULL.
   */
  void (*array_end)(struct wcjson *ctx, void *doc, void *arr);
  /**
   * Called whenever a JSON string has been scanned.
   * @param ctx Library context.
   * @param doc Document being parsed - maybe NULL.
   * @param str String having been scanned.
   * @param len Number of characters of that string.
   * @param escaped Flag indicating the string contains escape sequences.
   * @return Result node - maybe NULL.
   */
  void *(*string_value)(struct wcjson *ctx, void *doc, const wchar_t *str,
                        const size_t len, const bool escaped);
  /**
   * Called whenever a JSON number has been scanned.
   * @param ctx Library context.
   * @param doc Document being parsed - maybe NULL.
   * @param num Number having been scanned.
   * @param len Number of characters of that number.
   * @return Result node - maybe NULL.
   */
  void *(*number_value)(struct wcjson *ctx, void *doc, const wchar_t *num,
                        const size_t len);
  /**
   * Called whenever a JSON boolean has been scanned.
   * @param ctx Library context.
   * @param doc Document being parsed - maybe NULL.
   * @param value Boolean having been scanned.
   * @return Result node - maybe NULL.
   */
  void *(*bool_value)(struct wcjson *ctx, void *doc, const bool value);
  /**
   * Called whenever a JSON null value has been scanned.
   * @param ctx Library context.
   * @param doc Document being parsed - maybe NULL.
   * @return Result node - maybe NULL.
   */
  void *(*null_value)(struct wcjson *ctx, void *doc);
};

/**
 * Processes JSON text from wide characters.
 * @param ctx Library context.
 * @param ops Callback functions to be called - maybe NULL.
 * @param doc Document being parsed - maybe NULL.
 * @param txt The wide characters to process.
 * @param len The number of wide characters to process.
 * @return 0 on success, <0 on failure.
 */
WCJSON_EXPORT int wcjson(struct wcjson *ctx, const struct wcjson_ops *ops,
                         void *doc, const wchar_t *txt, const size_t len);

/**
 * Encodes wide characters to a JSON string.
 * @param s The wide characters to encode.
 * @param s_len The number of wide characters to encode.
 * @param d The destination to write encoded wide characters to.
 * @param d_lenp Pointer to the number of wide characters available in d,
 * number of wide characters written to d on return.
 * @return 0 on success, <0 on failure.
 */
WCJSON_EXPORT int wctowcjsons(const wchar_t *s, size_t s_len, wchar_t *d,
                              size_t *d_lenp);

/**
 * Encodes wide characters to a JSON 7bit ASCII string,
 * @param s The wide characters to encode.
 * @param s_len The number of wide characters to encode.
 * @param d The destination to write encoded characters to.
 * @param d_lenp Pointer to the number of wide characters available in d,
 * number of wide characters written to d on return.
 * @return 0 on success, <0 on failure.
 */
WCJSON_EXPORT int wctoascjsons(const wchar_t *s, size_t s_len, wchar_t *d,
                               size_t *d_lenp);

/**
 * Decodes a JSON string to wide characters.
 * @param s The JSON string to decode.
 * @param s_len The number of wide characters to decode.
 * @param d The destination to write decoded wide characters to.
 * @param d_lenp Pointer to the number of wide characters available in d,
 * number of wide characters written to d on return.
 * @return 0 on success, <0 on failure.
 */
WCJSON_EXPORT int wcjsonstowc(const wchar_t *s, size_t s_len, wchar_t *d,
                              size_t *d_lenp);

#ifdef __cplusplus
}
#endif
#endif
