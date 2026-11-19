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
#include "vellum/text/cursor_set.h"
#include "vellum/text/line_index.h"

typedef struct {
    VellumBuffer *buffer;
    VellumLineIndex *lines;
    VellumCursorSet *set;
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
    fixture->set = vellum_cursor_set_create(fixture->buffer, fixture->lines, &error);
    if (fixture->set == NULL) {
        fixture_destroy(fixture);
        return false;
    }
    return true;
}

static void fixture_destroy(Fixture *fixture)
{
    vellum_cursor_set_destroy(fixture->set);
    vellum_line_index_destroy(fixture->lines);
    vellum_buffer_destroy(fixture->buffer);
    memset(fixture, 0, sizeof(*fixture));
}

static bool state_is(VellumCursorSet *set, size_t index, size_t anchor, size_t head,
                     const char *label)
{
    VellumCursor *cursor = vellum_cursor_set_at(set, index);
    if (cursor == NULL || vellum_cursor_anchor(cursor) != anchor ||
        vellum_cursor_position(cursor) != head || !vellum_cursor_validate(cursor)) {
        fprintf(stderr, "cursor set %s: cursor %zu expected anchor=%zu head=%zu\n", label, index,
                anchor, head);
        if (cursor != NULL) {
            fprintf(stderr, "  got anchor=%zu head=%zu\n", vellum_cursor_anchor(cursor),
                    vellum_cursor_position(cursor));
        }
        return false;
    }
    return true;
}

static bool heads_are(VellumCursorSet *set, const size_t *positions, size_t count,
                      const char *label)
{
    if (vellum_cursor_set_count(set) != count) {
        fprintf(stderr, "cursor set %s: expected %zu cursors, got %zu\n", label, count,
                vellum_cursor_set_count(set));
        return false;
    }
    for (size_t i = 0u; i < count; ++i) {
        VellumCursor *cursor = vellum_cursor_set_at(set, i);
        if (cursor == NULL || vellum_cursor_position(cursor) != positions[i] ||
            !vellum_cursor_validate(cursor)) {
            fprintf(stderr, "cursor set %s: cursor %zu expected head=%zu, got %zu\n", label, i,
                    positions[i], cursor == NULL ? SIZE_MAX : vellum_cursor_position(cursor));
            return false;
        }
    }
    return true;
}

static bool positions_are(VellumCursorSet *set, const size_t *positions, size_t count,
                          const char *label)
{
    if (!heads_are(set, positions, count, label)) {
        return false;
    }
    for (size_t i = 0u; i < count; ++i) {
        if (!state_is(set, i, positions[i], positions[i], label)) {
            return false;
        }
    }
    return true;
}

#define SET_STEP(name, expr)                                                                       \
    do {                                                                                           \
        if (ok && !(expr)) {                                                                       \
            fprintf(stderr, "cursor set %s failed: %s\n", name, vellum_error_message(&error));     \
            ok = false;                                                                            \
        }                                                                                          \
    } while (0)

static bool creation_and_primary(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, "abc\ndef\nghi\n", 12u)) {
        return false;
    }
    VellumErrorInfo error;
    vellum_error_init(&error);
    bool ok = true;

    const size_t zero = 0u;
    SET_STEP("initial shape", positions_are(fixture.set, &zero, 1u, "initial"));
    if (vellum_cursor_set_primary(fixture.set) != 0u ||
        vellum_cursor_set_primary_cursor(fixture.set) != vellum_cursor_set_at(fixture.set, 0u) ||
        vellum_cursor_set_at_const(fixture.set, 0u) == NULL ||
        vellum_cursor_set_at(fixture.set, 1u) != NULL) {
        fprintf(stderr, "cursor set primary/accessors disagree\n");
        ok = false;
    }
    SET_STEP("initial validate", vellum_cursor_set_validate(fixture.set));

    SET_STEP("primary set", vellum_cursor_set_set(fixture.set, 6u, &error));
    SET_STEP("primary set state", state_is(fixture.set, 0u, 6u, 6u, "primary set"));
    SET_STEP("primary select", vellum_cursor_set_select(fixture.set, 1u, 7u, &error));
    SET_STEP("primary select state", state_is(fixture.set, 0u, 1u, 7u, "primary select"));
    vellum_cursor_set_collapse_all(fixture.set);
    SET_STEP("collapsed", state_is(fixture.set, 0u, 7u, 7u, "collapsed"));
    fixture_destroy(&fixture);
    return ok;
}

