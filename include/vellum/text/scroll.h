/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#ifndef VELLUM_TEXT_SCROLL_H
#define VELLUM_TEXT_SCROLL_H

#include <stdbool.h>
#include <stddef.h>

#include "vellum/core/error.h"
#include "vellum/text/buffer.h"
#include "vellum/text/line_index.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A layout-independent scrolling model over a document's line index.
 *
 * The model owns scroll offsets and a view size, and answers which logical
 * lines and byte ranges are visible. Vertical content extent is derived live
 * from `line_count * line_height`; horizontal extent is the caller-supplied
 * content width. The buffer and line index are borrowed and must outlive the
 * scroll state; reading from them live means edits need no explicit scroll
 * invalidation, but callers should `vellum_scroll_clamp()` after an edit that
 * changes the line count or view size.
 *
 * This module deliberately has no dependency on SDL, FreeType, or the layout
 * engine. Widths and line height are supplied by the caller (typically from
 * layout metrics), keeping the text engine usable headlessly.
 *
 * Coordinates are pixels in document space: `x` grows rightwards, `y` grows
 * downwards, and a scroll offset is the document point at the view's top-left
 * corner. The view rect's origin is a UI concern and is not stored here. */
typedef struct VellumScroll VellumScroll;

/* `buffer` and `lines` are borrowed; the line index must be current.
 * `line_height` must be positive. */
VellumScroll *vellum_scroll_create(const VellumBuffer *buffer, const VellumLineIndex *lines,
                                   float line_height, VellumErrorInfo *error);
void vellum_scroll_destroy(VellumScroll *scroll);

/* View size. Non-positive dimensions are clamped to zero. */
void vellum_scroll_set_view_size(VellumScroll *scroll, float width, float height);
void vellum_scroll_view_size(const VellumScroll *scroll, float *width, float *height);

/* Line height and horizontal content extent. Setting either re-clamps the
 * scroll offsets. `content_width` is the widest content line in pixels; 0
 * disables horizontal scrolling. */
void vellum_scroll_set_line_height(VellumScroll *scroll, float line_height);
float vellum_scroll_line_height(const VellumScroll *scroll);
void vellum_scroll_set_content_width(VellumScroll *scroll, float width);
float vellum_scroll_content_width(const VellumScroll *scroll);

/* Derived extents and scroll limits, all nonnegative pixels. */
float vellum_scroll_content_height(const VellumScroll *scroll);
void vellum_scroll_max_scroll(const VellumScroll *scroll, float *max_x, float *max_y);

/* Current scroll offsets. */
void vellum_scroll_position(const VellumScroll *scroll, float *x, float *y);

/* Sets absolute offsets or applies a delta; both clamp to the valid range. */
void vellum_scroll_set(VellumScroll *scroll, float x, float y);
void vellum_scroll_by(VellumScroll *scroll, float dx, float dy);

/* Re-clamps after an edit or a view change. */
void vellum_scroll_clamp(VellumScroll *scroll);

/* Logical lines overlapping the view, as [first, last] inclusive. Always
 * returns at least one line (an empty buffer still has one line). */
void vellum_scroll_visible_lines(const VellumScroll *scroll, size_t *first, size_t *last);

/* Byte range covering the visible lines, [start, end): the start of the first
 * visible line up to the end of the last visible line's content (before its
 * terminator). */
void vellum_scroll_visible_byte_range(const VellumScroll *scroll, size_t *start, size_t *end);

/* Minimal scrolling so a logical line or byte offset is inside the view.
 * Returns false for a NULL scroll or an out-of-range line. */
bool vellum_scroll_ensure_line_visible(VellumScroll *scroll, size_t line);
bool vellum_scroll_ensure_offset_visible(VellumScroll *scroll, size_t offset);

/* Minimal horizontal scrolling so the pixel range [x, x + width] is visible.
 * A range wider than the view is aligned to its left edge. */
void vellum_scroll_ensure_x_visible(VellumScroll *scroll, float x, float width);

/* Diagnostic invariant check: offsets within limits and a positive line height. */
bool vellum_scroll_validate(const VellumScroll *scroll);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_TEXT_SCROLL_H */
