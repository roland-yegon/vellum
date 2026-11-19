/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tests/unit/tests.h"
#include "vellum/core/error.h"
#include "vellum/text/buffer.h"
#include "vellum/text/cursor.h"
#include "vellum/text/line_index.h"
#include "vellum/text/unicode.h"

typedef struct {
    VellumBuffer *buffer;
    VellumLineIndex *lines;
    VellumCursor *cursor;
} Fixture;

static void fixture_destroy(Fixture *fixture);

static bool fixture_init(Fixture *fixture, const char *text, size_t length)
{
    VellumErrorInfo error;
    fixture->buffer = vellum_buffer_create(text, length, &error);
    fixture->lines = vellum_line_index_create(&error);
    if (fixture->buffer == NULL || fixture->lines == NULL ||
        !vellum_line_index_rebuild(fixture->lines, fixture->buffer, &error)) {
        fixture_destroy(fixture);
        return false;
    }
    fixture->cursor = vellum_cursor_create(fixture->buffer, fixture->lines, &error);
    if (fixture->cursor == NULL) {
        fixture_destroy(fixture);
        return false;
    }
    return true;
}

static void fixture_destroy(Fixture *fixture)
{
    vellum_cursor_destroy(fixture->cursor);
    vellum_line_index_destroy(fixture->lines);
    vellum_buffer_destroy(fixture->buffer);
    memset(fixture, 0, sizeof(*fixture));
}

static bool check_state(const Fixture *fixture, size_t anchor, size_t head, const char *label)
{
    if (vellum_cursor_anchor(fixture->cursor) != anchor ||
        vellum_cursor_position(fixture->cursor) != head ||
        !vellum_cursor_validate(fixture->cursor)) {
        fprintf(stderr, "cursor %s: expected anchor=%zu head=%zu, got anchor=%zu head=%zu\n", label,
                anchor, head, vellum_cursor_anchor(fixture->cursor),
                vellum_cursor_position(fixture->cursor));
        return false;
    }
    const bool selected = anchor != head;
    if (vellum_cursor_has_selection(fixture->cursor) != selected ||
        vellum_cursor_selection_start(fixture->cursor) != (anchor < head ? anchor : head) ||
        vellum_cursor_selection_end(fixture->cursor) != (anchor < head ? head : anchor)) {
        fprintf(stderr, "cursor %s: selection accessors disagree\n", label);
        return false;
    }
    return true;
}

static bool basic_cases(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, "", 0u)) {
        return false;
    }
    VellumErrorInfo error;
    vellum_error_init(&error);
    bool ok = true;
#define CURSOR_STEP(name, expr)                                                                    \
    do {                                                                                           \
        if (ok && !(expr)) {                                                                       \
            fprintf(stderr, "cursor basic %s failed: %s\n", name, vellum_error_message(&error));   \
            ok = false;                                                                            \
        }                                                                                          \
    } while (0)

    CURSOR_STEP("empty start", check_state(&fixture, 0u, 0u, "empty start"));
    CURSOR_STEP("set clamped", vellum_cursor_set(fixture.cursor, 100u, &error));
    CURSOR_STEP("set clamped state", check_state(&fixture, 0u, 0u, "set clamped"));
    CURSOR_STEP("move left at zero",
                vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_LEFT, false, &error));
    CURSOR_STEP("move right at end",
                vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_RIGHT, false, &error));
    CURSOR_STEP("empty after moves", check_state(&fixture, 0u, 0u, "empty after moves"));

    fixture_destroy(&fixture);
    if (!fixture_init(&fixture, "abc", 3u)) {
        return false;
    }
    CURSOR_STEP("set two", vellum_cursor_set(fixture.cursor, 2u, &error));
    CURSOR_STEP("set two state", check_state(&fixture, 2u, 2u, "set two"));
    CURSOR_STEP("select backwards", vellum_cursor_select(fixture.cursor, 2u, 0u, &error));
    CURSOR_STEP("select backwards state", check_state(&fixture, 2u, 0u, "select backwards"));
    vellum_cursor_collapse(fixture.cursor);
    CURSOR_STEP("collapsed state", check_state(&fixture, 0u, 0u, "collapsed"));

    fixture_destroy(&fixture);
    if (!vellum_cursor_validate(NULL) && vellum_cursor_position(NULL) == 0u &&
        vellum_cursor_anchor(NULL) == 0u && !vellum_cursor_has_selection(NULL) &&
        vellum_cursor_selection_start(NULL) == 0u && vellum_cursor_selection_end(NULL) == 0u) {
        vellum_cursor_destroy(NULL);
        vellum_cursor_collapse(NULL);
    } else {
        fprintf(stderr, "cursor NULL handling failed\n");
        ok = false;
    }