static bool add_and_overlap(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, "abcdefghijkl", 12u)) {
        return false;
    }
    VellumErrorInfo error;
    vellum_error_init(&error);
    bool ok = true;

    SET_STEP("add two", vellum_cursor_set_add(fixture.set, 2u, &error));
    SET_STEP("add six", vellum_cursor_set_add(fixture.set, 6u, &error));
    const size_t collapsed[] = {0u, 2u, 6u};
    SET_STEP("three collapsed", positions_are(fixture.set, collapsed, 3u, "three collapsed"));

    /* An exact duplicate of an existing caret is ignored. */
    SET_STEP("add duplicate", vellum_cursor_set_add(fixture.set, 2u, &error));
    SET_STEP("still three", positions_are(fixture.set, collapsed, 3u, "still three"));

    /* A selection can be added, and a caret inside it is ignored. */
    SET_STEP("add selection", vellum_cursor_set_add_selection(fixture.set, 9u, 11u, &error));
    SET_STEP("four cursors", vellum_cursor_set_count(fixture.set) == 4u);
    SET_STEP("selection state", state_is(fixture.set, 3u, 9u, 11u, "selection"));
    SET_STEP("add inside selection", vellum_cursor_set_add(fixture.set, 10u, &error));
    SET_STEP("still four", vellum_cursor_set_count(fixture.set) == 4u);
    SET_STEP("overlapping selection",
             vellum_cursor_set_add_selection(fixture.set, 10u, 12u, &error));
    SET_STEP("still four after overlap", vellum_cursor_set_count(fixture.set) == 4u);

    SET_STEP("validate", vellum_cursor_set_validate(fixture.set));
    fixture_destroy(&fixture);
    return ok;
}

static bool grapheme_snap(void)
{
    /* "a" + "e\u0301" (one extended grapheme) + "b" = 5 bytes with boundaries
     * at {0, 1, 4, 5}. Offsets inside the cluster must snap down to 1. */
    static const char text[] = "ae\xCC\x81"
                               "b";
    Fixture fixture;
    if (!fixture_init(&fixture, text, sizeof(text) - 1u)) {
        return false;
    }
    VellumErrorInfo error;
    vellum_error_init(&error);
    bool ok = true;

    SET_STEP("snap into cluster", vellum_cursor_set_add(fixture.set, 2u, &error));
    SET_STEP("snapped to one", state_is(fixture.set, 1u, 1u, 1u, "snapped"));
    SET_STEP("snap duplicate", vellum_cursor_set_add(fixture.set, 3u, &error));
    SET_STEP("deduped", vellum_cursor_set_count(fixture.set) == 2u);
    SET_STEP("add at boundary", vellum_cursor_set_add(fixture.set, 4u, &error));
    const size_t expected[] = {0u, 1u, 4u};
    SET_STEP("three snapped", positions_are(fixture.set, expected, 3u, "snapped set"));
    SET_STEP("validate", vellum_cursor_set_validate(fixture.set));
    fixture_destroy(&fixture);
    return ok;
}

static bool primary_and_removal(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, "abcdefghijkl", 12u)) {
        return false;
    }
    VellumErrorInfo error;
    vellum_error_init(&error);
    bool ok = true;

    const size_t offsets[] = {2u, 6u, 10u};
    for (size_t i = 0u; i < 3u; ++i) {
        SET_STEP("add for removal", vellum_cursor_set_add(fixture.set, offsets[i], &error));
    }

    /* The new set starts with one cursor at 0, so the added carets sit at
     * indices 1..3 and index 3 holds the caret at offset 10. */
    vellum_cursor_set_set_primary(fixture.set, 3u);
    if (vellum_cursor_set_primary(fixture.set) != 3u ||
        vellum_cursor_position(vellum_cursor_set_primary_cursor(fixture.set)) != 10u) {
        fprintf(stderr, "cursor set primary selection failed\n");
        ok = false;
    }
    vellum_cursor_set_set_primary(fixture.set, 99u);
    if (vellum_cursor_set_primary(fixture.set) != 3u) {
        fprintf(stderr, "cursor set out-of-range primary was accepted\n");
        ok = false;
    }

    SET_STEP("remove first", vellum_cursor_set_remove(fixture.set, 0u));
    const size_t after_first[] = {2u, 6u, 10u};
    SET_STEP("after first removal",
             positions_are(fixture.set, after_first, 3u, "after first removal"));
    if (vellum_cursor_set_primary(fixture.set) != 2u ||
        vellum_cursor_position(vellum_cursor_set_primary_cursor(fixture.set)) != 10u) {
        fprintf(stderr, "cursor set primary adjustment after removal failed\n");
        ok = false;
    }

    /* Removing the primary caret itself selects the next-highest index. */
    SET_STEP("remove primary", vellum_cursor_set_remove(fixture.set, 2u));
    const size_t after_second[] = {2u, 6u};
    SET_STEP("after second removal",
             positions_are(fixture.set, after_second, 2u, "after second removal"));
    if (vellum_cursor_set_primary(fixture.set) != 1u ||
        vellum_cursor_position(vellum_cursor_set_primary_cursor(fixture.set)) != 6u) {
        fprintf(stderr, "cursor set primary after removing it failed\n");
        ok = false;
    }

    SET_STEP("remove to one", vellum_cursor_set_remove(fixture.set, 1u));
    const size_t last = 2u;
    SET_STEP("one left", positions_are(fixture.set, &last, 1u, "one left"));
    if (vellum_cursor_set_primary(fixture.set) != 0u || vellum_cursor_set_remove(fixture.set, 0u) ||
        vellum_cursor_set_remove(fixture.set, 5u) || vellum_cursor_set_remove(NULL, 0u)) {
        fprintf(stderr, "cursor set should reject removing the last cursor or bad indices\n");
        ok = false;
    }
    SET_STEP("validate", vellum_cursor_set_validate(fixture.set));
    fixture_destroy(&fixture);
    return ok;
}

