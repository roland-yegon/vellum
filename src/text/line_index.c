/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include "vellum/text/line_index.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define VELLUM_INITIAL_LINE_CAPACITY 64u
#define VELLUM_SCAN_CHUNK 4096u

struct VellumLineIndex {
    size_t *starts;
    size_t count;
    size_t capacity;
};

static bool push_start(VellumLineIndex *index, size_t start, VellumErrorInfo *error)
{
    if (index->count > 0u && index->starts[index->count - 1u] == start) {
        return true; /* Consecutive terminators cannot produce equal starts; defensive. */
    }
    if (index->count == index->capacity) {
        size_t capacity = index->capacity == 0u ? VELLUM_INITIAL_LINE_CAPACITY : index->capacity;
        if (capacity > SIZE_MAX / 2u / sizeof(*index->starts)) {
            vellum_error_set(error, VELLUM_ERR_CAPACITY, "line index capacity overflow");
            return false;
        }
        capacity *= 2u;
        size_t *starts = realloc(index->starts, capacity * sizeof(*index->starts));
        if (starts == NULL) {
            vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not grow line starts to %zu",
                             capacity);
            return false;
        }
        index->starts = starts;
        index->capacity = capacity;
    }
    index->starts[index->count++] = start;
    return true;
}

/* Appends line starts for terminators in [from, to). `from` is assumed to be a
 * line start already present in the index (or the caller adds it separately).
 * A CR at the very end of the range is resolved by looking one byte past `to`
 * in the buffer so a CRLF split at the boundary is handled correctly. */
static bool scan_terminators(VellumLineIndex *index, const VellumBuffer *buffer, size_t from,
                             size_t to, VellumErrorInfo *error)
{
    char chunk[VELLUM_SCAN_CHUNK];
    size_t pos = from;
    bool pending_cr = false;

    while (pos < to) {
        const size_t remaining = to - pos;
        const size_t take = remaining < VELLUM_SCAN_CHUNK ? remaining : VELLUM_SCAN_CHUNK;
        if (!vellum_buffer_read(buffer, pos, chunk, take, error)) {
            return false;
        }
        for (size_t i = 0u; i < take; ++i) {
            const char c = chunk[i];
            if (pending_cr) {
                pending_cr = false;
                if (c == '\n') {
                    if (!push_start(index, pos + i + 1u, error)) {
                        return false;
                    }
                    continue;
                }
                if (!push_start(index, pos + i, error)) {
                    return false;
                }
            }
            if (c == '\r') {
                pending_cr = true;
            } else if (c == '\n') {
                if (!push_start(index, pos + i + 1u, error)) {
                    return false;
                }
            }
        }
        pos += take;
    }

    if (pending_cr) {
        size_t start = to;
        const size_t buffer_length = vellum_buffer_length(buffer);
        if (to < buffer_length) {
            char next = '\0';
            if (vellum_buffer_read(buffer, to, &next, 1u, error) && next == '\n') {
                start = to + 1u;
            }
        }
        if (!push_start(index, start, error)) {
            return false;
        }
    }
    return true;
}

VellumLineIndex *vellum_line_index_create(VellumErrorInfo *error)
{
    vellum_error_init(error);
    VellumLineIndex *index = calloc(1u, sizeof(*index));
    if (index == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate line index");
        return NULL;
    }
    return index;
}

void vellum_line_index_destroy(VellumLineIndex *index)
{
    if (index == NULL) {
        return;
    }
    free(index->starts);
    free(index);
}

bool vellum_line_index_rebuild(VellumLineIndex *index, const VellumBuffer *buffer,
                               VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (index == NULL || buffer == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "rebuild requires index and buffer");
        return false;
    }
    index->count = 0u;
    if (!push_start(index, 0u, error)) {
        return false;
    }
    return scan_terminators(index, buffer, 0u, vellum_buffer_length(buffer), error);
}

/* Finds the largest line index whose start is <= offset. */
static size_t line_of_offset(const VellumLineIndex *index, size_t offset)
{
    size_t lo = 0u;
    size_t hi = index->count;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2u;
        if (index->starts[mid] <= offset) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return lo - 1u;
}

