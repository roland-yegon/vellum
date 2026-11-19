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
#include "vellum/graphics/line_cache.h"

#ifndef VELLUM_FONT_DIR
#define VELLUM_FONT_DIR "."
#endif

#define CACHE_TEXTURE 42u
#define CACHE_CAPACITY 8u

typedef struct {
    VellumFontSystem *fonts;
    VellumShaper *shaper;
    VellumLineCache *cache;
    VellumDrawList *list;
} CacheFixture;

static bool add_bundled_font(VellumFontSystem *system, const char *name, VellumErrorInfo *error)
{
    char path[512];
    const int written = snprintf(path, sizeof(path), "%s/%s", VELLUM_FONT_DIR, name);
    if (written < 0 || (size_t)written >= sizeof(path)) {
        return false;
    }
    return vellum_font_system_add_face(system, path, error) != VELLUM_FONT_NO_FACE;
}

static bool fixture_init(CacheFixture *fixture, VellumErrorInfo *error)
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
    fixture->cache =
        fixture->shaper == NULL
            ? NULL
            : vellum_line_cache_create(fixture->fonts, fixture->shaper, CACHE_CAPACITY, error);
    return fixture->list != NULL && fixture->cache != NULL;
}

static void fixture_destroy(CacheFixture *fixture)
{
    if (fixture->cache != NULL) {
        vellum_line_cache_destroy(fixture->cache);
    }
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

static VellumTextLayoutParams params(float x, float y, float box_width, VellumColor color)
{
    VellumTextLayoutParams result;
    memset(&result, 0, sizeof(result));
    result.texture = CACHE_TEXTURE;
    result.color = color;
    result.x = x;
    result.baseline_y = y;
    result.box_width = box_width;
    result.auto_direction = true;
    return result;
}

static bool near(float a, float b)
{
    const float delta = a - b;
    return delta >= -0.001f && delta <= 0.001f;
}

static bool quad_eq(const VellumTexturedQuad *a, const VellumTexturedQuad *b)
{
    return a->texture == b->texture && near(a->x, b->x) && near(a->y, b->y) &&
           near(a->width, b->width) && near(a->height, b->height) && near(a->u0, b->u0) &&
           near(a->v0, b->v0) && near(a->u1, b->u1) && near(a->v1, b->v1) &&
           near(a->tint.r, b->tint.r) && near(a->tint.g, b->tint.g) && near(a->tint.b, b->tint.b) &&
           near(a->tint.a, b->tint.a);
}

static const VellumTexturedQuad *quads(VellumDrawList *list, size_t *count)
{
    return vellum_draw_list_quads(list, count);
}

static bool stats_eq(CacheFixture *fixture, uint64_t hits, uint64_t misses, uint64_t evictions,
                     size_t entries)
{
    VellumLineCacheStats stats;
    return vellum_line_cache_stats(fixture->cache, &stats) && stats.hits == hits &&
           stats.misses == misses && stats.evictions == evictions && stats.entries == entries &&
           stats.capacity == CACHE_CAPACITY;
}

static bool hit_reuses_geometry(void)
{
    VellumErrorInfo error;
    CacheFixture fixture = {NULL, NULL, NULL, NULL};
    if (!fixture_init(&fixture, &error)) {
        fixture_destroy(&fixture);
        return false;
    }

    static const char text[] = "const int value = 42;";
    const VellumTextLayoutParams p =
        params(40.0f, 200.0f, 0.0f, vellum_color(0.9f, 0.8f, 0.7f, 1.0f));
    VellumTextLayoutMetrics first = {0u, 0u, 0.0f};
    VellumTextLayoutMetrics second = {0u, 0u, 0.0f};
    bool ok = vellum_line_cache_layout(fixture.cache, fixture.list, &p, text, strlen(text), 14u,
                                       &first, &error);

    size_t first_count = 0u;
    const VellumTexturedQuad *first_quads = quads(fixture.list, &first_count);
    ok = ok && first_count > 0u && first.quad_count == first_count;

    /* Copy the first frame's quads before the second layout appends more. */
    VellumTexturedQuad copy[128];
    if (first_count > sizeof(copy) / sizeof(copy[0])) {
        fixture_destroy(&fixture);
        return false;
    }
    memcpy(copy, first_quads, first_count * sizeof(copy[0]));

    vellum_font_system_atlas_clear_dirty(fixture.fonts);
    vellum_draw_list_clear(fixture.list);
    ok = ok && vellum_line_cache_layout(fixture.cache, fixture.list, &p, text, strlen(text), 14u,
                                        &second, &error);

    size_t second_count = 0u;
    const VellumTexturedQuad *second_quads = quads(fixture.list, &second_count);
    ok = ok && second_count == first_count && second.glyph_count == first.glyph_count &&
         second.quad_count == first.quad_count && near(second.advance, first.advance);
    for (size_t i = 0u; ok && i < first_count; ++i) {
        ok = quad_eq(&copy[i], &second_quads[i]);
    }

    /* A hit performs no rasterization, so the atlas stays clean. */
    ok = ok && !vellum_font_system_atlas_is_dirty(fixture.fonts);
    ok = ok && stats_eq(&fixture, 1u, 1u, 0u, 1u);

    fixture_destroy(&fixture);
    return ok;
}

static bool hit_repositions_and_retints(void)
{
    VellumErrorInfo error;
    CacheFixture fixture = {NULL, NULL, NULL, NULL};
    if (!fixture_init(&fixture, &error)) {
        fixture_destroy(&fixture);
        return false;
    }

    static const char text[] = "hello";
    const VellumColor first_color = vellum_color(1.0f, 0.0f, 0.0f, 1.0f);
    const VellumColor second_color = vellum_color(0.0f, 0.0f, 1.0f, 1.0f);
    const VellumTextLayoutParams first = params(40.0f, 200.0f, 0.0f, first_color);
    const VellumTextLayoutParams second = params(240.0f, 300.0f, 0.0f, second_color);
    bool ok = vellum_line_cache_layout(fixture.cache, fixture.list, &first, text, strlen(text), 14u,
                                       NULL, &error);
    size_t count = 0u;
    const VellumTexturedQuad *initial = quads(fixture.list, &count);
    VellumTexturedQuad copy[128];
    if (!ok || count == 0u || count > sizeof(copy) / sizeof(copy[0])) {
        fixture_destroy(&fixture);
        return false;
    }
    memcpy(copy, initial, count * sizeof(copy[0]));

    vellum_draw_list_clear(fixture.list);
    ok = ok && vellum_line_cache_layout(fixture.cache, fixture.list, &second, text, strlen(text),
                                        14u, NULL, &error);
    size_t second_count = 0u;
    const VellumTexturedQuad *moved = quads(fixture.list, &second_count);
    ok = ok && second_count == count;
    for (size_t i = 0u; ok && i < count; ++i) {
        ok = near(moved[i].x, copy[i].x + 200.0f) && near(moved[i].y, copy[i].y + 100.0f) &&
             near(moved[i].tint.r, 0.0f) && near(moved[i].tint.b, 1.0f) &&
             near(moved[i].u0, copy[i].u0) && near(moved[i].v0, copy[i].v0);
    }
    ok = ok && stats_eq(&fixture, 1u, 1u, 0u, 1u);

    fixture_destroy(&fixture);
    return ok;
}

static bool key_invalidation(void)
{
    VellumErrorInfo error;
    CacheFixture fixture = {NULL, NULL, NULL, NULL};
    if (!fixture_init(&fixture, &error)) {
        fixture_destroy(&fixture);
        return false;
    }

    static const char latin[] = "abcdef";
    static const char arabic[] = "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85"; /* سلام */
    const VellumTextLayoutParams ltr =
        params(0.0f, 100.0f, 0.0f, vellum_color(1.0f, 1.0f, 1.0f, 1.0f));
    bool ok = true;
    VellumTextLayoutParams rtl = ltr;
    rtl.box_width = 300.0f;

    ok = ok && vellum_line_cache_layout(fixture.cache, fixture.list, &ltr, latin, strlen(latin),
                                        14u, NULL, &error);
    vellum_draw_list_clear(fixture.list);
    /* Different pixel size is a different entry and re-rasterizes the atlas. */
    vellum_font_system_atlas_clear_dirty(fixture.fonts);
    ok = ok && vellum_line_cache_layout(fixture.cache, fixture.list, &ltr, latin, strlen(latin),
                                        18u, NULL, &error);
    ok = ok && vellum_font_system_atlas_is_dirty(fixture.fonts);
    vellum_draw_list_clear(fixture.list);
    /* Same text and size is now a hit. */
    ok = ok && vellum_line_cache_layout(fixture.cache, fixture.list, &ltr, latin, strlen(latin),
                                        14u, NULL, &error);
    vellum_draw_list_clear(fixture.list);
    /* Arabic with auto direction and a box width is a distinct RTL entry. */
    ok = ok && vellum_line_cache_layout(fixture.cache, fixture.list, &rtl, arabic, strlen(arabic),
                                        14u, NULL, &error);
    vellum_draw_list_clear(fixture.list);
    /* Forcing LTR on the same bytes is another entry. */
    VellumTextLayoutParams forced = rtl;
    forced.auto_direction = false;
    forced.direction = VELLUM_TEXT_LTR;
    ok = ok && vellum_line_cache_layout(fixture.cache, fixture.list, &forced, arabic,
                                        strlen(arabic), 14u, NULL, &error);
    vellum_draw_list_clear(fixture.list);
    ok = ok && vellum_line_cache_layout(fixture.cache, fixture.list, &ltr, latin, strlen(latin),
                                        14u, NULL, &error);
    ok = ok && stats_eq(&fixture, 2u, 4u, 0u, 4u);

    fixture_destroy(&fixture);
    return ok;
}

static bool eviction_and_clear(void)
{
    VellumErrorInfo error;
    CacheFixture fixture = {NULL, NULL, NULL, NULL};
    if (!fixture_init(&fixture, &error)) {
        fixture_destroy(&fixture);
        return false;
    }

    const VellumTextLayoutParams p =
        params(0.0f, 100.0f, 0.0f, vellum_color(1.0f, 1.0f, 1.0f, 1.0f));
    bool ok = true;
    char text[16];
    for (uint32_t i = 0u; i < 12u; ++i) {
        const int written = snprintf(text, sizeof(text), "line%u", i);
        ok = ok && written > 0;
        ok = ok && vellum_line_cache_layout(fixture.cache, fixture.list, &p, text, (size_t)written,
                                            14u, NULL, &error);
        vellum_draw_list_clear(fixture.list);
    }
    ok = ok && stats_eq(&fixture, 0u, 12u, 4u, CACHE_CAPACITY);
    VellumLineCacheStats stats;
    ok = ok && vellum_line_cache_stats(fixture.cache, &stats) && stats.cached_quads > 0u &&
         stats.cached_bytes > 0u;

    vellum_line_cache_clear(fixture.cache);
    ok = ok && stats_eq(&fixture, 0u, 12u, 4u, 0u);
    ok = ok && vellum_line_cache_stats(fixture.cache, &stats) && stats.cached_quads == 0u &&
         stats.cached_bytes == 0u;
    ok = ok &&
         vellum_line_cache_layout(fixture.cache, fixture.list, &p, "line0", 5u, 14u, NULL, &error);
    ok = ok && stats_eq(&fixture, 0u, 13u, 4u, 1u);

    fixture_destroy(&fixture);
    return ok;
}

static bool invalid_arguments(void)
{
    VellumErrorInfo error;
    CacheFixture fixture = {NULL, NULL, NULL, NULL};
    if (!fixture_init(&fixture, &error)) {
        fixture_destroy(&fixture);
        return false;
    }

    const VellumTextLayoutParams p = params(0.0f, 0.0f, 0.0f, vellum_color(1.0f, 1.0f, 1.0f, 1.0f));
    bool ok = !vellum_line_cache_create(NULL, fixture.shaper, 8u, &error) &&
              error.code == VELLUM_ERR_INVALID_ARGUMENT;
    ok = ok && !vellum_line_cache_create(fixture.fonts, NULL, 8u, &error);
    ok = ok && !vellum_line_cache_layout(NULL, fixture.list, &p, "x", 1u, 14u, NULL, &error) &&
         error.code == VELLUM_ERR_INVALID_ARGUMENT;
    ok = ok && !vellum_line_cache_layout(fixture.cache, NULL, &p, "x", 1u, 14u, NULL, &error);
    ok = ok &&
         !vellum_line_cache_layout(fixture.cache, fixture.list, NULL, "x", 1u, 14u, NULL, &error);
    VellumTextLayoutParams no_texture = p;
    no_texture.texture = 0u;
    ok = ok && !vellum_line_cache_layout(fixture.cache, fixture.list, &no_texture, "x", 1u, 14u,
                                         NULL, &error);
    ok = ok &&
         !vellum_line_cache_layout(fixture.cache, fixture.list, &p, NULL, 1u, 14u, NULL, &error);
    ok = ok && !vellum_line_cache_stats(NULL, NULL);
    VellumLineCacheStats stats;
    ok = ok && vellum_line_cache_stats(fixture.cache, &stats);

    size_t before = 0u;
    (void)quads(fixture.list, &before);
    VellumTextLayoutMetrics metrics = {7u, 7u, 7.0f};
    ok = ok &&
         vellum_line_cache_layout(fixture.cache, fixture.list, &p, "", 0u, 14u, &metrics, &error);
    size_t after = 0u;
    (void)quads(fixture.list, &after);
    ok = ok && after == before && metrics.glyph_count == 0u && metrics.quad_count == 0u;
    ok = ok && stats_eq(&fixture, 0u, 0u, 0u, 0u);

    fixture_destroy(&fixture);
    return ok;
}

bool test_line_cache(void)
{
    return hit_reuses_geometry() && hit_repositions_and_retints() && key_invalidation() &&
           eviction_and_clear() && invalid_arguments();
}
