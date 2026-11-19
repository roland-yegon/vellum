/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include "vellum/text/cursor_set.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define VELLUM_INITIAL_CURSOR_CAPACITY 4u

struct VellumCursorSet {
    VellumBuffer *buffer;
    VellumLineIndex *lines;
    VellumCursor **cursors;
    size_t count;
    size_t capacity;
    size_t primary;
};

static bool append_cursor(VellumCursorSet *set, VellumCursor *cursor, VellumErrorInfo *error)
{
    if (set->count == set->capacity) {
        if (set->capacity > SIZE_MAX / 2u) {
            vellum_error_set(error, VELLUM_ERR_CAPACITY, "cursor set capacity overflow");
            return false;
        }
        const size_t capacity =
            set->capacity == 0u ? VELLUM_INITIAL_CURSOR_CAPACITY : set->capacity * 2u;
        VellumCursor **cursors = realloc(set->cursors, capacity * sizeof(*cursors));
        if (cursors == NULL) {
            vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED,
                             "could not grow cursor set to %zu cursors", capacity);
            return false;
        }
        set->cursors = cursors;
        set->capacity = capacity;
    }
    set->cursors[set->count++] = cursor;
    return true;
}

static bool overlaps_selection(const VellumCursorSet *set, const VellumCursor *candidate)
{
    const size_t start = vellum_cursor_selection_start(candidate);
    const size_t end = vellum_cursor_selection_end(candidate);
    for (size_t i = 0u; i < set->count; ++i) {
        const size_t other_start = vellum_cursor_selection_start(set->cursors[i]);
        const size_t other_end = vellum_cursor_selection_end(set->cursors[i]);
        if (other_start <= end && start <= other_end) {
            return true;
        }
    }
    return false;
}

VellumCursorSet *vellum_cursor_set_create(VellumBuffer *buffer, VellumLineIndex *lines,
                                          VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (buffer == NULL || lines == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT,
                         "a cursor set needs a buffer and a line index");
        return NULL;
    }
    if (vellum_line_index_line_count(lines) == 0u) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT,
                         "the line index must be built before creating a cursor set");
        return NULL;
    }

    VellumCursorSet *set = calloc(1u, sizeof(*set));
    if (set == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate cursor set");
        return NULL;
    }
    set->buffer = buffer;
    set->lines = lines;

    VellumCursor *primary = vellum_cursor_create(buffer, lines, error);
    if (primary == NULL) {
        free(set);
        return NULL;
    }
    if (!append_cursor(set, primary, error)) {
        vellum_cursor_destroy(primary);
        free(set);
        return NULL;
    }
    return set;
}

void vellum_cursor_set_destroy(VellumCursorSet *set)
{
    if (set == NULL) {
        return;
    }
    for (size_t i = 0u; i < set->count; ++i) {
        vellum_cursor_destroy(set->cursors[i]);
    }
    free(set->cursors);
    free(set);
}

size_t vellum_cursor_set_count(const VellumCursorSet *set)
{
    return set == NULL ? 0u : set->count;
}

size_t vellum_cursor_set_primary(const VellumCursorSet *set)
{
    return set == NULL ? 0u : set->primary;
}

void vellum_cursor_set_set_primary(VellumCursorSet *set, size_t index)
{
    if (set != NULL && index < set->count) {
        set->primary = index;
    }
}

VellumCursor *vellum_cursor_set_at(VellumCursorSet *set, size_t index)
{
    return set == NULL || index >= set->count ? NULL : set->cursors[index];
}

const VellumCursor *vellum_cursor_set_at_const(const VellumCursorSet *set, size_t index)
{
    return set == NULL || index >= set->count ? NULL : set->cursors[index];
}

VellumCursor *vellum_cursor_set_primary_cursor(VellumCursorSet *set)
{
    return vellum_cursor_set_at(set, vellum_cursor_set_primary(set));
}

bool vellum_cursor_set_add(VellumCursorSet *set, size_t offset, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (set == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "add needs a cursor set");
        return false;
    }

    VellumCursor *cursor = vellum_cursor_create(set->buffer, set->lines, error);
    if (cursor == NULL) {
        return false;
    }
    if (!vellum_cursor_set(cursor, offset, error)) {
        vellum_cursor_destroy(cursor);
        return false;
    }
    if (overlaps_selection(set, cursor)) {
        vellum_cursor_destroy(cursor);
        return true;
    }
    if (!append_cursor(set, cursor, error)) {
        vellum_cursor_destroy(cursor);
        return false;
    }
    return true;
}

