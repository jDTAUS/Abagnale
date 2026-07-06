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
 * $JDTAUS: wcjson-document.h 9502 2026-06-11 04:46:18Z schulte $
 * Origin: https://github.com/wcjson/wcjson v0.35
 * Modifications: Changed to include wcjson.h from current directory.
 */

/**
 * @file wcjson-document.h
 * @brief Document C API header file.
 */
#ifndef WCJSON_DOCUMENT_H
#define WCJSON_DOCUMENT_H

#ifdef HAVE_HOST_H
#include "host.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <wchar.h>

#include "wcjson.h"

/** JSON value. */
struct wcjson_value {
  unsigned is_null : 1;
  unsigned is_boolean : 1;
  unsigned is_true : 1;
  unsigned is_string : 1;
  unsigned is_number : 1;
  unsigned is_object : 1;
  unsigned is_array : 1;
  unsigned is_pair : 1;
  const wchar_t *string;
  size_t s_len;
  const char *mbstring;
  size_t mb_len;
  size_t idx;
  size_t head_idx;
  size_t tail_idx;
  size_t prev_idx;
  size_t next_idx;
};

/** JSON document. */
struct wcjson_document {
  struct wcjson_value *values;
  size_t v_nitems;
  size_t v_nitems_cnt;
  size_t v_next;
  wchar_t *strings;
  size_t s_nitems;
  size_t s_nitems_cnt;
  size_t s_next;
  char *mbstrings;
  size_t mb_nitems;
  size_t mb_nitems_cnt;
  size_t mb_next;
  wchar_t *esc;
  size_t e_nitems;
  size_t e_nitems_cnt;
};

/**
 * Macro expanding to a rvalue expression initializing a wcjson_document
 * structure.
 */
#define WCJSON_DOCUMENT_INITIALIZER                                            \
  {                                                                            \
      .values = NULL,                                                          \
      .v_nitems = 0,                                                           \
      .v_nitems_cnt = 0,                                                       \
      .v_next = 0,                                                             \
      .strings = NULL,                                                         \
      .s_nitems = 0,                                                           \
      .s_nitems_cnt = 0,                                                       \
      .mbstrings = NULL,                                                       \
      .mb_nitems = 0,                                                          \
      .mb_nitems_cnt = 0,                                                      \
      .mb_next = 0,                                                            \
      .esc = NULL,                                                             \
      .e_nitems = 0,                                                           \
      .e_nitems_cnt = 0,                                                       \
  }

/**
 * Creates a null value in a document.
 * @param doc The document to create the value in.
 * @return A null value or NULL if the document cannot provide more values.
 */
WCJSON_EXPORT struct wcjson_value *
wcjson_value_null(struct wcjson_document *doc);

/**
 * Creates a boolean value in a document.
 * @param doc The document to create the value in.
 * @param val The value of the boolean value.
 * @return A boolean value or NULL if the document cannot provide more values.
 */
WCJSON_EXPORT struct wcjson_value *
wcjson_value_bool(struct wcjson_document *doc, const bool val);

/**
 * Creates a string value in a document.
 * @param doc The document to create the value in.
 * @param val The value of the string value.
 * @param len The number of characters of the string excluding the terminating
 * zero character.
 * @return A string value or NULL if the document cannot provide more values.
 */
WCJSON_EXPORT struct wcjson_value *
wcjson_value_string(struct wcjson_document *doc, const wchar_t *val,
                    const size_t len);

/**
 * Creates a number value in a document.
 * @param doc The document to create the value in.
 * @param val The value of the number value.
 * @param len The number of characters of the number excluding the terminating
 * zero character.
 * @return A number value or NULL if the document cannot provide more values.
 */
WCJSON_EXPORT struct wcjson_value *
wcjson_value_number(struct wcjson_document *doc, const wchar_t *val,
                    const size_t len);

/**
 * Creates an object value in a document.
 * @param doc The document to create the value in.
 * @return An object value or NULL if the document cannot provide more values.
 */
WCJSON_EXPORT struct wcjson_value *
wcjson_value_object(struct wcjson_document *doc);

/**
 * Creates an array value in a document.
 * @param doc The document to create the value in.
 * @return An array value or NULL if the document cannot provide more values.
 */
WCJSON_EXPORT struct wcjson_value *
wcjson_value_array(struct wcjson_document *doc);

/**
 * Accessor macro to get the head value of the child value list of a value.
 * @param d The document to query for values.
 * @param v The value to query.
 */
