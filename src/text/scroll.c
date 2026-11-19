/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */

#include "vellum/text/scroll.h"

#include <stdint.h>
#include <stdlib.h>

struct VellumScroll {
    const VellumBuffer *buffer;
    const VellumLineIndex *lines;
    float line_height;
    float view_width;
    float view_height;
    float content_width;
    float x;
    float y;
};

static void clamp_scroll(VellumScroll *scroll)
{
    float max_x = scroll->content_width - scroll->view_width;
    if (max_x < 0.0f) {
        max_x = 0.0f;
    }
    const float max_y = vellum_scroll_content_height(scroll) - scroll->view_height;

    if (scroll->x < 0.0f) {
        scroll->x = 0.0f;
    } else if (scroll->x > max_x) {
        scroll->x = max_x;
    }
    /* The content height is nonnegative, but max_y is computed separately so
     * a zero view with an empty document still yields a well-clamped offset. */
    const float limited_y = max_y < 0.0f ? 0.0f : max_y;
    if (scroll->y < 0.0f) {
        scroll->y = 0.0f;
    } else if (scroll->y > limited_y) {
        scroll->y = limited_y;
    }
}

VellumScroll *vellum_scroll_create(const VellumBuffer *buffer, const VellumLineIndex *lines,
                                   float line_height, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (buffer == NULL || lines == NULL || !(line_height > 0.0f)) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "invalid scroll parameters");
        return NULL;
    }

    VellumScroll *scroll = malloc(sizeof(*scroll));
    if (scroll == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate scroll state");
        return NULL;
    }
    scroll->buffer = buffer;
    scroll->lines = lines;
    scroll->line_height = line_height;
    scroll->view_width = 0.0f;
    scroll->view_height = 0.0f;
    scroll->content_width = 0.0f;
    scroll->x = 0.0f;
    scroll->y = 0.0f;
    return scroll;
}

void vellum_scroll_destroy(VellumScroll *scroll)
{
    free(scroll);
}

void vellum_scroll_set_view_size(VellumScroll *scroll, float width, float height)
{
    if (scroll == NULL) {
        return;
    }
    scroll->view_width = width > 0.0f ? width : 0.0f;
    scroll->view_height = height > 0.0f ? height : 0.0f;
    clamp_scroll(scroll);
}

void vellum_scroll_view_size(const VellumScroll *scroll, float *width, float *height)
{
    if (scroll == NULL) {
        return;
    }
    if (width != NULL) {
        *width = scroll->view_width;
    }
    if (height != NULL) {
        *height = scroll->view_height;
    }
}

void vellum_scroll_set_line_height(VellumScroll *scroll, float line_height)
{
    if (scroll == NULL || !(line_height > 0.0f)) {
        return;
    }
    scroll->line_height = line_height;
    clamp_scroll(scroll);
}

float vellum_scroll_line_height(const VellumScroll *scroll)
{
    return scroll == NULL ? 0.0f : scroll->line_height;
}

void vellum_scroll_set_content_width(VellumScroll *scroll, float width)
{
    if (scroll == NULL) {
        return;
    }
    scroll->content_width = width > 0.0f ? width : 0.0f;
    clamp_scroll(scroll);
}

float vellum_scroll_content_width(const VellumScroll *scroll)
{
    return scroll == NULL ? 0.0f : scroll->content_width;
}

float vellum_scroll_content_height(const VellumScroll *scroll)
{
    if (scroll == NULL) {
        return 0.0f;
    }
    return (float)vellum_line_index_line_count(scroll->lines) * scroll->line_height;
}

void vellum_scroll_max_scroll(const VellumScroll *scroll, float *max_x, float *max_y)
{
    if (scroll == NULL) {
        return;
    }
    float mx = scroll->content_width - scroll->view_width;
    if (mx < 0.0f) {
        mx = 0.0f;
    }
    float my = vellum_scroll_content_height(scroll) - scroll->view_height;
    if (my < 0.0f) {
        my = 0.0f;
    }
    if (max_x != NULL) {
        *max_x = mx;
    }
    if (max_y != NULL) {
        *max_y = my;
    }
}

void vellum_scroll_position(const VellumScroll *scroll, float *x, float *y)
{
    if (scroll == NULL) {
        return;
    }
    if (x != NULL) {
        *x = scroll->x;
    }
    if (y != NULL) {
        *y = scroll->y;
    }
}

