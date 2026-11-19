/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "tests/unit/tests.h"
#include "vellum/graphics/shape.h"

#ifndef VELLUM_FONT_DIR
#define VELLUM_FONT_DIR "."
#endif

static bool add_bundled_font(VellumFontSystem *system, const char *name, VellumErrorInfo *error)
{
    char path[512];
    const int written = snprintf(path, sizeof(path), "%s/%s", VELLUM_FONT_DIR, name);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        return false;
    }
    return vellum_font_system_add_face(system, path, error) != VELLUM_FONT_NO_FACE;
}

bool test_shape(void)
{
    VellumErrorInfo error;
    VellumFontSystem *fonts = vellum_font_system_create(512u, &error);
    if (fonts == NULL) {
        return false;
    }
    if (!add_bundled_font(fonts, "JetBrainsMono-Variable.ttf", &error) ||
        !add_bundled_font(fonts, "NotoSansSC-Variable.ttf", &error) ||
        !add_bundled_font(fonts, "NotoSansArabic-Variable.ttf", &error)) {
        vellum_font_system_destroy(fonts);
        return false;
    }

    VellumShaper *shaper = vellum_shaper_create(fonts, &error);
    if (shaper == NULL) {
        vellum_font_system_destroy(fonts);
        return false;
    }

    size_t count = 0u;
    const VellumShapedGlyph *glyphs =
        vellum_shaper_shape(shaper, "A", 1u, 24u, VELLUM_TEXT_LTR, &count, &error);
    if (glyphs == NULL || count != 1u || glyphs[0].glyph_index == 0u ||
        glyphs[0].x_advance <= 0.0f || glyphs[0].cluster != 0u) {
        vellum_shaper_destroy(shaper);
        vellum_font_system_destroy(fonts);
        return false;
    }
    const VellumShapedGlyph *cached =
        vellum_shaper_shape(shaper, "A", 1u, 24u, VELLUM_TEXT_LTR, &count, &error);
    if (cached != glyphs) {
        vellum_shaper_destroy(shaper);
        vellum_font_system_destroy(fonts);
        return false;
    }
    const VellumShapedGlyph *other =
        vellum_shaper_shape(shaper, "B", 1u, 24u, VELLUM_TEXT_LTR, &count, &error);
    if (other == NULL || other == glyphs || other[0].glyph_index == glyphs[0].glyph_index ||
        vellum_shaper_shape(shaper, "A", 1u, 24u, VELLUM_TEXT_LTR, &count, &error) != glyphs) {
        vellum_shaper_destroy(shaper);
        vellum_font_system_destroy(fonts);
        return false;
    }

    /* JetBrains Mono implements programming ligatures as a spacer glyph followed
     * by the ligature glyph, not as one collapsed output glyph. Assert that the
     * ligature glyph was selected by comparing with the standalone second char. */
    size_t equals_count = 0u;
    size_t greater_count = 0u;
    const VellumShapedGlyph *equals =
        vellum_shaper_shape(shaper, "=", 1u, 24u, VELLUM_TEXT_LTR, &equals_count, &error);
    const VellumShapedGlyph *greater =
        vellum_shaper_shape(shaper, ">", 1u, 24u, VELLUM_TEXT_LTR, &greater_count, &error);
    if (equals == NULL || equals_count != 1u || greater == NULL || greater_count != 1u) {
        vellum_shaper_destroy(shaper);
        vellum_font_system_destroy(fonts);
        return false;
    }

    glyphs = vellum_shaper_shape(shaper, "!=", 2u, 24u, VELLUM_TEXT_LTR, &count, &error);
    if (glyphs == NULL || count != 2u || glyphs[1].glyph_index == 0u ||
        glyphs[1].glyph_index == equals[0].glyph_index) {
        vellum_shaper_destroy(shaper);
        vellum_font_system_destroy(fonts);
        return false;
    }
    glyphs = vellum_shaper_shape(shaper, "=>", 2u, 24u, VELLUM_TEXT_LTR, &count, &error);
    if (glyphs == NULL || count != 2u || glyphs[1].glyph_index == 0u ||
        glyphs[1].glyph_index == greater[0].glyph_index) {
        vellum_shaper_destroy(shaper);
        vellum_font_system_destroy(fonts);
        return false;
    }

    glyphs = vellum_shaper_shape(shaper, "\xE4\xB8\xAD\xE6\x96\x87", 6u, 24u, VELLUM_TEXT_LTR,
                                 &count, &error);
    if (glyphs == NULL || count != 2u || glyphs[0].glyph_index == 0u ||
        glyphs[1].glyph_index == 0u || glyphs[0].cluster != 0u || glyphs[1].cluster != 3u) {
        vellum_shaper_destroy(shaper);
        vellum_font_system_destroy(fonts);
        return false;
    }

    const char arabic[] = "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7";
    glyphs = vellum_shaper_shape(shaper, arabic, sizeof(arabic) - 1u, 24u, VELLUM_TEXT_RTL, &count,
                                 &error);
    if (glyphs == NULL || count < 2u || glyphs[0].glyph_index == 0u ||
        glyphs[count - 1u].glyph_index == 0u || glyphs[0].cluster <= glyphs[count - 1u].cluster) {
        vellum_shaper_destroy(shaper);
        vellum_font_system_destroy(fonts);
        return false;
    }

    glyphs = vellum_shaper_shape(shaper, "", 0u, 24u, VELLUM_TEXT_LTR, &count, &error);
    if (glyphs != NULL || count != 0u) {
        vellum_shaper_destroy(shaper);
        vellum_font_system_destroy(fonts);
        return false;
    }
    if (vellum_shaper_shape(shaper, "\xFF", 1u, 24u, VELLUM_TEXT_LTR, &count, &error) != NULL ||
        error.code != VELLUM_ERR_PARSE ||
        vellum_shaper_shape(NULL, "A", 1u, 24u, VELLUM_TEXT_LTR, &count, &error) != NULL) {
        vellum_shaper_destroy(shaper);
        vellum_font_system_destroy(fonts);
        return false;
    }

    vellum_shaper_destroy(shaper);
    vellum_shaper_destroy(NULL);

    VellumFontSystem *empty = vellum_font_system_create(128u, &error);
    VellumShaper *empty_shaper = vellum_shaper_create(empty, &error);
    if (empty == NULL || empty_shaper == NULL ||
        vellum_shaper_shape(empty_shaper, "A", 1u, 24u, VELLUM_TEXT_LTR, &count, &error) != NULL ||
        error.code != VELLUM_ERR_NOT_FOUND) {
        vellum_shaper_destroy(empty_shaper);
        vellum_font_system_destroy(empty);
        return false;
    }
    vellum_shaper_destroy(empty_shaper);
    vellum_font_system_destroy(empty);
    vellum_font_system_destroy(fonts);
    return true;
}