#undef CURSOR_STEP
    fixture_destroy(&fixture);
    return ok;
}

static bool grapheme_movement(void)
{
    /* "a" + "e" U+0301 + "b": boundaries at 0, 1, 4, 5. */
    static const char text[] = "a\x65\xCC\x81"
                               "b";
    Fixture fixture;
    if (!fixture_init(&fixture, text, 5u)) {
        return false;
    }
    VellumErrorInfo error;
    vellum_error_init(&error);
    bool ok = true;
#define CURSOR_STEP(name, expr)                                                                    \
    do {                                                                                           \
        if (ok && !(expr)) {                                                                       \
            fprintf(stderr, "cursor grapheme %s failed: %s\n", name,                               \
                    vellum_error_message(&error));                                                 \
            ok = false;                                                                            \
        }                                                                                          \
    } while (0)

    CURSOR_STEP("set start", vellum_cursor_set(fixture.cursor, 0u, &error));
    CURSOR_STEP("right to e-acute",
                vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_RIGHT, false, &error));
    CURSOR_STEP("at 1", check_state(&fixture, 1u, 1u, "at 1"));
    CURSOR_STEP("right skips mark",
                vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_RIGHT, false, &error));
    CURSOR_STEP("at 4", check_state(&fixture, 4u, 4u, "at 4"));
    CURSOR_STEP("right to end",
                vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_RIGHT, false, &error));
    CURSOR_STEP("at 5", check_state(&fixture, 5u, 5u, "at 5"));
    CURSOR_STEP("right at end",
                vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_RIGHT, false, &error));
    CURSOR_STEP("still 5", check_state(&fixture, 5u, 5u, "still 5"));
    CURSOR_STEP("left to b", vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_LEFT, false, &error));
    CURSOR_STEP("at 4 again", check_state(&fixture, 4u, 4u, "at 4 again"));
    CURSOR_STEP("left skips mark",
                vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_LEFT, false, &error));
    CURSOR_STEP("back to 1", check_state(&fixture, 1u, 1u, "back to 1"));
    CURSOR_STEP("left to 0", vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_LEFT, false, &error));
    CURSOR_STEP("at 0", check_state(&fixture, 0u, 0u, "at 0"));
    CURSOR_STEP("left at zero",
                vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_LEFT, false, &error));
    CURSOR_STEP("still 0", check_state(&fixture, 0u, 0u, "still 0"));
    CURSOR_STEP("set mid mark", vellum_cursor_set(fixture.cursor, 3u, &error));
    CURSOR_STEP("snapped to 1", check_state(&fixture, 1u, 1u, "snapped to 1"));

    fixture_destroy(&fixture);
    /* Family ZWJ emoji (18 bytes) followed by 'x'. */
    static const char emoji[] = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D"
                                "\xF0\x9F\x91\xA7"
                                "x";
    if (!fixture_init(&fixture, emoji, 19u)) {
        return false;
    }
    CURSOR_STEP("set emoji start", vellum_cursor_set(fixture.cursor, 0u, &error));
    CURSOR_STEP("right over family",
                vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_RIGHT, false, &error));
    CURSOR_STEP("at 18", check_state(&fixture, 18u, 18u, "at 18"));
    CURSOR_STEP("right to x",
                vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_RIGHT, false, &error));
    CURSOR_STEP("at 19", check_state(&fixture, 19u, 19u, "at 19"));
    CURSOR_STEP("set mid emoji", vellum_cursor_set(fixture.cursor, 9u, &error));
    CURSOR_STEP("snapped to emoji start", check_state(&fixture, 0u, 0u, "snapped to emoji start"));

