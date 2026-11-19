/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include "vellum/text/cursor.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vellum/text/unicode.h"

struct VellumCursor {
    VellumBuffer *buffer;
    VellumLineIndex *lines;
    size_t anchor;
    size_t head;
    size_t desired_column;

    /* Reusable scratch for one line's grapheme boundaries. */
    char *text;
    size_t text_capacity;
    size_t *boundaries;
    size_t boundary_capacity;
    size_t boundary_count;
    size_t region_start;
    size_t region_length;
};

/* Grows a scratch allocation when needed and returns the (possibly moved)
 * pointer. A NULL return always means failure, so callers must request at
 * least one element. */
static void *grow_buffer(void *ptr, size_t *capacity, size_t need, size_t element_size,
                         VellumErrorInfo *error)
{
    if (need <= *capacity) {
        return ptr;
    }
    if (need > SIZE_MAX / element_size) {
        vellum_error_set(error, VELLUM_ERR_CAPACITY, "cursor scratch size overflow");
        return NULL;
    }

    size_t next = *capacity == 0u ? 64u : *capacity;
    while (next < need) {
        if (next > SIZE_MAX / 2u) {
            next = need;
            break;
        }
        next *= 2u;
    }
    const size_t bytes = next * element_size;
    void *grown = realloc(ptr, bytes);
    if (grown == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED,
                         "could not grow cursor scratch to %zu bytes", bytes);
        return NULL;
    }
    *capacity = next;
    return grown;
}

static bool line_of(const VellumCursor *cursor, size_t offset, size_t *line, VellumErrorInfo *error)
{
    if (!vellum_line_index_line_of(cursor->lines, offset, line)) {
        vellum_error_set(error, VELLUM_ERR_INTERNAL, "offset %zu is outside the line index",
                         offset);
        return false;
    }
    return true;
}

/* Loads the bytes and grapheme boundaries for one logical line, including its
 * terminator. A terminator never splits a cluster, so per-line analysis matches
 * iterating the whole buffer. Invalid UTF-8 degrades to byte boundaries. */
static bool load_line(VellumCursor *cursor, size_t line, VellumErrorInfo *error)
{
    const size_t count = vellum_line_index_line_count(cursor->lines);
    if (line >= count) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "line %zu is out of range", line);
        return false;
    }

    const size_t start = vellum_line_index_line_start(cursor->lines, line);
    const size_t total = vellum_buffer_length(cursor->buffer);
    const size_t end =
        line + 1u < count ? vellum_line_index_line_start(cursor->lines, line + 1u) : total;
    if (start == SIZE_MAX || end == SIZE_MAX || end < start || end > total) {
        vellum_error_set(error, VELLUM_ERR_INTERNAL, "invalid byte range for line %zu", line);
        return false;
    }
    const size_t length = end - start;

    char *text = grow_buffer(cursor->text, &cursor->text_capacity, length + 1u, 1u, error);
    if (text == NULL) {
        return false;
    }
    cursor->text = text;
    vellum_error_init(error);
    if (length > 0u && !vellum_buffer_read(cursor->buffer, start, cursor->text, length, error)) {
        return false;
    }
    cursor->text[length] = '\0';

    VellumGraphemeIterator *iterator = vellum_grapheme_iterator_create(cursor->text, length, error);
    if (iterator == NULL) {
        if (error == NULL || error->code != VELLUM_ERR_PARSE) {
            return false;
        }
        size_t *boundaries = grow_buffer(cursor->boundaries, &cursor->boundary_capacity,
                                         length + 1u, sizeof(*cursor->boundaries), error);
        if (boundaries == NULL) {
            return false;
        }
        cursor->boundaries = boundaries;
        for (size_t i = 0u; i <= length; ++i) {
            cursor->boundaries[i] = i;
        }
        cursor->boundary_count = length + 1u;
        vellum_error_init(error);
    } else {
        cursor->boundary_count = 0u;
        size_t boundary = 0u;
        while (vellum_grapheme_iterator_next(iterator, &boundary)) {
            size_t *boundaries =
                grow_buffer(cursor->boundaries, &cursor->boundary_capacity,
                            cursor->boundary_count + 1u, sizeof(*cursor->boundaries), error);
            if (boundaries == NULL) {
                vellum_grapheme_iterator_destroy(iterator);
                return false;
            }
            cursor->boundaries = boundaries;
            cursor->boundaries[cursor->boundary_count++] = boundary;
        }
        vellum_grapheme_iterator_destroy(iterator);
        if (cursor->boundary_count == 0u) {
            vellum_error_set(error, VELLUM_ERR_INTERNAL,
                             "grapheme iterator returned no boundaries");
            return false;
        }
    }

    cursor->region_start = start;
    cursor->region_length = length;
    return true;
}

