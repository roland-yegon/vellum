/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#include "vellum/graphics/text_layout.h"

static bool layout_args_valid(VellumShaper *shaper, VellumFontSystem *fonts, VellumDrawList *list,
                              const VellumTextLayoutParams *params, const char *text)
{
    return shaper != NULL && fonts != NULL && list != NULL && params != NULL && text != NULL &&
           params->texture != 0u;
}

bool vellum_text_layout_line(VellumShaper *shaper, VellumFontSystem *fonts, VellumDrawList *list,
                             const VellumTextLayoutParams *params, const char *text, size_t length,
                             uint32_t pixel_size, VellumTextLayoutMetrics *metrics,
                             VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (!layout_args_valid(shaper, fonts, list, params, text)) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT,
                         "shaper, fonts, draw list, params, texture, and text are required");
        return false;
    }

    if (metrics != NULL) {
        metrics->glyph_count = 0u;
        metrics->quad_count = 0u;
        metrics->advance = 0.0f;
    }
    if (length == 0u) {
        return true;
    }

    VellumTextDirection direction =
        params->auto_direction ? vellum_text_base_direction(text, length) : params->direction;
    if (direction != VELLUM_TEXT_RTL) {
        direction = VELLUM_TEXT_LTR;
    }

    size_t glyph_count = 0u;
    const VellumShapedGlyph *glyphs =
        vellum_shaper_shape(shaper, text, length, pixel_size, direction, &glyph_count, error);
    if (glyphs == NULL) {
        return false;
    }

    float total_advance = 0.0f;
    for (size_t i = 0u; i < glyph_count; ++i) {
        total_advance += glyphs[i].x_advance;
    }

    /* RTL glyphs arrive in visual order (rightmost first) with positive advances.
     * Placing from the box's right edge and walking left reproduces that order in
     * screen space; LTR simply walks forward from the box's left edge. */
    float pen = params->x;
    if (direction == VELLUM_TEXT_RTL) {
        pen += params->box_width > 0.0f ? params->box_width : total_advance;
    }

    size_t quad_count = 0u;
    for (size_t i = 0u; i < glyph_count; ++i) {
        const VellumShapedGlyph *shaped = &glyphs[i];
        if (direction == VELLUM_TEXT_RTL) {
            pen -= shaped->x_advance;
        }

        const VellumGlyph *raster = vellum_font_system_glyph(
            fonts, shaped->face_id, shaped->glyph_index, pixel_size, error);
        if (raster == NULL) {
            return false;
        }
        if (raster->width > 0.0f && raster->height > 0.0f) {
            const float x = pen + shaped->x_offset + raster->bearing_x;
            const float y = params->baseline_y - shaped->y_offset - raster->bearing_y;
            if (!vellum_draw_list_textured_quad(list, params->texture, x, y, raster->width,
                                                raster->height, raster->u0, raster->v0, raster->u1,
                                                raster->v1, params->color)) {
                return false;
            }
            ++quad_count;
        }

        if (direction == VELLUM_TEXT_LTR) {
            pen += shaped->x_advance;
        }
    }

    if (metrics != NULL) {
        metrics->glyph_count = glyph_count;
        metrics->quad_count = quad_count;
        metrics->advance = total_advance;
    }
    return true;
}