#undef CURSOR_STEP
    fixture_destroy(&fixture);
    return ok;
}

static bool line_movement(void)
{
    static const char text[] = "abc\nde\nfghi\r\nj";
    Fixture fixture;
    if (!fixture_init(&fixture, text, 14u)) {
        return false;
    }
    VellumErrorInfo error;
    vellum_error_init(&error);
    bool ok = true;
#define CURSOR_STEP(name, expr)                                                                    \
    do {                                                                                           \
        if (ok && !(expr)) {                                                                       \
            fprintf(stderr, "cursor line %s failed: %s\n", name, vellum_error_message(&error));    \
            ok = false;                                                                            \
        }                                                                                          \
    } while (0)

    CURSOR_STEP("set 0", vellum_cursor_set(fixture.cursor, 0u, &error));
    CURSOR_STEP("right x3",
                vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_RIGHT, false, &error) &&
                    vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_RIGHT, false, &error) &&
                    vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_RIGHT, false, &error));
    CURSOR_STEP("at 3", check_state(&fixture, 3u, 3u, "at 3"));
    CURSOR_STEP("right over LF",
                vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_RIGHT, false, &error));
    CURSOR_STEP("at 4", check_state(&fixture, 4u, 4u, "at 4"));
    CURSOR_STEP("left over LF",
                vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_LEFT, false, &error));
    CURSOR_STEP("back to 3", check_state(&fixture, 3u, 3u, "back to 3"));

    CURSOR_STEP("set 9", vellum_cursor_set(fixture.cursor, 9u, &error));
    CURSOR_STEP("down clamps",
                vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_DOWN, false, &error));
    CURSOR_STEP("down to 14", check_state(&fixture, 14u, 14u, "down to 14"));
    CURSOR_STEP("up restores column",
                vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_UP, false, &error));
    CURSOR_STEP("back to 9", check_state(&fixture, 9u, 9u, "back to 9"));

    CURSOR_STEP("home",
                vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_LINE_START, false, &error));
    CURSOR_STEP("at 7", check_state(&fixture, 7u, 7u, "at 7"));
    CURSOR_STEP("end", vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_LINE_END, false, &error));
    CURSOR_STEP("at 11", check_state(&fixture, 11u, 11u, "at 11"));
    CURSOR_STEP("right over CRLF",
                vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_RIGHT, false, &error));
    CURSOR_STEP("at 13", check_state(&fixture, 13u, 13u, "at 13"));
    CURSOR_STEP("left over CRLF",
                vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_LEFT, false, &error));
    CURSOR_STEP("back to 11", check_state(&fixture, 11u, 11u, "back to 11"));

    CURSOR_STEP("buffer end",
                vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_BUFFER_END, false, &error));
    CURSOR_STEP("at 14", check_state(&fixture, 14u, 14u, "at 14"));
    CURSOR_STEP("down at last line",
                vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_DOWN, false, &error));
    CURSOR_STEP("still 14", check_state(&fixture, 14u, 14u, "still 14"));
    CURSOR_STEP("buffer start",
                vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_BUFFER_START, false, &error));
    CURSOR_STEP("up at first line",
                vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_UP, false, &error));
    CURSOR_STEP("still 0", check_state(&fixture, 0u, 0u, "still 0"));

#undef CURSOR_STEP
    fixture_destroy(&fixture);
    return ok;
}

static bool selection_and_edits(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, "abcdef", 6u)) {
        return false;
    }
    VellumErrorInfo error;
    vellum_error_init(&error);
    bool ok = true;