static bool clear_set(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, "abcdefghijkl", 12u)) {
        return false;
    }
    VellumErrorInfo error;
    vellum_error_init(&error);
    bool ok = true;

    SET_STEP("add for clear", vellum_cursor_set_add(fixture.set, 2u, &error) &&
                                  vellum_cursor_set_add(fixture.set, 6u, &error) &&
                                  vellum_cursor_set_add(fixture.set, 10u, &error));
    SET_STEP("select primary", vellum_cursor_set_select(fixture.set, 0u, 3u, &error));
    vellum_cursor_set_set_primary(fixture.set, 1u);
    vellum_cursor_set_clear(fixture.set);
    SET_STEP("cleared count", vellum_cursor_set_count(fixture.set) == 1u &&
                                  vellum_cursor_set_primary(fixture.set) == 0u);
    SET_STEP("cleared keeps primary position", state_is(fixture.set, 0u, 2u, 2u, "cleared"));
    SET_STEP("validate", vellum_cursor_set_validate(fixture.set));
    fixture_destroy(&fixture);
    return ok;
}

static bool move_all_cursors(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, "abc\ndef\nghi\n", 12u)) {
        return false;
    }
    VellumErrorInfo error;
    vellum_error_init(&error);
    bool ok = true;

    SET_STEP("add movers", vellum_cursor_set_add(fixture.set, 2u, &error) &&
                               vellum_cursor_set_add(fixture.set, 6u, &error) &&
                               vellum_cursor_set_add(fixture.set, 10u, &error));
    const size_t start[] = {0u, 2u, 6u, 10u};
    SET_STEP("movers start", positions_are(fixture.set, start, 4u, "movers start"));

    SET_STEP("move right",
             vellum_cursor_set_move_all(fixture.set, VELLUM_CURSOR_RIGHT, false, &error));
    const size_t right[] = {1u, 3u, 7u, 11u};
    SET_STEP("after right", positions_are(fixture.set, right, 4u, "after right"));

    SET_STEP("line starts",
             vellum_cursor_set_move_all(fixture.set, VELLUM_CURSOR_LINE_START, false, &error));
    const size_t line_starts[] = {0u, 0u, 4u, 8u};
    SET_STEP("after line starts", positions_are(fixture.set, line_starts, 4u, "after line starts"));

    SET_STEP("line ends",
             vellum_cursor_set_move_all(fixture.set, VELLUM_CURSOR_LINE_END, false, &error));
    const size_t line_ends[] = {3u, 3u, 7u, 11u};
    SET_STEP("after line ends", positions_are(fixture.set, line_ends, 4u, "after line ends"));

    /* Extension keeps each caret's own anchor. */
    fixture_destroy(&fixture);
    if (!fixture_init(&fixture, "abcdefghijkl", 12u)) {
        return false;
    }
    SET_STEP("add for extend", vellum_cursor_set_add(fixture.set, 2u, &error));
    SET_STEP("extend right",
             vellum_cursor_set_move_all(fixture.set, VELLUM_CURSOR_RIGHT, true, &error));
    SET_STEP("first extended", state_is(fixture.set, 0u, 0u, 1u, "first extended"));
    SET_STEP("second extended", state_is(fixture.set, 1u, 2u, 3u, "second extended"));
    vellum_cursor_set_collapse_all(fixture.set);
    SET_STEP("collapsed again", state_is(fixture.set, 0u, 1u, 1u, "collapsed again") &&
                                    state_is(fixture.set, 1u, 3u, 3u, "collapsed second"));
    SET_STEP("validate", vellum_cursor_set_validate(fixture.set));
    fixture_destroy(&fixture);
    return ok;
}