/* Scans forward in the buffer from `position` to find the first line start after
 * that position, i.e. the byte following the next terminator at or after it.
 * Returns the buffer length when no further terminator exists. */
static size_t next_line_start(const VellumBuffer *buffer, size_t position)
{
    const size_t length = vellum_buffer_length(buffer);
    if (position >= length) {
        return length;
    }
    char chunk[VELLUM_SCAN_CHUNK];
    size_t pos = position;
    bool pending_cr = false;
    while (pos < length) {
        const size_t take = length - pos < VELLUM_SCAN_CHUNK ? length - pos : VELLUM_SCAN_CHUNK;
        if (!vellum_buffer_read(buffer, pos, chunk, take, NULL)) {
            return length;
        }
        for (size_t i = 0u; i < take; ++i) {
            const char c = chunk[i];
            if (pending_cr) {
                pending_cr = false;
                if (c == '\n') {
                    return pos + i + 1u;
                }
                /* A standalone CR terminates its own line at the current byte. */
                return pos + i;
            }
            if (c == '\r') {
                pending_cr = true;
            } else if (c == '\n') {
                return pos + i + 1u;
            }
        }
        pos += take;
    }
    return length;
}

/* Replaces the line starts covering [rescan_from, boundary) with a fresh scan of
 * [rescan_from, new_boundary) and shifts the remaining tail by `delta`. `first` is
 * the index of `rescan_from` in the old index; `boundary` is the old-coordinate
 * position of the first unaffected line start. */
static bool splice_rescan(VellumLineIndex *index, const VellumBuffer *buffer, size_t first,
                          size_t rescan_from, size_t boundary, size_t new_boundary, ptrdiff_t delta,
                          VellumErrorInfo *error)
{
    /* Determine how many old starts are replaced: everything from `first` up to
     * (but not including) the first start at or beyond `boundary`. */
    size_t last = first;
    while (last + 1u < index->count && index->starts[last + 1u] < boundary) {
        ++last;
    }

    /* Save the unaffected tail before overwriting the array. */
    const size_t tail_count = index->count > last + 1u ? index->count - last - 1u : 0u;
    size_t *tail = NULL;
    if (tail_count > 0u) {
        tail = malloc(tail_count * sizeof(*tail));
        if (tail == NULL) {
            vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not save line index tail");
            return false;
        }
        memcpy(tail, index->starts + last + 1u, tail_count * sizeof(*tail));
    }

    index->count = first;
    if (!push_start(index, rescan_from, error) ||
        !scan_terminators(index, buffer, rescan_from, new_boundary, error)) {
        free(tail);
        return false;
    }
    for (size_t i = 0u; i < tail_count; ++i) {
        size_t shifted = tail[i];
        if (delta < 0) {
            const size_t amount = (size_t)(-delta);
            if (shifted < amount) {
                shifted = 0u;
            } else {
                shifted -= amount;
            }
        } else {
            if (shifted > SIZE_MAX - (size_t)delta) {
                free(tail);
                vellum_error_set(error, VELLUM_ERR_CAPACITY, "line start overflow");
                return false;
            }
            shifted += (size_t)delta;
        }
        if (!push_start(index, shifted, error)) {
            free(tail);
            return false;
        }
    }
    free(tail);
    return true;
}