#define CURSOR_STEP(name, expr)                                                                    \
    do {                                                                                           \
        if (ok && !(expr)) {                                                                       \
            fprintf(stderr, "cursor edits %s failed: %s\n", name, vellum_error_message(&error));   \
            ok = false;                                                                            \
        }                                                                                          \
    } while (0)

    CURSOR_STEP("set 2", vellum_cursor_set(fixture.cursor, 2u, &error));
    CURSOR_STEP("extend right",
                vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_RIGHT, true, &error));
    CURSOR_STEP("selection 2-3", check_state(&fixture, 2u, 3u, "selection 2-3"));
    CURSOR_STEP("extend again",
                vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_RIGHT, true, &error));
    CURSOR_STEP("selection 2-4", check_state(&fixture, 2u, 4u, "selection 2-4"));
    CURSOR_STEP("plain move collapses",
                vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_LEFT, false, &error));
    CURSOR_STEP("collapsed at 3", check_state(&fixture, 3u, 3u, "collapsed at 3"));
    CURSOR_STEP("select backwards", vellum_cursor_select(fixture.cursor, 5u, 1u, &error));
    CURSOR_STEP("selection 5-1", check_state(&fixture, 5u, 1u, "selection 5-1"));

    /* Insert "XY" at 2, then notify the line index and cursor in that order. */
    CURSOR_STEP("buffer insert", vellum_buffer_insert(fixture.buffer, 2u, "XY", 2u, &error));
    CURSOR_STEP("line insert",
                vellum_line_index_notify_insert(fixture.lines, fixture.buffer, 2u, 2u, &error));
    CURSOR_STEP("cursor insert", vellum_cursor_notify_insert(fixture.cursor, 2u, 2u, &error));
    CURSOR_STEP("remapped selection", check_state(&fixture, 7u, 1u, "remapped selection"));

    CURSOR_STEP("buffer delete", vellum_buffer_delete(fixture.buffer, 0u, 2u, &error));
    CURSOR_STEP("line delete",
                vellum_line_index_notify_delete(fixture.lines, fixture.buffer, 0u, 2u, &error));
    CURSOR_STEP("cursor delete", vellum_cursor_notify_delete(fixture.cursor, 0u, 2u, &error));
    CURSOR_STEP("remapped again", check_state(&fixture, 5u, 0u, "remapped again"));

    /* Deleting across the caret collapses it to the deletion point. */
    CURSOR_STEP("set 3", vellum_cursor_set(fixture.cursor, 3u, &error));
    CURSOR_STEP("buffer delete caret", vellum_buffer_delete(fixture.buffer, 2u, 3u, &error));
    CURSOR_STEP("line delete caret",
                vellum_line_index_notify_delete(fixture.lines, fixture.buffer, 2u, 3u, &error));
    CURSOR_STEP("cursor delete caret", vellum_cursor_notify_delete(fixture.cursor, 2u, 3u, &error));
    CURSOR_STEP("caret at deletion", check_state(&fixture, 2u, 2u, "caret at deletion"));

    /* Invalid notification ranges are rejected. */
    if (vellum_cursor_notify_insert(fixture.cursor, 100u, 1u, &error) ||
        error.code != VELLUM_ERR_INVALID_ARGUMENT) {
        fprintf(stderr, "cursor accepted an out-of-range insert notification\n");
        ok = false;
    }

#undef CURSOR_STEP
    fixture_destroy(&fixture);
    return ok;
}

static bool invalid_utf8(void)
{
    static const char text[] = "a\xFF\x80"
                               "b";
    Fixture fixture;
    if (!fixture_init(&fixture, text, 4u)) {
        return false;
    }
    VellumErrorInfo error;
    vellum_error_init(&error);
    bool ok = true;
#define CURSOR_STEP(name, expr)                                                                    \
    do {                                                                                           \
        if (ok && !(expr)) {                                                                       \
            fprintf(stderr, "cursor utf8 %s failed: %s\n", name, vellum_error_message(&error));    \
            ok = false;                                                                            \
        }                                                                                          \
    } while (0)

    CURSOR_STEP("set 0", vellum_cursor_set(fixture.cursor, 0u, &error));
    for (size_t i = 1u; i <= 4u; ++i) {
        CURSOR_STEP("byte right",
                    vellum_cursor_move(fixture.cursor, VELLUM_CURSOR_RIGHT, false, &error));
    }
    CURSOR_STEP("at 4", check_state(&fixture, 4u, 4u, "byte at 4"));
    CURSOR_STEP("set mid", vellum_cursor_set(fixture.cursor, 2u, &error));
    CURSOR_STEP("still 2", check_state(&fixture, 2u, 2u, "byte 2"));

#undef CURSOR_STEP
    fixture_destroy(&fixture);
    return ok;
}

#define FUZZ_OPERATIONS 10000u
#define MAX_TEXT 2048u
#define MAX_LINES (MAX_TEXT + 2u)