static bool notify_remapping(void)
{
    Fixture fixture;
    if (!fixture_init(&fixture, "abcdefghij", 10u)) {
        return false;
    }
    VellumErrorInfo error;
    vellum_error_init(&error);
    bool ok = true;

    SET_STEP("add notifiees", vellum_cursor_set_add(fixture.set, 2u, &error) &&
                                  vellum_cursor_set_add(fixture.set, 6u, &error));
    SET_STEP("select primary", vellum_cursor_set_select(fixture.set, 0u, 2u, &error));

    /* Insert "XY" at offset 4: carets at/before it stay, the one after shifts. */
    SET_STEP("buffer insert", vellum_buffer_insert(fixture.buffer, 4u, "XY", 2u, &error));
    SET_STEP("index insert",
             vellum_line_index_notify_insert(fixture.lines, fixture.buffer, 4u, 2u, &error));
    SET_STEP("set insert", vellum_cursor_set_notify_insert(fixture.set, 4u, 2u, &error));
    const size_t after_insert[] = {2u, 2u, 8u};
    SET_STEP("after insert", heads_are(fixture.set, after_insert, 3u, "after insert"));
    SET_STEP("selection after insert", state_is(fixture.set, 0u, 0u, 2u, "selection after insert"));

    /* Delete bytes [1,3): the primary selection and the caret at 2 collapse
     * into the gap, and the caret at 8 shifts down by two. */
    SET_STEP("buffer delete", vellum_buffer_delete(fixture.buffer, 1u, 2u, &error));
    SET_STEP("index delete",
             vellum_line_index_notify_delete(fixture.lines, fixture.buffer, 1u, 2u, &error));
    SET_STEP("set delete", vellum_cursor_set_notify_delete(fixture.set, 1u, 2u, &error));
    const size_t after_delete[] = {1u, 1u, 6u};
    SET_STEP("after delete", heads_are(fixture.set, after_delete, 3u, "after delete"));
    SET_STEP("selection after delete", state_is(fixture.set, 0u, 0u, 1u, "selection after delete"));
    SET_STEP("validate", vellum_cursor_set_validate(fixture.set));
    fixture_destroy(&fixture);
    return ok;
}