bool vellum_cursor_set_add_selection(VellumCursorSet *set, size_t anchor, size_t head,
                                     VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (set == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "add needs a cursor set");
        return false;
    }

    VellumCursor *cursor = vellum_cursor_create(set->buffer, set->lines, error);
    if (cursor == NULL) {
        return false;
    }
    if (!vellum_cursor_select(cursor, anchor, head, error)) {
        vellum_cursor_destroy(cursor);
        return false;
    }
    if (overlaps_selection(set, cursor)) {
        vellum_cursor_destroy(cursor);
        return true;
    }
    if (!append_cursor(set, cursor, error)) {
        vellum_cursor_destroy(cursor);
        return false;
    }
    return true;
}

bool vellum_cursor_set_remove(VellumCursorSet *set, size_t index)
{
    if (set == NULL || index >= set->count || set->count == 1u) {
        return false;
    }
    vellum_cursor_destroy(set->cursors[index]);
    for (size_t i = index + 1u; i < set->count; ++i) {
        set->cursors[i - 1u] = set->cursors[i];
    }
    --set->count;
    if (set->primary == index) {
        set->primary = index < set->count ? index : set->count - 1u;
    } else if (set->primary > index) {
        --set->primary;
    }
    return true;
}

void vellum_cursor_set_clear(VellumCursorSet *set)
{
    if (set == NULL) {
        return;
    }
    VellumCursor *primary = set->cursors[set->primary];
    for (size_t i = 0u; i < set->count; ++i) {
        if (set->cursors[i] != primary) {
            vellum_cursor_destroy(set->cursors[i]);
        }
    }
    set->cursors[0] = primary;
    set->count = 1u;
    set->primary = 0u;
    vellum_cursor_collapse(primary);
}

bool vellum_cursor_set_set(VellumCursorSet *set, size_t offset, VellumErrorInfo *error)
{
    VellumCursor *cursor = vellum_cursor_set_primary_cursor(set);
    if (cursor == NULL) {
        vellum_error_init(error);
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "set needs a cursor set");
        return false;
    }
    return vellum_cursor_set(cursor, offset, error);
}

bool vellum_cursor_set_select(VellumCursorSet *set, size_t anchor, size_t head,
                              VellumErrorInfo *error)
{
    VellumCursor *cursor = vellum_cursor_set_primary_cursor(set);
    if (cursor == NULL) {
        vellum_error_init(error);
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "select needs a cursor set");
        return false;
    }
    return vellum_cursor_select(cursor, anchor, head, error);
}

bool vellum_cursor_set_move_all(VellumCursorSet *set, VellumCursorMotion motion, bool extend,
                                VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (set == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "move needs a cursor set");
        return false;
    }
    for (size_t i = 0u; i < set->count; ++i) {
        if (!vellum_cursor_move(set->cursors[i], motion, extend, error)) {
            return false;
        }
    }
    return true;
}

void vellum_cursor_set_collapse_all(VellumCursorSet *set)
{
    if (set == NULL) {
        return;
    }
    for (size_t i = 0u; i < set->count; ++i) {
        vellum_cursor_collapse(set->cursors[i]);
    }
}

bool vellum_cursor_set_notify_insert(VellumCursorSet *set, size_t offset, size_t length,
                                     VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (set == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "insert notify needs a cursor set");
        return false;
    }
    for (size_t i = 0u; i < set->count; ++i) {
        if (!vellum_cursor_notify_insert(set->cursors[i], offset, length, error)) {
            return false;
        }
    }
    return true;
}

bool vellum_cursor_set_notify_delete(VellumCursorSet *set, size_t offset, size_t length,
                                     VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (set == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "delete notify needs a cursor set");
        return false;
    }
    for (size_t i = 0u; i < set->count; ++i) {
        if (!vellum_cursor_notify_delete(set->cursors[i], offset, length, error)) {
            return false;
        }
    }
    return true;
}

bool vellum_cursor_set_validate(VellumCursorSet *set)
{
    if (set == NULL || set->count == 0u || set->primary >= set->count ||
        !vellum_buffer_validate(set->buffer) ||
        !vellum_line_index_validate(set->lines, set->buffer)) {
        return false;
    }
    for (size_t i = 0u; i < set->count; ++i) {
        if (set->cursors[i] == NULL || !vellum_cursor_validate(set->cursors[i])) {
            return false;
        }
    }
    return true;
}