/* Snaps a relative byte offset within the currently loaded line. */
static size_t snap_loaded(const VellumCursor *cursor, size_t relative, bool prefer_next)
{
    const size_t last = cursor->boundaries[cursor->boundary_count - 1u];
    size_t best = prefer_next ? last : cursor->boundaries[0u];
    for (size_t i = 0u; i < cursor->boundary_count; ++i) {
        const size_t boundary = cursor->boundaries[i];
        if (prefer_next) {
            if (boundary >= relative) {
                best = boundary;
                break;
            }
        } else {
            if (boundary <= relative) {
                best = boundary;
            } else {
                break;
            }
        }
    }
    return cursor->region_start + best;
}

static bool snap_offset(VellumCursor *cursor, size_t offset, bool prefer_next, size_t *out,
                        VellumErrorInfo *error)
{
    const size_t total = vellum_buffer_length(cursor->buffer);
    if (offset > total) {
        offset = total;
    }
    size_t line = 0u;
    if (!line_of(cursor, offset, &line, error) || !load_line(cursor, line, error)) {
        return false;
    }
    size_t relative = offset - cursor->region_start;
    if (relative > cursor->region_length) {
        relative = cursor->region_length;
    }
    *out = snap_loaded(cursor, relative, prefer_next);
    return true;
}

/* Computes a fresh desired column for an offset without mutating the cursor. */
static bool column_for(const VellumCursor *cursor, size_t offset, size_t *column,
                       VellumErrorInfo *error)
{
    size_t line = 0u;
    if (!line_of(cursor, offset, &line, error)) {
        return false;
    }
    const size_t start = vellum_line_index_line_start(cursor->lines, line);
    if (start == SIZE_MAX) {
        vellum_error_set(error, VELLUM_ERR_INTERNAL, "missing line start for line %zu", line);
        return false;
    }
    *column = offset - start;
    return true;
}

VellumCursor *vellum_cursor_create(VellumBuffer *buffer, VellumLineIndex *lines,
                                   VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (buffer == NULL || lines == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT,
                         "a cursor needs a buffer and a line index");
        return NULL;
    }
    if (vellum_line_index_line_count(lines) == 0u) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT,
                         "the line index must be built before creating a cursor");
        return NULL;
    }

    VellumCursor *cursor = calloc(1u, sizeof(*cursor));
    if (cursor == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate cursor");
        return NULL;
    }
    cursor->buffer = buffer;
    cursor->lines = lines;
    return cursor;
}

void vellum_cursor_destroy(VellumCursor *cursor)
{
    if (cursor == NULL) {
        return;
    }
    free(cursor->text);
    free(cursor->boundaries);
    free(cursor);
}

size_t vellum_cursor_position(const VellumCursor *cursor)
{
    return cursor == NULL ? 0u : cursor->head;
}

size_t vellum_cursor_anchor(const VellumCursor *cursor)
{
    return cursor == NULL ? 0u : cursor->anchor;
}

bool vellum_cursor_has_selection(const VellumCursor *cursor)
{
    return cursor != NULL && cursor->anchor != cursor->head;
}

size_t vellum_cursor_selection_start(const VellumCursor *cursor)
{
    if (cursor == NULL) {
        return 0u;
    }
    return cursor->anchor < cursor->head ? cursor->anchor : cursor->head;
}

size_t vellum_cursor_selection_end(const VellumCursor *cursor)
{
    if (cursor == NULL) {
        return 0u;
    }
    return cursor->anchor < cursor->head ? cursor->head : cursor->anchor;
}

void vellum_cursor_collapse(VellumCursor *cursor)
{
    if (cursor != NULL) {
        cursor->anchor = cursor->head;
    }
}

bool vellum_cursor_set(VellumCursor *cursor, size_t offset, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (cursor == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "set needs a cursor");
        return false;
    }
    size_t snapped = 0u;
    size_t column = 0u;
    if (!snap_offset(cursor, offset, false, &snapped, error) ||
        !column_for(cursor, snapped, &column, error)) {
        return false;
    }
    cursor->head = snapped;
    cursor->anchor = snapped;
    cursor->desired_column = column;
    return true;
}

bool vellum_cursor_select(VellumCursor *cursor, size_t anchor, size_t head, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (cursor == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "select needs a cursor");
        return false;
    }
    size_t snapped_anchor = 0u;
    size_t snapped_head = 0u;
    size_t column = 0u;
    if (!snap_offset(cursor, anchor, false, &snapped_anchor, error) ||
        !snap_offset(cursor, head, false, &snapped_head, error) ||
        !column_for(cursor, snapped_head, &column, error)) {
        return false;
    }
    cursor->anchor = snapped_anchor;
    cursor->head = snapped_head;
    cursor->desired_column = column;
    return true;
}

