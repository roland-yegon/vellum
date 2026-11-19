/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#ifndef VELLUM_TEXT_UNICODE_H
#define VELLUM_TEXT_UNICODE_H

#include <stdbool.h>
#include <stddef.h>

#include "vellum/core/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Byte offsets are reported in UTF-8 bytes, never code points. */
typedef enum {
    VELLUM_UTF8_VALID = 0,
    VELLUM_UTF8_ERR_INVALID_ARGUMENT,
    VELLUM_UTF8_ERR_STRAY_CONTINUATION,
    VELLUM_UTF8_ERR_INVALID_LEAD,
    VELLUM_UTF8_ERR_INVALID_CONTINUATION,
    VELLUM_UTF8_ERR_OVERLONG,
    VELLUM_UTF8_ERR_SURROGATE,
    VELLUM_UTF8_ERR_OUT_OF_RANGE,
    VELLUM_UTF8_ERR_TRUNCATED,
    VELLUM_UTF8_ERR_TOO_LONG
} VellumUtf8Status;

typedef struct {
    size_t offset;
    VellumUtf8Status status;
} VellumUtf8Error;

const char *vellum_utf8_status_name(VellumUtf8Status status);

/* Uses ICU's strict UTF-8 rules. `length` is a byte count; embedded NUL is allowed.
 * Inputs longer than INT32_MAX bytes are rejected by this phase's ICU bridge. */
bool vellum_utf8_validate(const char *text, size_t length, VellumUtf8Error *error);

typedef struct VellumGraphemeIterator VellumGraphemeIterator;

/* Iterates ICU character (extended grapheme cluster) boundaries over UTF-8 text.
 * The caller must keep `text` alive and unmodified until the iterator is destroyed. */
VellumGraphemeIterator *vellum_grapheme_iterator_create(const char *text, size_t length,
                                                        VellumErrorInfo *error);
void vellum_grapheme_iterator_destroy(VellumGraphemeIterator *iterator);

/* Yields every boundary, including 0 and the final byte offset. Returns false when
 * exhausted. Each call returns the start of the next user-perceived character. */
bool vellum_grapheme_iterator_next(VellumGraphemeIterator *iterator, size_t *byte_offset);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_TEXT_UNICODE_H */
