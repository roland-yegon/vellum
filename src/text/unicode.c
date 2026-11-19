/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#include "vellum/text/unicode.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#include <unicode/ubrk.h>
#include <unicode/utext.h>
#include <unicode/utf8.h>
#include <unicode/utypes.h>

struct VellumGraphemeIterator {
    UBreakIterator *breaks;
    int32_t length;
    int32_t boundary;
    bool started;
    bool finished;
};

const char *vellum_utf8_status_name(VellumUtf8Status status)
{
    switch (status) {
    case VELLUM_UTF8_VALID:
        return "valid";
    case VELLUM_UTF8_ERR_INVALID_ARGUMENT:
        return "invalid argument";
    case VELLUM_UTF8_ERR_STRAY_CONTINUATION:
        return "stray continuation byte";
    case VELLUM_UTF8_ERR_INVALID_LEAD:
        return "invalid lead byte";
    case VELLUM_UTF8_ERR_INVALID_CONTINUATION:
        return "invalid continuation byte";
    case VELLUM_UTF8_ERR_OVERLONG:
        return "overlong encoding";
    case VELLUM_UTF8_ERR_SURROGATE:
        return "UTF-16 surrogate";
    case VELLUM_UTF8_ERR_OUT_OF_RANGE:
        return "code point above U+10FFFF";
    case VELLUM_UTF8_ERR_TRUNCATED:
        return "truncated sequence";
    case VELLUM_UTF8_ERR_TOO_LONG:
        return "input exceeds ICU int32 range";
    }
    return "unknown";
}

static VellumUtf8Status classify_invalid(const char *text, size_t length, size_t offset)
{
    const uint8_t lead = (uint8_t)text[offset];
    if (lead < 0x80u) {
        return VELLUM_UTF8_ERR_INVALID_LEAD;
    }
    if (lead < 0xC0u) {
        return VELLUM_UTF8_ERR_STRAY_CONTINUATION;
    }
    if (lead < 0xC2u) {
        return VELLUM_UTF8_ERR_OVERLONG;
    }
    if (lead > 0xF4u) {
        return VELLUM_UTF8_ERR_INVALID_LEAD;
    }

    size_t sequence_length = 2u;
    if (lead >= 0xF0u) {
        sequence_length = 4u;
    } else if (lead >= 0xE0u) {
        sequence_length = 3u;
    }
    if (length - offset < sequence_length) {
        return VELLUM_UTF8_ERR_TRUNCATED;
    }

    const uint8_t first = (uint8_t)text[offset + 1u];
    if ((first & 0xC0u) != 0x80u) {
        return VELLUM_UTF8_ERR_INVALID_CONTINUATION;
    }
    if (lead == 0xE0u && first < 0xA0u) {
        return VELLUM_UTF8_ERR_OVERLONG;
    }
    if (lead == 0xEDu && first > 0x9Fu) {
        return VELLUM_UTF8_ERR_SURROGATE;
    }
    if (lead == 0xF0u && first < 0x90u) {
        return VELLUM_UTF8_ERR_OVERLONG;
    }
    if (lead == 0xF4u && first > 0x8Fu) {
        return VELLUM_UTF8_ERR_OUT_OF_RANGE;
    }
    for (size_t i = 2u; i < sequence_length; ++i) {
        const uint8_t continuation = (uint8_t)text[offset + i];
        if ((continuation & 0xC0u) != 0x80u) {
            return VELLUM_UTF8_ERR_INVALID_CONTINUATION;
        }
    }
    return VELLUM_UTF8_ERR_INVALID_LEAD;
}