/* Finds the largest boundary strictly before `relative`, if any. */
static bool previous_in_line(const VellumCursor *cursor, size_t relative, size_t *out)
{
    bool found = false;
    for (size_t i = 0u; i < cursor->boundary_count; ++i) {
        if (cursor->boundaries[i] >= relative) {
            break;
        }
        *out = cursor->boundaries[i];
        found = true;
    }
    return found;
}

/* Finds the smallest boundary strictly after `relative`, if any. */
static bool next_in_line(const VellumCursor *cursor, size_t relative, size_t *out)
{
    for (size_t i = 0u; i < cursor->boundary_count; ++i) {
        if (cursor->boundaries[i] > relative) {
            *out = cursor->boundaries[i];
            return true;
        }
    }
    return false;
}

bool vellum_cursor_move(VellumCursor *cursor, VellumCursorMotion motion, bool extend,
                        VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (cursor == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "move needs a cursor");
        return false;
    }

    const size_t total = vellum_buffer_length(cursor->buffer);
    const size_t count = vellum_line_index_line_count(cursor->lines);
    size_t current_line = 0u;
    size_t target = cursor->head;

    switch (motion) {
    case VELLUM_CURSOR_BUFFER_START:
        target = 0u;
        break;
    case VELLUM_CURSOR_BUFFER_END:
        target = total;
        break;
    case VELLUM_CURSOR_LINE_START:
        if (!line_of(cursor, cursor->head, &current_line, error)) {
            return false;
        }
        target = vellum_line_index_line_start(cursor->lines, current_line);
        if (target == SIZE_MAX) {
            vellum_error_set(error, VELLUM_ERR_INTERNAL, "missing line start");
            return false;
        }
        break;
    case VELLUM_CURSOR_LINE_END:
        if (!line_of(cursor, cursor->head, &current_line, error)) {
            return false;
        }
        target = vellum_line_index_content_end(cursor->lines, cursor->buffer, current_line);
        if (target == SIZE_MAX) {
            vellum_error_set(error, VELLUM_ERR_INTERNAL, "missing content end");
            return false;
        }
        break;
    case VELLUM_CURSOR_LEFT:
        if (cursor->head > 0u) {
            size_t boundary = 0u;
            if (!line_of(cursor, cursor->head, &current_line, error) ||
                !load_line(cursor, current_line, error)) {
                return false;
            }
            const size_t relative = cursor->head - cursor->region_start;
            if (previous_in_line(cursor, relative, &boundary)) {
                target = cursor->region_start + boundary;
            } else if (current_line > 0u) {
                if (!load_line(cursor, current_line - 1u, error)) {
                    return false;
                }
                /* The final boundary is the line start; the one before it is
                 * the end of the previous line's content (before its terminator). */
                if (previous_in_line(cursor, cursor->region_length, &boundary)) {
                    target = cursor->region_start + boundary;
                } else {
                    target = cursor->region_start;
                }
            } else {
                target = 0u;
            }
        }
        break;
    case VELLUM_CURSOR_RIGHT:
        if (cursor->head < total) {
            size_t boundary = 0u;
            if (!line_of(cursor, cursor->head, &current_line, error) ||
                !load_line(cursor, current_line, error)) {
                return false;
            }
            const size_t relative = cursor->head - cursor->region_start;
            if (next_in_line(cursor, relative, &boundary)) {
                target = cursor->region_start + boundary;
            } else if (current_line + 1u < count) {
                target = vellum_line_index_line_start(cursor->lines, current_line + 1u);
                if (target == SIZE_MAX) {
                    vellum_error_set(error, VELLUM_ERR_INTERNAL, "missing next line start");
                    return false;
                }
            } else {
                target = total;
            }
        }
        break;
    case VELLUM_CURSOR_UP:
    case VELLUM_CURSOR_DOWN: {
        if (!line_of(cursor, cursor->head, &current_line, error)) {
            return false;
        }
        const bool up = motion == VELLUM_CURSOR_UP;
        if ((up && current_line == 0u) || (!up && current_line + 1u >= count)) {
            break; /* No movement; keep the caret and its desired column. */
        }
        const size_t target_line = up ? current_line - 1u : current_line + 1u;
        const size_t line_start = vellum_line_index_line_start(cursor->lines, target_line);
        const size_t content_end =
            vellum_line_index_content_end(cursor->lines, cursor->buffer, target_line);
        if (line_start == SIZE_MAX || content_end == SIZE_MAX || content_end < line_start) {
            vellum_error_set(error, VELLUM_ERR_INTERNAL, "invalid target line range");
            return false;
        }
        const size_t max_relative = content_end - line_start;
        size_t relative = cursor->desired_column;
        if (relative > max_relative) {
            relative = max_relative;
        }
        if (!load_line(cursor, target_line, error)) {
            return false;
        }
        size_t boundary = cursor->boundaries[0u];
        for (size_t i = 0u; i < cursor->boundary_count; ++i) {
            if (cursor->boundaries[i] > relative) {
                break;
            }
            boundary = cursor->boundaries[i];
        }
        /* A terminator is a cluster too; never place the caret on it. */
        if (boundary > max_relative) {
            boundary = cursor->boundaries[0u];
            for (size_t i = 0u; i < cursor->boundary_count; ++i) {
                if (cursor->boundaries[i] > max_relative) {
                    break;
                }
                boundary = cursor->boundaries[i];
            }
        }
        target = line_start + boundary;
        break;
    }
    default:
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "unknown cursor motion %d",
                         (int)motion);
        return false;
    }

    if (target > total) {
        target = total;
    }
    size_t column = cursor->desired_column;
    if (motion != VELLUM_CURSOR_UP && motion != VELLUM_CURSOR_DOWN) {
        if (!column_for(cursor, target, &column, error)) {
            return false;
        }
    }
    cursor->head = target;
    if (!extend) {
        cursor->anchor = target;
    }
    cursor->desired_column = column;
    return true;
}

