/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#ifndef VELLUM_TEXT_SCROLLBAR_H
#define VELLUM_TEXT_SCROLLBAR_H

#include <stdbool.h>
#include <stddef.h>

#include "vellum/text/scroll.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VELLUM_SCROLLBAR_VERTICAL = 0,
    VELLUM_SCROLLBAR_HORIZONTAL
} VellumScrollbarOrientation;

typedef enum {
    VELLUM_SCROLLBAR_HIT_NONE = 0,
    VELLUM_SCROLLBAR_HIT_THUMB,
    VELLUM_SCROLLBAR_HIT_BEFORE, /* track region before the thumb */
    VELLUM_SCROLLBAR_HIT_AFTER   /* track region after the thumb */
} VellumScrollbarHit;

/* A plain screen-space rectangle with no color, kept independent of the
 * graphics module so this primitive stays headless. */
typedef struct {
    float x;
    float y;
    float width;
    float height;
} VellumScrollbarRect;

typedef struct {
    VellumScrollbarRect track;
    VellumScrollbarRect thumb;
} VellumScrollbarGeometry;

/* Pure scrollbar geometry and hit testing over a borrowed VellumScroll. The
 * scroll must outlive the bar. The track is expressed in screen/parent
 * coordinates; a view origin is a UI concern and is not stored here. All
 * geometry is recomputed from the scroll's live state, so edits and view
 * changes need no explicit invalidation. */
typedef struct {
    VellumScroll *scroll;
    VellumScrollbarOrientation orientation;
    VellumScrollbarRect track;
    float min_thumb_length;
} VellumScrollbar;

/* Initializes a bar. A negative `min_thumb_length` is treated as zero; the
 * effective minimum is clamped to the track length during geometry. */
void vellum_scrollbar_init(VellumScrollbar *bar, VellumScroll *scroll,
                           VellumScrollbarOrientation orientation, VellumScrollbarRect track,
                           float min_thumb_length);

/* Computes the track and thumb rectangles from the scroll's current state.
 * Does nothing when `bar` or `out` is NULL. */
void vellum_scrollbar_geometry(const VellumScrollbar *bar, VellumScrollbarGeometry *out);

/* Classifies a screen-space point relative to the bar. Out-of-track points
 * return VELLUM_SCROLLBAR_HIT_NONE. */
VellumScrollbarHit vellum_scrollbar_hit_test(const VellumScrollbar *bar, float px, float py);

/* Moves the borrowed scroll so the thumb center (or its leading edge when
 * `center` is false) sits at `axis_coordinate`, the coordinate along the bar's
 * axis. Does nothing when there is no scrollable range. */
void vellum_scrollbar_scroll_to_pointer(const VellumScrollbar *bar, float axis_coordinate,
                                        bool center);

/* Pages the borrowed scroll by one view along the bar's axis. `direction` is
 * -1 (toward the start) or +1 (toward the end); other values are ignored. */
void vellum_scrollbar_page(const VellumScrollbar *bar, int direction);

/* True when the bar has a scroll, a valid orientation, and finite nonnegative
 * sizes. */
bool vellum_scrollbar_validate(const VellumScrollbar *bar);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_TEXT_SCROLLBAR_H */
