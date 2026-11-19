/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#ifndef VELLUM_GRAPHICS_SHAPE_H
#define VELLUM_GRAPHICS_SHAPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vellum/core/error.h"
#include "vellum/graphics/font.h"
#include "vellum/text/unicode.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VellumShaper VellumShaper;

typedef struct {
    uint32_t glyph_index;
    uint32_t cluster;
    float x_advance;
    float y_advance;
    float x_offset;
    float y_offset;
} VellumShapedGlyph;

VellumShaper *vellum_shaper_create(VellumFontSystem *fonts, VellumErrorInfo *error);
void vellum_shaper_destroy(VellumShaper *shaper);

/* Shapes one paragraph into visual-order glyphs. The returned array is owned by the
 * cache inside `shaper` and stays valid until the same input is evicted or the shaper
 * is destroyed. `cluster` values are UTF-8 byte offsets into `text`. */
const VellumShapedGlyph *vellum_shaper_shape(VellumShaper *shaper, const char *text, size_t length,
                                             uint32_t pixel_size, VellumTextDirection direction,
                                             size_t *glyph_count, VellumErrorInfo *error);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_GRAPHICS_SHAPE_H */
