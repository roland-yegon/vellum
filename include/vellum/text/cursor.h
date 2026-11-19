/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#ifndef VELLUM_TEXT_CURSOR_H
#define VELLUM_TEXT_CURSOR_H

#include <stdbool.h>
#include <stddef.h>

#include "vellum/core/error.h"
#include "vellum/text/buffer.h"
#include "vellum/text/line_index.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Direction for `vellum_cursor_move`. Horizontal and line motions are
 * grapheme-aware; vertical motions use logical lines from the line index and
 * preserve a desired byte column across short lines. */
typedef enum {
    VELLUM_CURSOR_LEFT = 0,
    VELLUM_CURSOR_RIGHT,
    VELLUM_CURSOR_UP,
    VELLUM_CURSOR_DOWN,
    VELLUM_CURSOR_LINE_START,
    VELLUM_CURSOR_LINE_END,
    VELLUM_CURSOR_BUFFER_START,
    VELLUM_CURSOR_BUFFER_END
} VellumCursorMotion;

/* A single caret and optional selection over a buffer and its line index. Both
 * dependencies are borrowed and must outlive the cursor. All offsets are UTF-8
 * byte offsets and always land on extended-grapheme-cluster boundaries.
 *
 * The line index must be built and kept current before using the cursor. After
 * a buffer edit, notify the line index first, then call `vellum_cursor_notify_*`
 * so the caret keeps its position relative to the surrounding text.
 *
 * Lines are logical (line-index) lines; visual/wrapped-line movement belongs to
 * the layout layer that knows the view width. Invalid UTF-8 on a line falls
 * back to byte-wise boundaries so movement still works on mixed-encoding files. */
typedef struct VellumCursor VellumCursor;

VellumCursor *vellum_cursor_create(VellumBuffer *buffer, VellumLineIndex *lines,
                                   VellumErrorInfo *error);
void vellum_cursor_destroy(VellumCursor *cursor);

/* The head is the caret; the anchor is the fixed end of the selection. When
 * they are equal the selection is collapsed. Selection bounds are ordered. */
size_t vellum_cursor_position(const VellumCursor *cursor);
size_t vellum_cursor_anchor(const VellumCursor *cursor);
bool vellum_cursor_has_selection(const VellumCursor *cursor);
size_t vellum_cursor_selection_start(const VellumCursor *cursor);
size_t vellum_cursor_selection_end(const VellumCursor *cursor);

/* Places the caret (and collapses the selection) or sets both ends directly.
 * Offsets are clamped to the buffer and snapped to a grapheme boundary. */
bool vellum_cursor_set(VellumCursor *cursor, size_t offset, VellumErrorInfo *error);
bool vellum_cursor_select(VellumCursor *cursor, size_t anchor, size_t head, VellumErrorInfo *error);
void vellum_cursor_collapse(VellumCursor *cursor);

/* Moves the caret. With `extend` true the anchor stays put and the selection
 * grows or shrinks; otherwise the selection collapses to the new position. */
bool vellum_cursor_move(VellumCursor *cursor, VellumCursorMotion motion, bool extend,
                        VellumErrorInfo *error);

/* Remaps the anchor and head after a buffer edit. The line index must already
 * reflect the edit. Returns false only on internal/ICU failure. */
bool vellum_cursor_notify_insert(VellumCursor *cursor, size_t offset, size_t length,
                                 VellumErrorInfo *error);
bool vellum_cursor_notify_delete(VellumCursor *cursor, size_t offset, size_t length,
                                 VellumErrorInfo *error);

/* Checks the borrowed structures and that both offsets are in range and on
 * grapheme boundaries. May use internal scratch space. */
bool vellum_cursor_validate(VellumCursor *cursor);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_TEXT_CURSOR_H */