bool vellum_utf8_validate(const char *text, size_t length, VellumUtf8Error *error)
{
    if (error != NULL) {
        error->offset = 0u;
        error->status = VELLUM_UTF8_VALID;
    }
    if (text == NULL && length != 0u) {
        if (error != NULL) {
            error->status = VELLUM_UTF8_ERR_INVALID_ARGUMENT;
        }
        return false;
    }
    if (length > (size_t)INT32_MAX) {
        if (error != NULL) {
            error->status = VELLUM_UTF8_ERR_TOO_LONG;
        }
        return false;
    }

    const uint8_t *bytes = (const uint8_t *)text;
    int32_t index = 0;
    const int32_t limit = (int32_t)length;
    while (index < limit) {
        const int32_t start = index;
        UChar32 code_point = U_SENTINEL;
        U8_NEXT(bytes, index, limit, code_point);
        if (code_point < 0) {
            if (error != NULL) {
                error->offset = (size_t)start;
                error->status = classify_invalid(text, length, (size_t)start);
            }
            return false;
        }
    }
    return true;
}

static bool set_icu_error(VellumErrorInfo *error, const char *operation, UErrorCode status)
{
    vellum_error_set(error, VELLUM_ERR_INTERNAL, "%s failed: %s", operation, u_errorName(status));
    return false;
}

VellumGraphemeIterator *vellum_grapheme_iterator_create(const char *text, size_t length,
                                                        VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (text == NULL && length != 0u) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "grapheme text is NULL");
        return NULL;
    }
    if (length > (size_t)INT32_MAX) {
        vellum_error_set(error, VELLUM_ERR_UNSUPPORTED,
                         "grapheme iteration supports at most %d bytes in this phase", INT32_MAX);
        return NULL;
    }

    VellumUtf8Error utf8_error;
    if (!vellum_utf8_validate(text, length, &utf8_error)) {
        vellum_error_set(error, VELLUM_ERR_PARSE, "invalid UTF-8 at byte %zu: %s",
                         utf8_error.offset, vellum_utf8_status_name(utf8_error.status));
        return NULL;
    }

    VellumGraphemeIterator *iterator = malloc(sizeof(*iterator));
    if (iterator == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate grapheme iterator");
        return NULL;
    }
    iterator->breaks = NULL;
    iterator->length = (int32_t)length;
    iterator->boundary = 0;
    iterator->started = false;
    iterator->finished = false;

    UErrorCode status = U_ZERO_ERROR;
    UText *utf8_text = utext_openUTF8(NULL, text == NULL ? "" : text, (int64_t)length, &status);
    if (U_FAILURE(status)) {
        (void)set_icu_error(error, "utext_openUTF8", status);
        free(iterator);
        return NULL;
    }

    iterator->breaks = ubrk_open(UBRK_CHARACTER, "und", NULL, 0, &status);
    if (U_FAILURE(status)) {
        (void)set_icu_error(error, "ubrk_open", status);
        utext_close(utf8_text);
        free(iterator);
        return NULL;
    }

    ubrk_setUText(iterator->breaks, utf8_text, &status);
    /* ubrk_setUText makes a shallow clone, so the UText wrapper can go away now. */
    utext_close(utf8_text);
    if (U_FAILURE(status)) {
        (void)set_icu_error(error, "ubrk_setUText", status);
        ubrk_close(iterator->breaks);
        free(iterator);
        return NULL;
    }

    return iterator;
}

void vellum_grapheme_iterator_destroy(VellumGraphemeIterator *iterator)
{
    if (iterator == NULL) {
        return;
    }
    if (iterator->breaks != NULL) {
        ubrk_close(iterator->breaks);
    }
    free(iterator);
}

bool vellum_grapheme_iterator_next(VellumGraphemeIterator *iterator, size_t *byte_offset)
{
    if (iterator == NULL || iterator->breaks == NULL || byte_offset == NULL || iterator->finished) {
        return false;
    }

    if (!iterator->started) {
        iterator->boundary = ubrk_first(iterator->breaks);
        iterator->started = true;
    } else {
        iterator->boundary = ubrk_next(iterator->breaks);
    }

    if (iterator->boundary == UBRK_DONE) {
        iterator->finished = true;
        return false;
    }
    if (iterator->boundary < 0 || iterator->boundary > iterator->length) {
        iterator->finished = true;
        return false;
    }

    *byte_offset = (size_t)iterator->boundary;
    return true;
}
