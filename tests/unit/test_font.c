/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tests/unit/tests.h"
#include "vellum/graphics/font.h"

#ifndef VELLUM_FONT_DIR
#define VELLUM_FONT_DIR "."
#endif

static bool add_bundled_font(VellumFontSystem *system, const char *name, uint32_t *face_id,
                             VellumErrorInfo *error)
{
    char path[512];
    const int written = snprintf(path, sizeof(path), "%s/%s", VELLUM_FONT_DIR, name);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        return false;
    }
    *face_id = vellum_font_system_add_face(system, path, error);
    return *face_id != VELLUM_FONT_NO_FACE;
}

static bool has_coverage(VellumFontSystem *system, const VellumGlyph *glyph)
{
    uint32_t page_size = 0u;
    const uint8_t *pixels = vellum_font_system_atlas_pixels(system, &page_size, NULL);
    if (pixels == NULL || glyph->width <= 0.0f || glyph->height <= 0.0f) {
        return false;
    }

    uint32_t x0 = (uint32_t)(glyph->u0 * (float)page_size);
    uint32_t y0 = (uint32_t)(glyph->v0 * (float)page_size);
    uint32_t x1 = (uint32_t)(glyph->u1 * (float)page_size);
    uint32_t y1 = (uint32_t)(glyph->v1 * (float)page_size);
    if (x1 > page_size) {
        x1 = page_size;
    }
    if (y1 > page_size) {
        y1 = page_size;
    }

    for (uint32_t y = y0; y < y1; ++y) {
        for (uint32_t x = x0; x < x1; ++x) {
            if (pixels[((size_t)y * page_size + x) * 4u + 3u] != 0u) {
                return true;
            }
        }
    }
    return false;
}

bool test_font(void)
{
    VellumErrorInfo error;
    if (vellum_font_system_create(100u, &error) != NULL ||
        error.code != VELLUM_ERR_INVALID_ARGUMENT) {
        return false;
    }

    VellumFontSystem *system = vellum_font_system_create(256u, &error);
    if (system == NULL) {
        return false;
    }

    uint32_t mono = VELLUM_FONT_NO_FACE;
    uint32_t cjk = VELLUM_FONT_NO_FACE;
    uint32_t arabic = VELLUM_FONT_NO_FACE;
    if (!add_bundled_font(system, "JetBrainsMono-Variable.ttf", &mono, &error) ||
        !add_bundled_font(system, "NotoSansSC-Variable.ttf", &cjk, &error) ||
        !add_bundled_font(system, "NotoSansArabic-Variable.ttf", &arabic, &error)) {
        vellum_font_system_destroy(system);
        return false;
    }

    const VellumGlyph *glyph = vellum_font_system_glyph_for_char(system, mono, 'A', 24u, &error);
    if (glyph == NULL || glyph->glyph_index == 0u || !has_coverage(system, glyph) ||
        glyph->advance_x <= 0.0f || glyph->u0 < 0.0f || glyph->u1 > 1.0f || glyph->v0 < 0.0f ||
        glyph->v1 > 1.0f || glyph->page_index != 0u) {
        vellum_font_system_destroy(system);
        return false;
    }

    const VellumGlyph *cached = vellum_font_system_glyph_for_char(system, mono, 'A', 24u, &error);
    if (cached != glyph || !vellum_font_system_atlas_is_dirty(system)) {
        vellum_font_system_destroy(system);
        return false;
    }
    vellum_font_system_atlas_clear_dirty(system);
    if (vellum_font_system_atlas_is_dirty(system) ||
        vellum_font_system_glyph_for_char(system, mono, 'A', 24u, &error) != glyph ||
        vellum_font_system_atlas_is_dirty(system)) {
        vellum_font_system_destroy(system);
        return false;
    }

    const VellumGlyph *accented =
        vellum_font_system_glyph_for_char(system, mono, 0x00E9u, 24u, &error);
    const VellumGlyph *han = vellum_font_system_glyph_for_char(system, cjk, 0x4E2Du, 24u, &error);
    const VellumGlyph *alef =
        vellum_font_system_glyph_for_char(system, arabic, 0x0627u, 24u, &error);
    if (accented == NULL || accented->glyph_index == 0u || han == NULL || han->glyph_index == 0u ||
        alef == NULL || alef->glyph_index == 0u) {
        vellum_font_system_destroy(system);
        return false;
    }

    float ascender = 0.0f;
    float descender = 0.0f;
    float line_height = 0.0f;
    if (!vellum_font_system_face_metrics(system, mono, 24u, &ascender, &descender, &line_height,
                                         &error) ||
        ascender <= 0.0f || descender >= 0.0f || line_height <= 0.0f) {
        vellum_font_system_destroy(system);
        return false;
    }

    uint32_t width = 0u;
    uint32_t height = 0u;
    const uint8_t *pixels = vellum_font_system_atlas_pixels(system, &width, &height);
    if (pixels == NULL || width != 256u || height != 256u) {
        vellum_font_system_destroy(system);
        return false;
    }

    if (vellum_font_system_glyph(system, VELLUM_FONT_NO_FACE, 1u, 24u, &error) != NULL ||
        error.code != VELLUM_ERR_NOT_FOUND ||
        vellum_font_system_add_face(system, "/does/not/exist.ttf", &error) != VELLUM_FONT_NO_FACE ||
        error.code != VELLUM_ERR_PLATFORM) {
        vellum_font_system_destroy(system);
        return false;
    }

    vellum_font_system_destroy(system);
    vellum_font_system_destroy(NULL);
    return true;
}
