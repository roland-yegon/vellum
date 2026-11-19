/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "tests/unit/tests.h"
#include "vellum/text/unicode.h"

static bool expect_valid(const char *text, size_t length)
{
    VellumUtf8Error error;
    if (!vellum_utf8_validate(text, length, &error)) {
        return false;
    }
    return error.offset == 0u && error.status == VELLUM_UTF8_VALID;
}

static bool expect_invalid(const char *text, size_t length, size_t offset, VellumUtf8Status status)
{
    VellumUtf8Error error;
    error.offset = SIZE_MAX;
    error.status = VELLUM_UTF8_VALID;
    if (vellum_utf8_validate(text, length, &error)) {
        return false;
    }
    return error.offset == offset && error.status == status;
}

static bool expect_boundaries(const char *text, const size_t *expected, size_t expected_count)
{
    VellumErrorInfo error;
    VellumGraphemeIterator *iterator = vellum_grapheme_iterator_create(text, strlen(text), &error);
    if (iterator == NULL) {
        return false;
    }

    size_t actual[32] = {0u};
    size_t count = 0u;
    size_t offset = 0u;
    while (vellum_grapheme_iterator_next(iterator, &offset)) {
        if (count == sizeof(actual) / sizeof(actual[0])) {
            vellum_grapheme_iterator_destroy(iterator);
            return false;
        }
        actual[count++] = offset;
    }
    vellum_grapheme_iterator_destroy(iterator);

    if (count != expected_count) {
        return false;
    }
    for (size_t i = 0u; i < count; ++i) {
        if (actual[i] != expected[i]) {
            return false;
        }
    }
    return true;
}

bool test_unicode(void)
{
    if (!expect_valid("", 0u) || !expect_valid("abc", 3u) || !expect_valid("a\0b", 3u) ||
        !expect_valid("a\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80", 10u)) {
        return false;
    }

    if (!expect_invalid("\x80", 1u, 0u, VELLUM_UTF8_ERR_STRAY_CONTINUATION) ||
        !expect_invalid("\xC0\xAF", 2u, 0u, VELLUM_UTF8_ERR_OVERLONG) ||
        !expect_invalid("\xE0\x80\xAF", 3u, 0u, VELLUM_UTF8_ERR_OVERLONG) ||
        !expect_invalid("\xED\xA0\x80", 3u, 0u, VELLUM_UTF8_ERR_SURROGATE) ||
        !expect_invalid("\xF4\x90\x80\x80", 4u, 0u, VELLUM_UTF8_ERR_OUT_OF_RANGE) ||
        !expect_invalid("abc\xF0\x9F", 5u, 3u, VELLUM_UTF8_ERR_TRUNCATED) ||
        !expect_invalid("a\xE2\x28\xA1", 4u, 1u, VELLUM_UTF8_ERR_INVALID_CONTINUATION) ||
        !expect_invalid(NULL, 1u, 0u, VELLUM_UTF8_ERR_INVALID_ARGUMENT) ||
        !expect_invalid("", (size_t)INT32_MAX + 1u, 0u, VELLUM_UTF8_ERR_TOO_LONG)) {
        return false;
    }

    if (vellum_utf8_status_name(VELLUM_UTF8_VALID) == NULL ||
        vellum_utf8_status_name((VellumUtf8Status)999) == NULL) {
        return false;
    }

    static const size_t ascii[] = {0u, 1u, 2u, 3u};
    static const size_t combining[] = {0u, 3u, 4u};
    static const size_t cjk[] = {0u, 3u, 6u};
    /* ICU's current default grapheme rules keep each Indic consonant + virama
     * separate; the test documents that behaviour rather than assuming a conjunct
     * is one cluster. */
    static const size_t devanagari[] = {0u, 3u, 6u, 9u};
    static const size_t family[] = {0u, 18u};
    static const size_t flag[] = {0u, 8u};
    static const size_t empty[] = {0u};

    if (!expect_boundaries("abc", ascii, sizeof(ascii) / sizeof(ascii[0])) ||
        !expect_boundaries("e\xCC\x81x", combining, sizeof(combining) / sizeof(combining[0])) ||
        !expect_boundaries("\xE4\xB8\xAD\xE6\x96\x87", cjk, sizeof(cjk) / sizeof(cjk[0])) ||
        !expect_boundaries("\xE0\xA4\x95\xE0\xA4\x8D\xE0\xA4\xB7", devanagari,
                           sizeof(devanagari) / sizeof(devanagari[0])) ||
        !expect_boundaries("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D"
                           "\xF0\x9F\x91\xA7",
                           family, sizeof(family) / sizeof(family[0])) ||
        !expect_boundaries("\xF0\x9F\x87\xB0\xF0\x9F\x87\xAA", flag,
                           sizeof(flag) / sizeof(flag[0])) ||
        !expect_boundaries("", empty, sizeof(empty) / sizeof(empty[0]))) {
        return false;
    }

    VellumErrorInfo error;
    if (vellum_grapheme_iterator_create("\xFF", 1u, &error) != NULL ||
        error.code != VELLUM_ERR_PARSE) {
        return false;
    }
    static char placeholder = 'x';
    if (vellum_grapheme_iterator_create(&placeholder, (size_t)INT32_MAX + 1u, &error) != NULL ||
        error.code != VELLUM_ERR_UNSUPPORTED) {
        return false;
    }
    if (vellum_grapheme_iterator_next(NULL, NULL)) {
        return false;
    }
    vellum_grapheme_iterator_destroy(NULL);

    return true;
}