bool vellum_line_index_notify_insert(VellumLineIndex *index, const VellumBuffer *buffer,
                                     size_t offset, size_t length, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (index == NULL || buffer == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT,
                         "insert notify needs index and buffer");
        return false;
    }
    if (index->count == 0u || length == 0u) {
        return true;
    }
    if (length > (size_t)PTRDIFF_MAX) {
        vellum_error_set(error, VELLUM_ERR_CAPACITY,
                         "insert length exceeds line index offset range");
        return false;
    }

    size_t first = line_of_offset(index, offset);
    size_t rescan_from = index->starts[first];

    /* If the insertion turns a standalone CR before the line into CRLF, the line
     * start at `offset` is no longer valid; back up to the previous line. */
    if (rescan_from == offset && offset > 0u && length > 0u) {
        char inserted_first = '\0';
        char before = '\0';
        if (vellum_buffer_read(buffer, offset, &inserted_first, 1u, NULL) &&
            vellum_buffer_read(buffer, offset - 1u, &before, 1u, NULL) && before == '\r' &&
            inserted_first == '\n') {
            if (first > 0u) {
                --first;
                rescan_from = index->starts[first];
            }
        }
    }

    const size_t new_boundary = next_line_start(buffer, offset + length);
    if (new_boundary == vellum_buffer_length(buffer)) {
        /* No next line: rescan to the end of the buffer. */
        return splice_rescan(index, buffer, first, rescan_from, SIZE_MAX, new_boundary,
                             (ptrdiff_t)length, error);
    }
    const size_t boundary = new_boundary - length;
    return splice_rescan(index, buffer, first, rescan_from, boundary, new_boundary,
                         (ptrdiff_t)length, error);
}

bool vellum_line_index_notify_delete(VellumLineIndex *index, const VellumBuffer *buffer,
                                     size_t offset, size_t length, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (index == NULL || buffer == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT,
                         "delete notify needs index and buffer");
        return false;
    }
    if (index->count == 0u || length == 0u) {
        return true;
    }
    if (length > (size_t)PTRDIFF_MAX) {
        vellum_error_set(error, VELLUM_ERR_CAPACITY,
                         "delete length exceeds line index offset range");
        return false;
    }

    size_t first = line_of_offset(index, offset);
    size_t rescan_from = index->starts[first];

    /* If the deletion makes a CR before the range adjacent to a LF after it,
     * the old line start at offset is no longer valid. */
    if (rescan_from == offset && offset > 0u) {
        char before = '\0';
        char after = '\0';
        const size_t buffer_length = vellum_buffer_length(buffer);
        if (vellum_buffer_read(buffer, offset - 1u, &before, 1u, NULL) && offset < buffer_length &&
            vellum_buffer_read(buffer, offset, &after, 1u, NULL) && before == '\r' &&
            after == '\n') {
            if (first > 0u) {
                --first;
                rescan_from = index->starts[first];
            }
        }
    }

    const size_t new_boundary = next_line_start(buffer, offset);
    if (new_boundary == vellum_buffer_length(buffer)) {
        /* No surviving line after the edit: every remaining start is stale. */
        return splice_rescan(index, buffer, first, rescan_from, SIZE_MAX, new_boundary,
                             -(ptrdiff_t)length, error);
    }
    const size_t boundary = new_boundary + length;
    return splice_rescan(index, buffer, first, rescan_from, boundary, new_boundary,
                         -(ptrdiff_t)length, error);
}

size_t vellum_line_index_line_count(const VellumLineIndex *index)
{
    return index == NULL ? 0u : index->count;
}

size_t vellum_line_index_line_start(const VellumLineIndex *index, size_t line)
{
    if (index == NULL || line >= index->count) {
        return SIZE_MAX;
    }
    return index->starts[line];
}

bool vellum_line_index_line_of(const VellumLineIndex *index, size_t offset, size_t *line)
{
    if (index == NULL || index->count == 0u) {
        return false;
    }
    if (line != NULL) {
        *line = line_of_offset(index, offset);
    }
    return true;
}

