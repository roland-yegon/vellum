/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include "vellum/text/scrollbar.h"

#include <math.h>

static float nonneg(float value)
{
    return value > 0.0f ? value : 0.0f;
}

static bool is_vertical(const VellumScrollbar *bar)
{
    return bar->orientation == VELLUM_SCROLLBAR_VERTICAL;
}

static float track_start(const VellumScrollbar *bar)
{
    return is_vertical(bar) ? bar->track.y : bar->track.x;
}

static float track_length(const VellumScrollbar *bar)
{
    return nonneg(is_vertical(bar) ? bar->track.height : bar->track.width);
}

/* Resolves the scroll state along the bar's axis. */
static void axis_state(const VellumScrollbar *bar, float *content, float *view, float *max_scroll,
                       float *offset)
{
    const VellumScroll *scroll = bar->scroll;
    if (is_vertical(bar)) {
        *content = vellum_scroll_content_height(scroll);
        vellum_scroll_view_size(scroll, NULL, view);
        vellum_scroll_max_scroll(scroll, NULL, max_scroll);
        vellum_scroll_position(scroll, NULL, offset);
    } else {
        *content = vellum_scroll_content_width(scroll);
        vellum_scroll_view_size(scroll, view, NULL);
        vellum_scroll_max_scroll(scroll, max_scroll, NULL);
        vellum_scroll_position(scroll, offset, NULL);
    }
}

static VellumScrollbarGeometry compute_geometry(const VellumScrollbar *bar)
{
    VellumScrollbarGeometry out = {0};
    out.track = bar->track;

    float content = 0.0f;
    float view = 0.0f;
    float max_scroll = 0.0f;
    float offset = 0.0f;
    axis_state(bar, &content, &view, &max_scroll, &offset);

    const float start = track_start(bar);
    const float length = track_length(bar);
    float thumb_length = length;
    float thumb_start = start;
    if (max_scroll > 0.0f) {
        /* content > view >= 0 whenever max_scroll is positive, so this is safe. */
        thumb_length = length * view / content;
        const float minimum = nonneg(bar->min_thumb_length);
        if (thumb_length < minimum) {
            thumb_length = minimum;
        }
        if (thumb_length > length) {
            thumb_length = length;
        }
        thumb_start = start + (offset / max_scroll) * (length - thumb_length);
    }

    if (is_vertical(bar)) {
        out.thumb.x = bar->track.x;
        out.thumb.y = thumb_start;
        out.thumb.width = bar->track.width;
        out.thumb.height = thumb_length;
    } else {
        out.thumb.x = thumb_start;
        out.thumb.y = bar->track.y;
        out.thumb.width = thumb_length;
        out.thumb.height = bar->track.height;
    }
    return out;
}

static bool point_in_rect(VellumScrollbarRect rect, float px, float py)
{
    return rect.width > 0.0f && rect.height > 0.0f && px >= rect.x && px < rect.x + rect.width &&
           py >= rect.y && py < rect.y + rect.height;
}

void vellum_scrollbar_init(VellumScrollbar *bar, VellumScroll *scroll,
                           VellumScrollbarOrientation orientation, VellumScrollbarRect track,
                           float min_thumb_length)
{
    if (bar == NULL) {
        return;
    }
    bar->scroll = scroll;
    bar->orientation = orientation;
    bar->track = track;
    /* Clamp negatives, but preserve non-finite values so validate() rejects
     * them instead of silently turning a NaN into a valid zero minimum. */
    bar->min_thumb_length = isnan(min_thumb_length) ? min_thumb_length : nonneg(min_thumb_length);
}

void vellum_scrollbar_geometry(const VellumScrollbar *bar, VellumScrollbarGeometry *out)
{
    if (bar == NULL || out == NULL) {
        return;
    }
    *out = compute_geometry(bar);
}

VellumScrollbarHit vellum_scrollbar_hit_test(const VellumScrollbar *bar, float px, float py)
{
    if (!vellum_scrollbar_validate(bar)) {
        return VELLUM_SCROLLBAR_HIT_NONE;
    }
    const VellumScrollbarGeometry geometry = compute_geometry(bar);
    if (point_in_rect(geometry.thumb, px, py)) {
        return VELLUM_SCROLLBAR_HIT_THUMB;
    }
    if (!point_in_rect(geometry.track, px, py)) {
        return VELLUM_SCROLLBAR_HIT_NONE;
    }
    const float point = is_vertical(bar) ? py : px;
    const float thumb_start = is_vertical(bar) ? geometry.thumb.y : geometry.thumb.x;
    return point < thumb_start ? VELLUM_SCROLLBAR_HIT_BEFORE : VELLUM_SCROLLBAR_HIT_AFTER;
}

void vellum_scrollbar_scroll_to_pointer(const VellumScrollbar *bar, float axis_coordinate,
                                        bool center)
{
    if (bar == NULL || bar->scroll == NULL) {
        return;
    }
    const VellumScrollbarGeometry geometry = compute_geometry(bar);
    float content = 0.0f;
    float view = 0.0f;
    float max_scroll = 0.0f;
    float offset = 0.0f;
    axis_state(bar, &content, &view, &max_scroll, &offset);

    const float start = track_start(bar);
    const float thumb_length = is_vertical(bar) ? geometry.thumb.height : geometry.thumb.width;
    const float travel = track_length(bar) - thumb_length;
    if (max_scroll <= 0.0f || travel <= 0.0f) {
        return;
    }

    float desired = center ? axis_coordinate - thumb_length * 0.5f : axis_coordinate;
    if (desired < start) {
        desired = start;
    }
    if (desired > start + travel) {
        desired = start + travel;
    }
    const float target = ((desired - start) / travel) * max_scroll;
    if (is_vertical(bar)) {
        float x = 0.0f;
        vellum_scroll_position(bar->scroll, &x, NULL);
        vellum_scroll_set(bar->scroll, x, target);
    } else {
        float y = 0.0f;
        vellum_scroll_position(bar->scroll, NULL, &y);
        vellum_scroll_set(bar->scroll, target, y);
    }
}

void vellum_scrollbar_page(const VellumScrollbar *bar, int direction)
{
    if (bar == NULL || bar->scroll == NULL || (direction != -1 && direction != 1)) {
        return;
    }
    float content = 0.0f;
    float view = 0.0f;
    float max_scroll = 0.0f;
    float offset = 0.0f;
    axis_state(bar, &content, &view, &max_scroll, &offset);
    const float target = offset + (float)direction * view;
    if (is_vertical(bar)) {
        float x = 0.0f;
        vellum_scroll_position(bar->scroll, &x, NULL);
        vellum_scroll_set(bar->scroll, x, target);
    } else {
        float y = 0.0f;
        vellum_scroll_position(bar->scroll, NULL, &y);
        vellum_scroll_set(bar->scroll, target, y);
    }
}

bool vellum_scrollbar_validate(const VellumScrollbar *bar)
{
    if (bar == NULL || bar->scroll == NULL) {
        return false;
    }
    if (bar->orientation != VELLUM_SCROLLBAR_VERTICAL &&
        bar->orientation != VELLUM_SCROLLBAR_HORIZONTAL) {
        return false;
    }
    return isfinite(bar->track.x) && isfinite(bar->track.y) && isfinite(bar->track.width) &&
           isfinite(bar->track.height) && bar->track.width >= 0.0f && bar->track.height >= 0.0f &&
           isfinite(bar->min_thumb_length) && bar->min_thumb_length >= 0.0f;
}