#define wcjson_value_head(d, v)                                                \
  ((v)->head_idx == 0 ? NULL : &(d)->values[(v)->head_idx])

/**
 * Accessor macro to get the next value of the child value list of a value.
 * @param d The document to query for values.
 * @param v The value to query.
 */
#define wcjson_value_next(d, v)                                                \
  ((v)->next_idx == 0 ? NULL : &(d)->values[(v)->next_idx])

/**
 * Accessor macro to get the tail value of the child value list of a value.
 * @param d The document to query for values.
 * @param v The value to query.
 */
#define wcjson_value_tail(d, v)                                                \
  ((v)->tail_idx == 0 ? NULL : &(d)->values[(v)->tail_idx])

/**
 * Accessor macro to get the previous value of the child value list of a value.
 * @param d The document to query for values.
 * @param v The value to query.
 */
#define wcjson_value_prev(d, v)                                                \
  ((v)->prev_idx == 0 ? NULL : &(d)->values[(v)->prev_idx])

/**
 * Iterator macro to iterate the child value list of a value.
 * @param lval The lvalue to assign values to.
 * @param d The document to query for values.
 * @param v The value to iterate.
 */
#define wcjson_value_foreach(lval, d, v)                                       \
  for ((lval) = wcjson_value_head((d), (v)); (lval) != NULL;                   \
       (lval) = wcjson_value_next((d), (lval)))

/**
 * Adds a value to the head of the child value list of an array.
 * @param doc The document containing the array and the value.
 * @param arr The array to add a value to.
 * @param val The value to add.
 * @return 0 on success or <0 if adding fails.
 */
WCJSON_EXPORT int wcjson_array_add_head(const struct wcjson_document *doc,
                                        struct wcjson_value *arr,
                                        struct wcjson_value *val);

/**
 * Adds a value to the tail of the child value list of an array.
 * @param doc The document containing the array and the value.
 * @param arr The array to add a value to.
 * @param val The value to add.
 * @return 0 on success or <0 if adding fails.
 */
WCJSON_EXPORT int wcjson_array_add_tail(const struct wcjson_document *doc,
                                        struct wcjson_value *arr,
                                        struct wcjson_value *val);

/**
 * Gets a value from an array.
 * @param doc The document containing the array.
 * @param arr The array to get a value from.
 * @param idx The index of the value to get.
 * @return The value at idx from arr or NULL if no such value is found.
 */
WCJSON_EXPORT struct wcjson_value *
wcjson_array_get(const struct wcjson_document *doc,
                 const struct wcjson_value *arr, const size_t idx);

/**
 * Removes a value from an array.
 * @param doc The document containing the array.
 * @param arr The array to remove a value from.
 * @param idx The index of the value to remove.
 * @return The removed value at idx from arr or NULL if no such value is found.
 */
WCJSON_EXPORT struct wcjson_value *
wcjson_array_remove(const struct wcjson_document *doc, struct wcjson_value *arr,
                    const size_t idx);

/**
 * Adds a key value pair to the head of the child value list of an object.
 * @param doc The document containing the object and the value.
 * @param obj The object to add a key value pair to.
 * @param key The key of the key value pair to add.
 * @param len The number of characters of the key excluding the terminating
 * zero character.
 * @param val The value of the key value pair to add.
 * @return 0 on success or <0 if adding fails.
 */
WCJSON_EXPORT int wcjson_object_add_head(struct wcjson_document *doc,
                                         struct wcjson_value *obj,
                                         const wchar_t *key, const size_t len,
                                         const struct wcjson_value *val);

/**
 * Adds a key value pair to the tail of the child value list of an object.
 * @param doc The document containing the object and the value.
 * @param obj The object to add a key value pair to.
 * @param key The key of the key value pair to add.
 * @param len The number of characters of the key excluding the terminating
 * zero character.
 * @param val The value of the key value pair to add.
 * @return 0 on success or <0 if adding fails.
 */
WCJSON_EXPORT int wcjson_object_add_tail(struct wcjson_document *doc,
                                         struct wcjson_value *obj,
                                         const wchar_t *key, const size_t len,
                                         const struct wcjson_value *val);

/**
 * Removes a key value pair from an object.
 * @param doc The document containing the object.
 * @param obj The object to remove a key value pair from.
 * @param key The key of the key value pair to remove.
 * @param len The number of characters of the key excluding the terminating
 * zero character.
 * @return The first value matching key or NULL if no such value is found.
 */