void vellum_scroll_set(VellumScroll *scroll, float x, float y)
{
    if (scroll == NULL) {
        return;
    }
    scroll->x = x;
    scroll->y = y;
    clamp_scroll(scroll);
}

void vellum_scroll_by(VellumScroll *scroll, float dx, float dy)
{
    if (scroll == NULL) {
        return;
    }
    scroll->x += dx;
    scroll->y += dy;
    clamp_scroll(scroll);
}

void vellum_scroll_clamp(VellumScroll *scroll)
{
    if (scroll != NULL) {
        clamp_scroll(scroll);
    }
}

void vellum_scroll_visible_lines(const VellumScroll *scroll, size_t *first, size_t *last)
{
    if (scroll == NULL) {
        return;
    }
    const size_t count = vellum_line_index_line_count(scroll->lines);
    if (count == 0u) {
        if (first != NULL) {
            *first = 0u;
        }
        if (last != NULL) {
            *last = 0u;
        }
        return;
    }

    const float ratio_top = scroll->y / scroll->line_height;
    const float ratio_bottom = (scroll->y + scroll->view_height) / scroll->line_height;
    /* Scroll offsets are clamped nonnegative, so truncation is floor here. */
    size_t f = (size_t)ratio_top;
    size_t l = (size_t)ratio_bottom;
    /* Round the bottom up to the next line, then exclude a line whose top edge
     * sits exactly at the bottom of the view. */
    if ((float)l < ratio_bottom) {
        ++l;
    }
    if (l > f) {
        --l;
    }
    if (f >= count) {
        f = count - 1u;
    }
    if (l >= count) {
        l = count - 1u;
    }
    if (l < f) {
        l = f;
    }
    if (first != NULL) {
        *first = f;
    }
    if (last != NULL) {
        *last = l;
    }
}

void vellum_scroll_visible_byte_range(const VellumScroll *scroll, size_t *start, size_t *end)
{
    if (scroll == NULL) {
        return;
    }
    size_t first = 0u;
    size_t last = 0u;
    vellum_scroll_visible_lines(scroll, &first, &last);

    size_t range_start = vellum_line_index_line_start(scroll->lines, first);
    if (range_start == SIZE_MAX) {
        range_start = 0u;
    }
    size_t range_end = vellum_line_index_content_end(scroll->lines, scroll->buffer, last);
    if (range_end == SIZE_MAX) {
        range_end = vellum_buffer_length(scroll->buffer);
    }
    if (range_end < range_start) {
        range_end = range_start;
    }
    if (start != NULL) {
        *start = range_start;
    }
    if (end != NULL) {
        *end = range_end;
    }
}

bool vellum_scroll_ensure_line_visible(VellumScroll *scroll, size_t line)
{
    if (scroll == NULL || line >= vellum_line_index_line_count(scroll->lines)) {
        return false;
    }
    const float top = (float)line * scroll->line_height;
    const float bottom = top + scroll->line_height;
    const float view_bottom = scroll->y + scroll->view_height;
    if (top < scroll->y) {
        scroll->y = top;
    } else if (bottom > view_bottom) {
        scroll->y = bottom - scroll->view_height;
    }
    clamp_scroll(scroll);
    return true;
}

bool vellum_scroll_ensure_offset_visible(VellumScroll *scroll, size_t offset)
{
    if (scroll == NULL) {
        return false;
    }
    size_t line = 0u;
    if (!vellum_line_index_line_of(scroll->lines, offset, &line)) {
        return false;
    }
    return vellum_scroll_ensure_line_visible(scroll, line);
}

void vellum_scroll_ensure_x_visible(VellumScroll *scroll, float x, float width)
{
    if (scroll == NULL) {
        return;
    }
    if (x < 0.0f) {
        x = 0.0f;
    }
    if (width < 0.0f) {
        width = 0.0f;
    }
    if (width >= scroll->view_width) {
        scroll->x = x;
    } else if (x < scroll->x) {
        scroll->x = x;
    } else if (x + width > scroll->x + scroll->view_width) {
        scroll->x = x + width - scroll->view_width;
    }
    clamp_scroll(scroll);
}

bool vellum_scroll_validate(const VellumScroll *scroll)
{
    if (scroll == NULL || !(scroll->line_height > 0.0f)) {
        return false;
    }
    float max_x = 0.0f;
    float max_y = 0.0f;
    vellum_scroll_max_scroll(scroll, &max_x, &max_y);
    return scroll->x >= 0.0f && scroll->x <= max_x && scroll->y >= 0.0f && scroll->y <= max_y;
}
