/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tests/unit/tests.h"
#include "vellum/graphics/draw_list.h"
#include "vellum/graphics/shape.h"
#include "vellum/graphics/text_layout.h"

#ifndef VELLUM_FONT_DIR
#define VELLUM_FONT_DIR "."
#endif

#define LAYOUT_TEXTURE 42u

typedef struct {
    VellumFontSystem *fonts;
    VellumShaper *shaper;
    VellumDrawList *list;
} LayoutFixture;

static bool add_bundled_font(VellumFontSystem *system, const char *name, VellumErrorInfo *error)
{
    char path[512];
    const int written = snprintf(path, sizeof(path), "%s/%s", VELLUM_FONT_DIR, name);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        return false;
    }
    return vellum_font_system_add_face(system, path, error) != VELLUM_FONT_NO_FACE;
}

static bool fixture_init(LayoutFixture *fixture, VellumErrorInfo *error)
{
    fixture->fonts = vellum_font_system_create(512u, error);
    if (fixture->fonts == NULL ||
        !add_bundled_font(fixture->fonts, "JetBrainsMono-Variable.ttf", error) ||
        !add_bundled_font(fixture->fonts, "NotoSansSC-Variable.ttf", error) ||
        !add_bundled_font(fixture->fonts, "NotoSansArabic-Variable.ttf", error)) {
        return false;
    }
    fixture->shaper = vellum_shaper_create(fixture->fonts, error);
    fixture->list = vellum_draw_list_create(16u, 16u);
    return fixture->shaper != NULL && fixture->list != NULL;
}

static void fixture_destroy(LayoutFixture *fixture)
{
    if (fixture->list != NULL) {
        vellum_draw_list_destroy(fixture->list);
    }
    if (fixture->shaper != NULL) {
        vellum_shaper_destroy(fixture->shaper);
    }
    if (fixture->fonts != NULL) {
        vellum_font_system_destroy(fixture->fonts);
    }
}

static VellumTextLayoutParams layout_params(float x, float baseline_y, float box_width,
                                            bool auto_direction, VellumTextDirection direction)
{
    VellumTextLayoutParams params;
    memset(&params, 0, sizeof(params));
    params.texture = LAYOUT_TEXTURE;
    params.color = vellum_color(1.0f, 0.5f, 0.25f, 1.0f);
    params.x = x;
    params.baseline_y = baseline_y;
    params.box_width = box_width;
    params.auto_direction = auto_direction;
    params.direction = direction;
    return params;
}

static const VellumTexturedQuad *layout_quads(LayoutFixture *fixture, size_t *count)
{
    return vellum_draw_list_quads(fixture->list, count);
}

