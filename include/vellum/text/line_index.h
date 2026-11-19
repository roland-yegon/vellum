/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#ifndef VELLUM_TEXT_LINE_INDEX_H
#define VELLUM_TEXT_LINE_INDEX_H

#include <stdbool.h>
#include <stddef.h>

#include "vellum/core/error.h"
#include "vellum/text/buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Line terminator style. A line with no terminator is the final line of a buffer
 * that does not end in a newline. Standalone CR is recognised for legacy files;
 * CRLF and LF are the primary formats. */
typedef enum {
    VELLUM_LINE_ENDING_NONE = 0,
    VELLUM_LINE_ENDING_LF,
    VELLUM_LINE_ENDING_CR,
    VELLUM_LINE_ENDING_CRLF
} VellumLineEnding;

/* Maps byte offsets to line starts. A flat sorted array of line-start byte offsets
 * is used today with binary search for offset → line queries; the two-level array
 * described in the architecture is a future optimisation for very large files.
 * Line endings are derived on demand from the buffer, so the index never stores
 * stale terminator metadata after an edit. */
typedef struct VellumLineIndex VellumLineIndex;

VellumLineIndex *vellum_line_index_create(VellumErrorInfo *error);
void vellum_line_index_destroy(VellumLineIndex *index);

/* Rebuilds the index by scanning the whole buffer for line terminators. */
bool vellum_line_index_rebuild(VellumLineIndex *index, const VellumBuffer *buffer,
                               VellumErrorInfo *error);

/* Updates the index after the buffer has been edited. `buffer` is the post-edit
 * state; `offset`/`length` describe the applied insert or delete. */
bool vellum_line_index_notify_insert(VellumLineIndex *index, const VellumBuffer *buffer,
                                     size_t offset, size_t length, VellumErrorInfo *error);
bool vellum_line_index_notify_delete(VellumLineIndex *index, const VellumBuffer *buffer,
                                     size_t offset, size_t length, VellumErrorInfo *error);

/* Always at least 1 (an empty buffer has one empty line). */
size_t vellum_line_index_line_count(const VellumLineIndex *index);

/* Returns SIZE_MAX for an out-of-range line. Line 0 always starts at byte 0. */
size_t vellum_line_index_line_start(const VellumLineIndex *index, size_t line);

/* Finds the line containing `offset`. `line` may be NULL when only success is
 * needed. Offsets at a line boundary belong to the line starting there. */
bool vellum_line_index_line_of(const VellumLineIndex *index, size_t offset, size_t *line);

/* Terminator style of a line, or NONE for the final unterminated line. */
VellumLineEnding vellum_line_index_ending(const VellumLineIndex *index, const VellumBuffer *buffer,
                                          size_t line);

/* Byte offset just past a line's content (before its terminator). Returns SIZE_MAX
 * for an out-of-range line. */
size_t vellum_line_index_content_end(const VellumLineIndex *index, const VellumBuffer *buffer,
                                     size_t line);

/* Most common terminator style; NONE when the buffer has no terminators. */
VellumLineEnding vellum_line_index_dominant_ending(const VellumLineIndex *index,
                                                   const VellumBuffer *buffer);

const char *vellum_line_ending_name(VellumLineEnding ending);

/* Full-scan verification that the cached starts match the buffer. For tests and
 * debugging; returns false for NULL inputs. */
bool vellum_line_index_validate(const VellumLineIndex *index, const VellumBuffer *buffer);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_TEXT_LINE_INDEX_H */
