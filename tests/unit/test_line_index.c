/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tests/unit/tests.h"
#include "vellum/text/buffer.h"
#include "vellum/text/line_index.h"

#define FUZZ_OPERATIONS 10000u

typedef struct {
    const char *text;
    size_t length;
} Chunk;

#define CHUNK(literal) {literal, sizeof(literal) - 1u}

static bool check_starts(const VellumLineIndex *index, const size_t *expected, size_t count)
{
    if (vellum_line_index_line_count(index) != count) {
        return false;
    }
    for (size_t i = 0u; i < count; ++i) {
        if (vellum_line_index_line_start(index, i) != expected[i]) {
            return false;
        }
    }
    return vellum_line_index_line_start(index, count) == SIZE_MAX;
}

static bool check_ending(const VellumLineIndex *index, const VellumBuffer *buffer, size_t line,
                         VellumLineEnding expected)
{
    return vellum_line_index_ending(index, buffer, line) == expected;
}

static bool basic_cases(void)
{
    VellumErrorInfo error;
    VellumLineIndex *index = vellum_line_index_create(&error);
    if (index == NULL) {
        return false;
    }
    bool ok = vellum_line_index_line_count(index) == 0u;

    static const struct {
        const char *text;
        size_t length;
        size_t starts[8];
        size_t count;
    } cases[] = {{"", 0u, {0u}, 1u},
                 {"abc", 3u, {0u}, 1u},
                 {"abc\n", 4u, {0u, 4u}, 2u},
                 {"\n", 1u, {0u, 1u}, 2u},
                 {"\n\n", 2u, {0u, 1u, 2u}, 3u},
                 {"a\nb\nc", 5u, {0u, 2u, 4u}, 3u},
                 {"a\r\nb\nc\rd", 8u, {0u, 3u, 5u, 7u}, 4u},
                 {"a\r\r\nb", 5u, {0u, 2u, 4u}, 3u}};
    const size_t case_count = sizeof(cases) / sizeof(cases[0u]);

    for (size_t c = 0u; c < case_count; ++c) {
        VellumBuffer *buffer = vellum_buffer_create(cases[c].text, cases[c].length, &error);
        if (buffer == NULL || !vellum_line_index_rebuild(index, buffer, &error) ||
            !check_starts(index, cases[c].starts, cases[c].count) ||
            !vellum_line_index_validate(index, buffer)) {
            vellum_buffer_destroy(buffer);
            ok = false;
            break;
        }
        vellum_buffer_destroy(buffer);
    }

    VellumBuffer *crlf = vellum_buffer_create("a\r\nb\n", 5u, &error);
    if (crlf != NULL && vellum_line_index_rebuild(index, crlf, &error)) {
        ok = ok && check_ending(index, crlf, 0u, VELLUM_LINE_ENDING_CRLF) &&
             check_ending(index, crlf, 1u, VELLUM_LINE_ENDING_LF) &&
             check_ending(index, crlf, 2u, VELLUM_LINE_ENDING_NONE) &&
             vellum_line_index_dominant_ending(index, crlf) == VELLUM_LINE_ENDING_CRLF &&
             vellum_line_index_content_end(index, crlf, 0u) == 1u &&
             vellum_line_index_content_end(index, crlf, 1u) == 4u &&
             vellum_line_index_content_end(index, crlf, 2u) == 5u;
    } else {
        ok = false;
    }
    vellum_buffer_destroy(crlf);

    VellumBuffer *empty = vellum_buffer_create("", 0u, &error);
    if (empty != NULL && vellum_line_index_rebuild(index, empty, &error)) {
        ok = ok && vellum_line_index_dominant_ending(index, empty) == VELLUM_LINE_ENDING_NONE &&
             vellum_line_index_ending(index, empty, 0u) == VELLUM_LINE_ENDING_NONE &&
             vellum_line_index_content_end(index, empty, 0u) == 0u;
    } else {
        ok = false;
    }
    vellum_buffer_destroy(empty);

    static const char *names[] = {"none", "LF", "CR", "CRLF"};
    for (int ending = 0; ending < 4; ++ending) {
        ok = ok && strcmp(vellum_line_ending_name((VellumLineEnding)ending), names[ending]) == 0;
    }

    size_t line = 999u;
    ok = ok && vellum_line_index_line_of(index, 0u, &line) && line == 0u;
    ok = ok && !vellum_line_index_line_of(NULL, 0u, &line) &&
         vellum_line_index_line_of(index, 0u, NULL);
    ok = ok && vellum_line_index_line_start(NULL, 0u) == SIZE_MAX &&
         vellum_line_index_line_count(NULL) == 0u && !vellum_line_index_validate(NULL, NULL);

    vellum_line_index_destroy(index);
    vellum_line_index_destroy(NULL);
    return ok;
}