VellumLineEnding vellum_line_index_ending(const VellumLineIndex *index, const VellumBuffer *buffer,
                                          size_t line)
{
    if (index == NULL || buffer == NULL || line >= index->count || line + 1u >= index->count) {
        return VELLUM_LINE_ENDING_NONE;
    }
    const size_t next_start = index->starts[line + 1u];
    char bytes[2u] = {'\0', '\0'};
    if (next_start >= 2u &&
        !vellum_buffer_read(buffer, next_start - 2u, bytes, sizeof(bytes), NULL)) {
        return VELLUM_LINE_ENDING_NONE;
    }
    if (next_start == 1u) {
        vellum_buffer_read(buffer, 0u, bytes, 1u, NULL);
        return bytes[0u] == '\n'
                   ? VELLUM_LINE_ENDING_LF
                   : (bytes[0u] == '\r' ? VELLUM_LINE_ENDING_CR : VELLUM_LINE_ENDING_NONE);
    }
    if (bytes[0u] == '\r' && bytes[1u] == '\n') {
        return VELLUM_LINE_ENDING_CRLF;
    }
    if (bytes[1u] == '\n') {
        return VELLUM_LINE_ENDING_LF;
    }
    if (bytes[1u] == '\r') {
        return VELLUM_LINE_ENDING_CR;
    }
    return VELLUM_LINE_ENDING_NONE;
}

size_t vellum_line_index_content_end(const VellumLineIndex *index, const VellumBuffer *buffer,
                                     size_t line)
{
    if (index == NULL || buffer == NULL || line >= index->count) {
        return SIZE_MAX;
    }
    if (line + 1u >= index->count) {
        return vellum_buffer_length(buffer);
    }
    const size_t next_start = index->starts[line + 1u];
    const VellumLineEnding ending = vellum_line_index_ending(index, buffer, line);
    const size_t terminator = ending == VELLUM_LINE_ENDING_CRLF   ? 2u
                              : ending == VELLUM_LINE_ENDING_NONE ? 0u
                                                                  : 1u;
    return next_start - terminator;
}

VellumLineEnding vellum_line_index_dominant_ending(const VellumLineIndex *index,
                                                   const VellumBuffer *buffer)
{
    if (index == NULL || buffer == NULL || index->count < 2u) {
        return VELLUM_LINE_ENDING_NONE;
    }
    size_t lf = 0u;
    size_t cr = 0u;
    size_t crlf = 0u;
    for (size_t line = 0u; line + 1u < index->count; ++line) {
        switch (vellum_line_index_ending(index, buffer, line)) {
        case VELLUM_LINE_ENDING_LF:
            ++lf;
            break;
        case VELLUM_LINE_ENDING_CR:
            ++cr;
            break;
        case VELLUM_LINE_ENDING_CRLF:
            ++crlf;
            break;
        case VELLUM_LINE_ENDING_NONE:
            break;
        }
    }
    if (crlf == 0u && lf == 0u && cr == 0u) {
        return VELLUM_LINE_ENDING_NONE;
    }
    if (crlf >= lf && crlf >= cr) {
        return VELLUM_LINE_ENDING_CRLF;
    }
    return lf >= cr ? VELLUM_LINE_ENDING_LF : VELLUM_LINE_ENDING_CR;
}

const char *vellum_line_ending_name(VellumLineEnding ending)
{
    switch (ending) {
    case VELLUM_LINE_ENDING_NONE:
        return "none";
    case VELLUM_LINE_ENDING_LF:
        return "LF";
    case VELLUM_LINE_ENDING_CR:
        return "CR";
    case VELLUM_LINE_ENDING_CRLF:
        return "CRLF";
    }
    return "unknown";
}

bool vellum_line_index_validate(const VellumLineIndex *index, const VellumBuffer *buffer)
{
    if (index == NULL || buffer == NULL || index->count == 0u || index->starts[0u] != 0u) {
        return false;
    }
    for (size_t i = 1u; i < index->count; ++i) {
        if (index->starts[i] <= index->starts[i - 1u]) {
            return false;
        }
        if (index->starts[i] > vellum_buffer_length(buffer)) {
            return false;
        }
    }
    VellumErrorInfo error;
    VellumLineIndex *fresh = vellum_line_index_create(&error);
    if (fresh == NULL) {
        return false;
    }
    if (!vellum_line_index_rebuild(fresh, buffer, &error)) {
        vellum_line_index_destroy(fresh);
        return false;
    }
    bool ok = fresh->count == index->count;
    for (size_t i = 0u; ok && i < index->count; ++i) {
        ok = fresh->starts[i] == index->starts[i];
    }
    vellum_line_index_destroy(fresh);
    return ok;
}