bool test_layout(void)
{
    VellumErrorInfo error;
    LayoutFixture fixture = {NULL, NULL, NULL};
    if (!fixture_init(&fixture, &error)) {
        fixture_destroy(&fixture);
        return false;
    }

    bool ok = false;
    VellumTextLayoutParams params = layout_params(100.0f, 200.0f, 0.0f, false, VELLUM_TEXT_LTR);
    VellumTextLayoutMetrics metrics;
    memset(&metrics, 0, sizeof(metrics));

    /* LTR: five glyphs advance left to right from the box's left edge. */
    if (!vellum_text_layout_line(fixture.shaper, fixture.fonts, fixture.list, &params, "Hello", 5u,
                                 24u, &metrics, &error) ||
        metrics.glyph_count != 5u || metrics.quad_count != 5u || metrics.advance <= 0.0f) {
        goto done;
    }
    size_t quad_count = 0u;
    const VellumTexturedQuad *quads = layout_quads(&fixture, &quad_count);
    if (quad_count != 5u || quads == NULL) {
        goto done;
    }
    for (size_t i = 1u; i < quad_count; ++i) {
        if (quads[i].x <= quads[i - 1u].x) {
            goto done;
        }
    }
    if (quads[0].x < params.x || quads[0].tint.r != params.color.r) {
        goto done;
    }

    /* Whitespace shapes but emits no quad. */
    vellum_draw_list_clear(fixture.list);
    memset(&metrics, 0, sizeof(metrics));
    if (!vellum_text_layout_line(fixture.shaper, fixture.fonts, fixture.list, &params, "a b", 3u,
                                 24u, &metrics, &error) ||
        metrics.glyph_count != 3u || metrics.quad_count != 2u ||
        vellum_draw_list_command_count(fixture.list) != 2u) {
        goto done;
    }

    /* ICU base-direction detection selects RTL for Arabic, and the shaper records
     * the fallback face for each run. The Arabic line is emitted right to left. */
    const char arabic[] = "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7";
    vellum_draw_list_clear(fixture.list);
    memset(&metrics, 0, sizeof(metrics));
    VellumTextLayoutParams rtl = layout_params(100.0f, 200.0f, 300.0f, true, VELLUM_TEXT_LTR);
    if (!vellum_text_layout_line(fixture.shaper, fixture.fonts, fixture.list, &rtl, arabic,
                                 sizeof(arabic) - 1u, 24u, &metrics, &error) ||
        metrics.glyph_count < 2u || metrics.quad_count < 2u || metrics.advance <= 0.0f) {
        goto done;
    }
    quads = layout_quads(&fixture, &quad_count);
    if (quad_count < 2u || quads[0].x <= quads[quad_count - 1u].x ||
        quads[0].x > rtl.x + rtl.box_width) {
        goto done;
    }

    /* Widening the right-aligned box shifts the whole RTL line by the same amount. */
    const float wide_box_x = quads[0].x;
    vellum_draw_list_clear(fixture.list);
    VellumTextLayoutParams rtl_narrow =
        layout_params(100.0f, 200.0f, 200.0f, false, VELLUM_TEXT_RTL);
    if (!vellum_text_layout_line(fixture.shaper, fixture.fonts, fixture.list, &rtl_narrow, arabic,
                                 sizeof(arabic) - 1u, 24u, &metrics, &error)) {
        goto done;
    }
    quads = layout_quads(&fixture, &quad_count);
    if (quad_count < 2u || wide_box_x - quads[0].x < 99.0f || wide_box_x - quads[0].x > 101.0f) {
        goto done;
    }

    /* Mixed Latin/CJK shapes into two faces in one LTR run. */
    const char mixed[] = "A\xE4\xB8\xAD\xE6\x96\x87";
    size_t shaped_count = 0u;
    const VellumShapedGlyph *shaped = vellum_shaper_shape(
        fixture.shaper, mixed, sizeof(mixed) - 1u, 24u, VELLUM_TEXT_LTR, &shaped_count, &error);
    if (shaped == NULL || shaped_count != 3u || shaped[0].face_id != 1u ||
        shaped[1].face_id != 2u || shaped[2].face_id != 2u) {
        goto done;
    }
    vellum_draw_list_clear(fixture.list);
    params.auto_direction = true;
    if (!vellum_text_layout_line(fixture.shaper, fixture.fonts, fixture.list, &params, mixed,
                                 sizeof(mixed) - 1u, 24u, &metrics, &error) ||
        metrics.glyph_count != 3u || metrics.quad_count != 3u) {
        goto done;
    }

    /* Empty input is a successful no-op; invalid input is rejected. */
    memset(&metrics, 0, sizeof(metrics));
    vellum_draw_list_clear(fixture.list);
    if (!vellum_text_layout_line(fixture.shaper, fixture.fonts, fixture.list, &params, "", 0u, 24u,
                                 &metrics, &error) ||
        metrics.glyph_count != 0u || metrics.quad_count != 0u ||
        vellum_draw_list_command_count(fixture.list) != 0u) {
        goto done;
    }
    if (vellum_text_layout_line(NULL, fixture.fonts, fixture.list, &params, "A", 1u, 24u, NULL,
                                &error) ||
        error.code != VELLUM_ERR_INVALID_ARGUMENT ||
        vellum_text_layout_line(fixture.shaper, fixture.fonts, fixture.list, &params, "\xFF", 1u,
                                24u, NULL, &error) ||
        error.code != VELLUM_ERR_PARSE) {
        goto done;
    }

    ok = true;

done:
    fixture_destroy(&fixture);
    return ok;
}