static bool line_of_offsets(void)
{
    VellumErrorInfo error;
    VellumBuffer *buffer = vellum_buffer_create("abc\ndef\nghi", 11u, &error);
    VellumLineIndex *index = vellum_line_index_create(&error);
    if (buffer == NULL || index == NULL || !vellum_line_index_rebuild(index, buffer, &error)) {
        vellum_buffer_destroy(buffer);
        vellum_line_index_destroy(index);
        return false;
    }

    static const size_t offsets[] = {0u, 1u, 2u, 3u, 4u, 5u, 7u, 8u, 9u, 11u};
    static const size_t lines[] = {0u, 0u, 0u, 0u, 1u, 1u, 1u, 2u, 2u, 2u};
    bool ok = true;
    for (size_t i = 0u; i < sizeof(offsets) / sizeof(offsets[0u]); ++i) {
        size_t line = 999u;
        ok = ok && vellum_line_index_line_of(index, offsets[i], &line) && line == lines[i];
    }

    vellum_buffer_destroy(buffer);
    vellum_line_index_destroy(index);
    return ok;
}

static bool apply_insert(VellumBuffer *buffer, VellumLineIndex *index, size_t offset,
                         const char *text, size_t length, VellumErrorInfo *error)
{
    if (!vellum_buffer_insert(buffer, offset, text, length, error)) {
        return false;
    }
    return vellum_line_index_notify_insert(index, buffer, offset, length, error) &&
           vellum_line_index_validate(index, buffer);
}

static bool apply_delete(VellumBuffer *buffer, VellumLineIndex *index, size_t offset, size_t length,
                         VellumErrorInfo *error)
{
    if (!vellum_buffer_delete(buffer, offset, length, error)) {
        return false;
    }
    return vellum_line_index_notify_delete(index, buffer, offset, length, error) &&
           vellum_line_index_validate(index, buffer);
}

static bool incremental_edits(void)
{
    VellumErrorInfo error;
    static const char seed[] = "abc\ndef\nghi";
    VellumBuffer *buffer = vellum_buffer_create(seed, sizeof(seed) - 1u, &error);
    VellumLineIndex *index = vellum_line_index_create(&error);
    if (buffer == NULL || index == NULL || !vellum_line_index_rebuild(index, buffer, &error)) {
        vellum_buffer_destroy(buffer);
        vellum_line_index_destroy(index);
        return false;
    }

    bool ok = true;

    /* Insert a newline inside a line: one new line start. */
    ok = ok && apply_insert(buffer, index, 5u, "XY\nZ", 4u, &error);
    ok = ok && vellum_line_index_line_count(index) == 4u &&
         vellum_line_index_line_start(index, 2u) == 8u;

    /* Insert text without a newline: all later starts shift. */
    ok = ok && apply_insert(buffer, index, 2u, "!", 1u, &error);
    ok = ok && vellum_line_index_line_count(index) == 4u &&
         vellum_line_index_line_start(index, 1u) == 5u;

    /* Insert at the very front. */
    ok = ok && apply_insert(buffer, index, 0u, ">>\n", 3u, &error);
    ok = ok && vellum_line_index_line_count(index) == 5u &&
         vellum_line_index_line_start(index, 0u) == 0u &&
         vellum_line_index_line_start(index, 1u) == 3u;

    /* Insert at the very end, both with and without a terminator. */
    ok = ok && apply_insert(buffer, index, vellum_buffer_length(buffer), "tail", 4u, &error);
    ok = ok && apply_insert(buffer, index, vellum_buffer_length(buffer), "\n", 1u, &error);
    ok = ok && vellum_line_index_line_count(index) == 6u &&
         vellum_line_index_line_start(index, 5u) == vellum_buffer_length(buffer);

    /* Delete a newline, merging two lines. */
    ok = ok && apply_delete(buffer, index, 2u, 1u, &error);

    /* Delete a whole line including its terminator. */
    if (vellum_buffer_length(buffer) > 4u) {
        ok = ok && apply_delete(buffer, index, 3u, 2u, &error);
    }

    /* Delete everything. */
    ok = ok && apply_delete(buffer, index, 0u, vellum_buffer_length(buffer), &error);
    ok = ok && vellum_line_index_line_count(index) == 1u &&
         vellum_line_index_line_start(index, 0u) == 0u && vellum_line_index_validate(index, buffer);

    /* CRLF boundary: inserting LF after a standalone CR removes the old line start. */
    vellum_buffer_destroy(buffer);
    buffer = vellum_buffer_create("abc\rXYZ", 7u, &error);
    if (buffer != NULL && vellum_line_index_rebuild(index, buffer, &error)) {
        ok = ok && vellum_line_index_line_count(index) == 2u &&
             vellum_line_index_line_start(index, 1u) == 4u;
        ok = ok && apply_insert(buffer, index, 4u, "\n", 1u, &error);
        ok = ok && vellum_line_index_line_count(index) == 2u &&
             vellum_line_index_line_start(index, 1u) == 5u &&
             check_ending(index, buffer, 0u, VELLUM_LINE_ENDING_CRLF);
    } else {
        ok = false;
    }

    /* Deleting the LF of a CRLF makes the CR a standalone terminator again. */
    ok = ok && apply_delete(buffer, index, 4u, 1u, &error);
    ok = ok && vellum_line_index_line_count(index) == 2u &&
         vellum_line_index_line_start(index, 1u) == 4u &&
         check_ending(index, buffer, 0u, VELLUM_LINE_ENDING_CR);

    vellum_buffer_destroy(buffer);
    vellum_line_index_destroy(index);
    return ok;
}