static bool null_and_validation(void)
{
    VellumErrorInfo error;
    vellum_error_init(&error);

    if (vellum_cursor_set_create(NULL, NULL, &error) != NULL ||
        error.code != VELLUM_ERR_INVALID_ARGUMENT || vellum_cursor_set_count(NULL) != 0u ||
        vellum_cursor_set_primary(NULL) != 0u || vellum_cursor_set_at(NULL, 0u) != NULL ||
        vellum_cursor_set_at_const(NULL, 0u) != NULL ||
        vellum_cursor_set_primary_cursor(NULL) != NULL || vellum_cursor_set_validate(NULL) ||
        vellum_cursor_set_add(NULL, 0u, &error) ||
        vellum_cursor_set_add_selection(NULL, 0u, 0u, &error) ||
        vellum_cursor_set_remove(NULL, 0u) || vellum_cursor_set_set(NULL, 0u, &error) ||
        vellum_cursor_set_select(NULL, 0u, 0u, &error) ||
        vellum_cursor_set_move_all(NULL, VELLUM_CURSOR_RIGHT, false, &error) ||
        vellum_cursor_set_notify_insert(NULL, 0u, 1u, &error) ||
        vellum_cursor_set_notify_delete(NULL, 0u, 1u, &error)) {
        fprintf(stderr, "cursor set NULL handling failed\n");
        return false;
    }
    vellum_cursor_set_clear(NULL);
    vellum_cursor_set_collapse_all(NULL);
    vellum_cursor_set_set_primary(NULL, 0u);
    vellum_cursor_set_destroy(NULL);

    /* A line index that has not been built is rejected. */
    VellumBuffer *buffer = vellum_buffer_create("abc", 3u, &error);
    VellumLineIndex *lines = vellum_line_index_create(&error);
    bool ok = buffer != NULL && lines != NULL;
    if (ok && vellum_cursor_set_create(buffer, lines, &error) != NULL) {
        fprintf(stderr, "cursor set should reject an unbuilt line index\n");
        ok = false;
    }
    vellum_line_index_destroy(lines);
    vellum_buffer_destroy(buffer);
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

static bool stress(void)
{
    static const char text[] = "abc\ndef\nghi\njkl\nmno\npqr\nstu\nvwx\nyz";
    Fixture fixture;
    if (!fixture_init(&fixture, text, sizeof(text) - 1u)) {
        return false;
    }
    VellumErrorInfo error;
    vellum_error_init(&error);
    uint32_t state = 0xC0FFEEu;
    bool ok = true;

    /* Structural operations preserve validity. */
    for (uint32_t i = 0u; i < 2000u && ok; ++i) {
        const size_t length = vellum_buffer_length(fixture.buffer);
        const size_t count = vellum_cursor_set_count(fixture.set);
        const uint32_t choice = next_random(&state) % 5u;
        if (choice == 0u || count == 1u) {
            ok = vellum_cursor_set_add(fixture.set, next_random(&state) % (length + 1u), &error);
        } else if (choice == 1u) {
            (void)vellum_cursor_set_remove(fixture.set, next_random(&state) % count);
        } else if (choice == 2u) {
            ok = vellum_cursor_set_move_all(fixture.set,
                                            (VellumCursorMotion)(next_random(&state) % 8u),
                                            (next_random(&state) & 1u) != 0u, &error);
        } else if (choice == 3u) {
            vellum_cursor_set_collapse_all(fixture.set);
        } else {
            vellum_cursor_set_set_primary(fixture.set, next_random(&state) % count);
        }
        if (!ok || !vellum_cursor_set_validate(fixture.set)) {
            fprintf(stderr, "cursor set stress failed at structural op %u: %s\n", i,
                    vellum_error_message(&error));
            ok = false;
        }
    }

    /* Interleaved edits keep every caret remapped and valid. */
    for (uint32_t i = 0u; i < 1000u && ok; ++i) {
        const size_t length = vellum_buffer_length(fixture.buffer);
        const size_t at = next_random(&state) % (length + 1u);
        if ((next_random(&state) & 1u) != 0u) {
            ok = vellum_buffer_insert(fixture.buffer, at, "Q", 1u, &error) &&
                 vellum_line_index_notify_insert(fixture.lines, fixture.buffer, at, 1u, &error) &&
                 vellum_cursor_set_notify_insert(fixture.set, at, 1u, &error);
        } else if (at < length) {
            ok = vellum_buffer_delete(fixture.buffer, at, 1u, &error) &&
                 vellum_line_index_notify_delete(fixture.lines, fixture.buffer, at, 1u, &error) &&
                 vellum_cursor_set_notify_delete(fixture.set, at, 1u, &error);
        }
        if (!ok || !vellum_cursor_set_validate(fixture.set)) {
            fprintf(stderr, "cursor set stress failed at edit op %u: %s\n", i,
                    vellum_error_message(&error));
            ok = false;
        }
    }
    fixture_destroy(&fixture);
    return ok;
}

bool test_cursor_set(void)
{
    if (!creation_and_primary()) {
        fprintf(stderr, "cursor set creation_and_primary failed\n");
        return false;
    }
    if (!add_and_overlap()) {
        fprintf(stderr, "cursor set add_and_overlap failed\n");
        return false;
    }
    if (!grapheme_snap()) {
        fprintf(stderr, "cursor set grapheme_snap failed\n");
        return false;
    }
    if (!primary_and_removal()) {
        fprintf(stderr, "cursor set primary_and_removal failed\n");
        return false;
    }
    if (!clear_set()) {
        fprintf(stderr, "cursor set clear_set failed\n");
        return false;
    }
    if (!move_all_cursors()) {
        fprintf(stderr, "cursor set move_all_cursors failed\n");
        return false;
    }
    if (!notify_remapping()) {
        fprintf(stderr, "cursor set notify_remapping failed\n");
        return false;
    }
    if (!null_and_validation()) {
        fprintf(stderr, "cursor set null_and_validation failed\n");
        return false;
    }
    if (!stress()) {
        fprintf(stderr, "cursor set stress failed\n");
        return false;
    }
    return true;
}
