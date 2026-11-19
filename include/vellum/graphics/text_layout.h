/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#ifndef VELLUM_GRAPHICS_TEXT_LAYOUT_H
#define VELLUM_GRAPHICS_TEXT_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vellum/core/error.h"
#include "vellum/graphics/draw_list.h"
#include "vellum/graphics/font.h"
#include "vellum/graphics/shape.h"
#include "vellum/text/unicode.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    VellumTextureHandle texture;   /* Glyph atlas texture bound by the renderer. */
    VellumColor color;             /* Tint applied to every emitted glyph quad. */
    float x;                       /* Left edge of the text box. */
    float baseline_y;              /* Y of this line's baseline (screen space, y down). */
    float box_width;               /* Right-aligns RTL lines when positive; 0 left-aligns. */
    bool auto_direction;           /* Detect base direction with ICU first-strong rules. */
    VellumTextDirection direction; /* Used when auto_direction is false. */
} VellumTextLayoutParams;

typedef struct {
    size_t glyph_count; /* Shaped glyphs, including whitespace. */
    size_t quad_count;  /* Visible quads actually emitted. */
    float advance;      /* Sum of horizontal advances in pixels (nonnegative). */
} VellumTextLayoutMetrics;

/* Shapes one single-line paragraph and appends its glyph quads to `list`.
 *
 * HarfBuzz 15 returns visual-order glyphs with positive advances for both LTR and
 * RTL, so RTL lines are placed from the right edge of the box leftwards. The
 * per-glyph face recorded by the shaper selects the rasterized atlas entry, which
 * makes per-codepoint font fallback work without caller involvement.
 *
 * Rasterizing may grow the font atlas; callers must upload it before submitting
 * `list` to the renderer. `length` is a byte count and embedded NUL is allowed.
 */
bool vellum_text_layout_line(VellumShaper *shaper, VellumFontSystem *fonts, VellumDrawList *list,
                             const VellumTextLayoutParams *params, const char *text, size_t length,
                             uint32_t pixel_size, VellumTextLayoutMetrics *metrics,
                             VellumErrorInfo *error);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_GRAPHICS_TEXT_LAYOUT_H */