static uint32_t next_random(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static bool differential_fuzz(void)
{
    static const Chunk snippets[] = {CHUNK(""),       CHUNK("a"),      CHUNK("\n"),
                                     CHUNK("\r"),     CHUNK("\r\n"),   CHUNK("x\ny"),
                                     CHUNK("a\r\nb"), CHUNK("世界\n"), CHUNK("\n\n")};
    const size_t snippet_count = sizeof(snippets) / sizeof(snippets[0u]);

    VellumErrorInfo error;
    VellumBuffer *buffer = vellum_buffer_create("seed\nline2\n", 11u, &error);
    VellumLineIndex *index = vellum_line_index_create(&error);
    if (buffer == NULL || index == NULL || !vellum_line_index_rebuild(index, buffer, &error)) {
        vellum_buffer_destroy(buffer);
        vellum_line_index_destroy(index);
        return false;
    }

    uint32_t state = 0xC0FFEEu;
    bool ok = true;
    for (uint32_t op = 0u; op < FUZZ_OPERATIONS && ok; ++op) {
        const size_t length = vellum_buffer_length(buffer);
        const uint32_t choice = next_random(&state) % 100u;
        char operation_desc[128] = "";

        if (choice < 55u) {
            const Chunk *snippet = &snippets[next_random(&state) % snippet_count];
            const size_t position = next_random(&state) % (length + 1u);
            snprintf(operation_desc, sizeof(operation_desc), "insert at %zu len %zu", position,
                     snippet->length);
            ok = vellum_buffer_insert(buffer, position, snippet->text, snippet->length, &error);
            if (ok) {
                ok = vellum_line_index_notify_insert(index, buffer, position, snippet->length,
                                                     &error);
            }
        } else if (length > 0u) {
            const size_t position = next_random(&state) % length;
            size_t count = 1u + (next_random(&state) % 8u);
            if (count > length - position) {
                count = length - position;
            }
            snprintf(operation_desc, sizeof(operation_desc), "delete at %zu len %zu", position,
                     count);
            ok = vellum_buffer_delete(buffer, position, count, &error);
            if (ok) {
                ok = vellum_line_index_notify_delete(index, buffer, position, count, &error);
            }
        }

        const bool valid = vellum_line_index_validate(index, buffer);
        ok = ok && valid;
        if (ok) {
            continue;
        }
        char *snapshot = vellum_buffer_snapshot(buffer, &error);
        fprintf(stderr, "line index fuzz failed at op %u: %s text='%s' (%s)\n", op, operation_desc,
                snapshot == NULL ? "NULL" : snapshot, vellum_error_message(&error));
        fprintf(stderr, "  actual starts (%zu):", vellum_line_index_line_count(index));
        for (size_t i = 0u; i < vellum_line_index_line_count(index); ++i) {
            fprintf(stderr, " %zu", vellum_line_index_line_start(index, i));
        }
        VellumErrorInfo fresh_error;
        VellumLineIndex *fresh = vellum_line_index_create(&fresh_error);
        vellum_line_index_rebuild(fresh, buffer, &fresh_error);
        fprintf(stderr, "\n  expected starts (%zu):", vellum_line_index_line_count(fresh));
        for (size_t i = 0u; i < vellum_line_index_line_count(fresh); ++i) {
            fprintf(stderr, " %zu", vellum_line_index_line_start(fresh, i));
        }
        fprintf(stderr, "\n");
        vellum_line_index_destroy(fresh);
        free(snapshot);
    }

    vellum_buffer_destroy(buffer);
    vellum_line_index_destroy(index);
    return ok;
}

bool test_line_index(void)
{
    return basic_cases() && line_of_offsets() && incremental_edits() && differential_fuzz();
}