static void remap_insert(size_t *offset, size_t at, size_t length)
{
    if (*offset >= at) {
        *offset += length;
    }
}

static void remap_delete(size_t *offset, size_t at, size_t length)
{
    if (*offset <= at) {
        return;
    }
    if (*offset < at + length) {
        *offset = at;
    } else {
        *offset -= length;
    }
}

bool vellum_cursor_notify_insert(VellumCursor *cursor, size_t offset, size_t length,
                                 VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (cursor == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "insert notify needs a cursor");
        return false;
    }
    const size_t total = vellum_buffer_length(cursor->buffer);
    if (length > total || offset > total - length) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT,
                         "insert notification range is out of bounds");
        return false;
    }

    size_t anchor = cursor->anchor;
    size_t head = cursor->head;
    remap_insert(&anchor, offset, length);
    remap_insert(&head, offset, length);

    size_t snapped_anchor = 0u;
    size_t snapped_head = 0u;
    size_t column = 0u;
    if (!snap_offset(cursor, anchor, false, &snapped_anchor, error) ||
        !snap_offset(cursor, head, false, &snapped_head, error) ||
        !column_for(cursor, snapped_head, &column, error)) {
        return false;
    }
    cursor->anchor = snapped_anchor;
    cursor->head = snapped_head;
    cursor->desired_column = column;
    return true;
}

bool vellum_cursor_notify_delete(VellumCursor *cursor, size_t offset, size_t length,
                                 VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (cursor == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "delete notify needs a cursor");
        return false;
    }
    const size_t total = vellum_buffer_length(cursor->buffer);
    if (offset > total) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT,
                         "delete notification range is out of bounds");
        return false;
    }

    size_t anchor = cursor->anchor;
    size_t head = cursor->head;
    remap_delete(&anchor, offset, length);
    remap_delete(&head, offset, length);

    size_t snapped_anchor = 0u;
    size_t snapped_head = 0u;
    size_t column = 0u;
    if (!snap_offset(cursor, anchor, false, &snapped_anchor, error) ||
        !snap_offset(cursor, head, false, &snapped_head, error) ||
        !column_for(cursor, snapped_head, &column, error)) {
        return false;
    }
    cursor->anchor = snapped_anchor;
    cursor->head = snapped_head;
    cursor->desired_column = column;
    return true;
}

bool vellum_cursor_validate(VellumCursor *cursor)
{
    if (cursor == NULL || !vellum_buffer_validate(cursor->buffer) ||
        !vellum_line_index_validate(cursor->lines, cursor->buffer)) {
        return false;
    }
    const size_t total = vellum_buffer_length(cursor->buffer);
    if (cursor->anchor > total || cursor->head > total) {
        return false;
    }

    VellumErrorInfo error;
    size_t line = 0u;
    for (int end = 0; end < 2; ++end) {
        const size_t offset = end == 0 ? cursor->anchor : cursor->head;
        if (!line_of(cursor, offset, &line, &error) || !load_line(cursor, line, &error)) {
            return false;
        }
        const size_t relative = offset - cursor->region_start;
        bool on_boundary = false;
        for (size_t i = 0u; i < cursor->boundary_count; ++i) {
            if (cursor->boundaries[i] == relative) {
                on_boundary = true;
                break;
            }
        }
        if (!on_boundary) {
            return false;
        }
    }
    return true;
}