WCJSON_EXPORT struct wcjson_value *
wcjson_object_remove(const struct wcjson_document *doc,
                     struct wcjson_value *obj, const wchar_t *key,
                     const size_t len);

/**
 * Gets a value from an object.
 * @param doc The document containing the object.
 * @param obj The object to query.
 * @param key The key to query that object for.
 * @param len The number of characters of the key excluding the terminating
 * zero character.
 * @return The first value matching key or NULL if no such value is found.
 */
WCJSON_EXPORT struct wcjson_value *
wcjson_object_get(const struct wcjson_document *doc,
                  const struct wcjson_value *obj, const wchar_t *key,
                  const size_t len);

/**
 * Adds a wide string to a document.
 * @param doc The document to add a wide string to.
 * @param s The string to add.
 * @param len The number of characters of the string excluding the terminating
 * zero character.
 * @return A pointer to the string or NULL if the document cannot hold the
 * string.
 */
WCJSON_EXPORT wchar_t *wcjson_document_string(struct wcjson_document *doc,
                                              const wchar_t *s,
                                              const size_t len);

/**
 * Adds a multibyte string to a document.
 * @param doc The document to add a multibyte string to.
 * @param s The string to add.
 * @param len The number of characters of the string excluding the terminating
 * zero character.
 * @return A pointer to the string or NULL if the document cannot holds the
 * string.
 */
WCJSON_EXPORT char *wcjson_document_mbstring(struct wcjson_document *doc,
                                             const char *s, const size_t len);

/**
 * Deserializes JSON text to populate a document.
 * @param ctx Library context.
 * @param doc Document to populate.
 * @param txt JSON text to deserialize.
 * @param len Number of characters to deserialize.
 * @return 0 on success or <0 on failure.
 */
WCJSON_EXPORT int wcjsondocvalues(struct wcjson *ctx,
                                  struct wcjson_document *doc,
                                  const wchar_t *txt, const size_t len);

/**
 * Decodes all string and number values in a document by unapplying JSON
 * escaping rules and adding terminating zero characters.
 * @param ctx Library context.
 * @param doc Document to process.
 * @return 0 on success or <0 on failure.
 */
WCJSON_EXPORT int wcjsondocstrings(struct wcjson *ctx,
                                   struct wcjson_document *doc);

/**
 * Creates multibyte strings for all string and number values in a document by
 * converting wide character values to multibyte character values.
 * @param ctx Library context.
 * @param doc Document to process.
 * @return 0 on success or <0 on failure.
 */
WCJSON_EXPORT int wcjsondocmbstrings(struct wcjson *ctx,
                                     struct wcjson_document *doc);

/**
 * Serializes a JSON document to a wide character file.
 * @param f The file to serialize to.
 * @param doc The document to serialize.
 * @param value The value to serialize.
 * @return 0 on success or <0 on failure.
 */
WCJSON_EXPORT int wcjsondocfprint(FILE *f, const struct wcjson_document *doc,
                                  const struct wcjson_value *value);

/**
 * Serializes a JSON document to a 7 bit ASCII file.
 * @param f The file to serialize to.
 * @param doc The document to serialize.
 * @param value The value to serialize.
 * @return 0 on success or <0 on failure.
 */
WCJSON_EXPORT int wcjsondocfprintasc(FILE *f, const struct wcjson_document *doc,
                                     const struct wcjson_value *value);

/**
 * Serializes a JSON document to a wide character string.
 * @param s The string to serialize to.
 * @param lenp Pointer to the number of characters available in s, the number of
 * characters written to s on return.
 * @param doc The document to serialize.
 * @param value The value to serialize.
 * @return 0 on success or <0 on failure.
 */
WCJSON_EXPORT int wcjsondocsprint(wchar_t *s, size_t *lenp,
                                  const struct wcjson_document *doc,
                                  const struct wcjson_value *value);

/**
 * Serializes a JSON document to a 7 bit ASCII string.
 * @param s The string to serialize to.
 * @param lenp Pointer to the number of characters available in s, the number of
 * characters written to s on return.
 * @param doc The document to serialize.
 * @param value The value to serialize.
 * @return 0 on success or <0 on failure.
 */
WCJSON_EXPORT int wcjsondocsprintasc(wchar_t *s, size_t *lenp,
                                     const struct wcjson_document *doc,
                                     const struct wcjson_value *value);

#ifdef __cplusplus
}
#endif
#endif