static uint32_t next_random(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static size_t model_boundaries(const char *text, size_t length, size_t *boundaries)
{
    VellumErrorInfo error;
    VellumGraphemeIterator *iterator = vellum_grapheme_iterator_create(text, length, &error);
    if (iterator == NULL) {
        /* Mirror the cursor's byte-wise fallback when edits split UTF-8. */
        for (size_t i = 0u; i <= length; ++i) {
            boundaries[i] = i;
        }
        return length + 1u;
    }
    size_t count = 0u;
    size_t boundary = 0u;
    while (vellum_grapheme_iterator_next(iterator, &boundary)) {
        boundaries[count++] = boundary;
    }
    vellum_grapheme_iterator_destroy(iterator);
    return count;
}

/* Naive line scanner over contiguous text, recognising LF, CR, and CRLF. */
static size_t model_lines(const char *text, size_t length, size_t *starts, size_t *ends)
{
    size_t lines = 1u;
    starts[0u] = 0u;
    for (size_t i = 0u; i < length;) {
        size_t terminator = 0u;
        if (text[i] == '\r') {
            terminator = i + 1u < length && text[i + 1u] == '\n' ? 2u : 1u;
        } else if (text[i] == '\n') {
            terminator = 1u;
        }
        if (terminator == 0u) {
            ++i;
            continue;
        }
        ends[lines - 1u] = i;
        i += terminator;
        if (lines >= MAX_LINES) {
            break;
        }
        starts[lines++] = i;
    }
    ends[lines - 1u] = length;
    return lines;
}

static size_t model_line_of(const size_t *starts, size_t lines, size_t offset)
{
    size_t line = 0u;
    for (size_t i = 0u; i < lines && starts[i] <= offset; ++i) {
        line = i;
    }
    return line;
}

static size_t model_snap(const size_t *boundaries, size_t count, size_t offset)
{
    size_t best = boundaries[0u];
    for (size_t i = 0u; i < count && boundaries[i] <= offset; ++i) {
        best = boundaries[i];
    }
    return best;
}

static size_t model_move(const char *model, size_t length, size_t head, size_t column,
                         VellumCursorMotion motion, size_t *new_column)
{
    size_t boundaries[MAX_TEXT + 2u];
    const size_t boundary_count = model_boundaries(model, length, boundaries);
    size_t starts[MAX_LINES];
    size_t ends[MAX_LINES];
    const size_t lines = model_lines(model, length, starts, ends);
    const size_t line = model_line_of(starts, lines, head);
    size_t target = head;

    switch (motion) {
    case VELLUM_CURSOR_BUFFER_START:
        target = 0u;
        break;
    case VELLUM_CURSOR_BUFFER_END:
        target = length;
        break;
    case VELLUM_CURSOR_LINE_START:
        target = starts[line];
        break;
    case VELLUM_CURSOR_LINE_END:
        target = ends[line];
        break;
    case VELLUM_CURSOR_LEFT: {
        size_t index = 0u;
        while (index < boundary_count && boundaries[index] != head) {
            ++index;
        }
        target = index > 0u ? boundaries[index - 1u] : 0u;
        break;
    }
    case VELLUM_CURSOR_RIGHT: {
        size_t index = 0u;
        while (index < boundary_count && boundaries[index] != head) {
            ++index;
        }
        target = index + 1u < boundary_count ? boundaries[index + 1u] : length;
        break;
    }
    case VELLUM_CURSOR_UP:
    case VELLUM_CURSOR_DOWN: {
        const bool up = motion == VELLUM_CURSOR_UP;
        if ((up && line == 0u) || (!up && line + 1u >= lines)) {
            *new_column = column;
            return head;
        }
        const size_t target_line = up ? line - 1u : line + 1u;
        const size_t max_relative = ends[target_line] - starts[target_line];
        const size_t relative = column < max_relative ? column : max_relative;
        size_t best = starts[target_line];
        for (size_t i = 0u; i < boundary_count; ++i) {
            if (boundaries[i] < starts[target_line]) {
                continue;
            }
            if (boundaries[i] > starts[target_line] + relative) {
                break;
            }
            best = boundaries[i];
        }
        *new_column = column; /* vertical movement keeps the desired column */
        return best;
    }
    default:
        break;
    }
    *new_column = target - starts[model_line_of(starts, lines, target)];
    return target;
}

static size_t fuzz_column(const char *model, size_t length, size_t offset)
{
    size_t starts[MAX_LINES];
    size_t ends[MAX_LINES];
    const size_t lines = model_lines(model, length, starts, ends);
    return offset - starts[model_line_of(starts, lines, offset)];
}

static bool differential_fuzz(void)
{
    static const char *snippets[] = {"a",
                                     "b",
                                     "\n",
                                     "\r\n",
                                     "\r",
                                     "xy",
                                     "\xE4\xB8\xAD\xE6\x96\x87",
                                     "e\xCC\x81",
                                     ("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D"
                                      "\xF0\x9F\x91\xA7"),
                                     "\xF0\x9F\x87\xB0\xF0\x9F\x87\xAA"};
    const size_t snippet_count = sizeof(snippets) / sizeof(snippets[0]);
    Fixture fixture;
    if (!fixture_init(&fixture, "", 0u)) {
        return false;
    }
    char model[MAX_TEXT + 2u];
    size_t length = 0u;
    size_t head = 0u;
    size_t anchor = 0u;
    size_t column = 0u;
    uint32_t state = 0xC0FFEEu;
    bool ok = true;

    for (uint32_t op = 0u; op < FUZZ_OPERATIONS && ok; ++op) {
        const uint32_t choice = next_random(&state) % 100u;
        VellumErrorInfo error;
        vellum_error_init(&error);

        if (choice < 60u) {
            const VellumCursorMotion motion = (VellumCursorMotion)(next_random(&state) % 8u);
            const bool extend = next_random(&state) % 2u != 0u;
            size_t new_column = column;
            const size_t target = model_move(model, length, head, column, motion, &new_column);
            const size_t target_anchor = extend ? anchor : target;
            if (!vellum_cursor_move(fixture.cursor, motion, extend, &error) ||
                !check_state(&fixture, target_anchor, target, "fuzz move")) {
                fprintf(stderr, "cursor fuzz move failed at op %u\n", op);
                ok = false;
                break;
            }
            head = target;
            anchor = target_anchor;
            column = new_column;
        } else if (choice < 75u) {
            size_t boundaries[MAX_TEXT + 2u];
            const size_t count = model_boundaries(model, length, boundaries);
            const size_t wanted_anchor = next_random(&state) % (length + 1u);
            const size_t wanted_head = next_random(&state) % (length + 1u);
            const size_t snapped_anchor = model_snap(boundaries, count, wanted_anchor);
            const size_t snapped_head = model_snap(boundaries, count, wanted_head);
            if (next_random(&state) % 2u != 0u) {
                if (!vellum_cursor_set(fixture.cursor, wanted_head, &error) ||
                    !check_state(&fixture, snapped_head, snapped_head, "fuzz set")) {
                    fprintf(stderr, "cursor fuzz set failed at op %u\n", op);
                    ok = false;
                    break;
                }
                anchor = snapped_head;
            } else {
                if (!vellum_cursor_select(fixture.cursor, wanted_anchor, wanted_head, &error) ||
                    !check_state(&fixture, snapped_anchor, snapped_head, "fuzz select")) {
                    fprintf(stderr, "cursor fuzz select failed at op %u\n", op);
                    ok = false;
                    break;
                }
                anchor = snapped_anchor;
            }
            head = snapped_head;
            column = fuzz_column(model, length, head);
        } else if (choice < 90u) {
            const char *snippet = snippets[next_random(&state) % snippet_count];
            const size_t insert_length = strlen(snippet);
            if (length + insert_length > MAX_TEXT / 2u) {
                continue;
            }
            size_t boundaries[MAX_TEXT + 2u];
            const size_t count = model_boundaries(model, length, boundaries);
            const size_t at = boundaries[next_random(&state) % count];
            if (!vellum_buffer_insert(fixture.buffer, at, snippet, insert_length, &error) ||
                !vellum_line_index_notify_insert(fixture.lines, fixture.buffer, at, insert_length,
                                                 &error) ||
                !vellum_cursor_notify_insert(fixture.cursor, at, insert_length, &error)) {
                fprintf(stderr, "cursor fuzz insert failed at op %u: %s\n", op,
                        vellum_error_message(&error));
                ok = false;
                break;
            }
            memmove(model + at + insert_length, model + at, length - at);
            memcpy(model + at, snippet, insert_length);
            length += insert_length;
            model[length] = '\0';
            if (head >= at) {
                head += insert_length;
            }
            if (anchor >= at) {
                anchor += insert_length;
            }
            size_t after_boundaries[MAX_TEXT + 2u];
            const size_t after_count = model_boundaries(model, length, after_boundaries);
            head = model_snap(after_boundaries, after_count, head);
            anchor = model_snap(after_boundaries, after_count, anchor);
            column = fuzz_column(model, length, head);
            if (!check_state(&fixture, anchor, head, "fuzz insert")) {
                fprintf(stderr,
                        "cursor fuzz insert state failed at op %u (at=%zu len=%zu model_len=%zu "
                        "snippet='%s')\n",
                        op, at, insert_length, length, snippet);
                ok = false;
                break;
            }
        } else {
            if (length == 0u) {
                continue;
            }
            /* Remove whole grapheme clusters so the model stays valid UTF-8;
             * byte-splitting deletes are covered by the invalid_utf8 case. */
            size_t boundaries[MAX_TEXT + 2u];
            const size_t before_count = model_boundaries(model, length, boundaries);
            if (before_count < 2u) {
                continue;
            }
            const size_t first = next_random(&state) % (before_count - 1u);
            size_t last = first + 1u + next_random(&state) % 4u;
            if (last > before_count - 1u) {
                last = before_count - 1u;
            }
            const size_t at = boundaries[first];
            const size_t delete_length = boundaries[last] - at;
            if (!vellum_buffer_delete(fixture.buffer, at, delete_length, &error) ||
                !vellum_line_index_notify_delete(fixture.lines, fixture.buffer, at, delete_length,
                                                 &error) ||
                !vellum_cursor_notify_delete(fixture.cursor, at, delete_length, &error)) {
                fprintf(stderr, "cursor fuzz delete failed at op %u: %s\n", op,
                        vellum_error_message(&error));
                ok = false;
                break;
            }
            memmove(model + at, model + at + delete_length, length - at - delete_length);
            length -= delete_length;
            model[length] = '\0';
            if (head > at && head < at + delete_length) {
                head = at;
            } else if (head >= at + delete_length) {
                head -= delete_length;
            }
            if (anchor > at && anchor < at + delete_length) {
                anchor = at;
            } else if (anchor >= at + delete_length) {
                anchor -= delete_length;
            }
            size_t after_boundaries[MAX_TEXT + 2u];
            const size_t after_count = model_boundaries(model, length, after_boundaries);
            head = model_snap(after_boundaries, after_count, head);
            anchor = model_snap(after_boundaries, after_count, anchor);
            column = fuzz_column(model, length, head);
            if (!check_state(&fixture, anchor, head, "fuzz delete")) {
                fprintf(stderr, "cursor fuzz delete state failed at op %u\n", op);
                ok = false;
                break;
            }
        }

        if (!vellum_cursor_validate(fixture.cursor)) {
            fprintf(stderr, "cursor fuzz validation failed at op %u\n", op);
            ok = false;
            break;
        }
    }

    fixture_destroy(&fixture);
    return ok;
}

bool test_cursor(void)
{
    if (!basic_cases()) {
        fprintf(stderr, "cursor basic_cases failed\n");
        return false;
    }
    if (!grapheme_movement()) {
        fprintf(stderr, "cursor grapheme_movement failed\n");
        return false;
    }
    if (!line_movement()) {
        fprintf(stderr, "cursor line_movement failed\n");
        return false;
    }
    if (!selection_and_edits()) {
        fprintf(stderr, "cursor selection_and_edits failed\n");
        return false;
    }
    if (!invalid_utf8()) {
        fprintf(stderr, "cursor invalid_utf8 failed\n");
        return false;
    }
    if (!differential_fuzz()) {
        fprintf(stderr, "cursor differential_fuzz failed\n");
        return false;
    }
    return true;
}
