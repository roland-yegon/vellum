/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#ifndef VELLUM_GRAPHICS_FONT_H
#define VELLUM_GRAPHICS_FONT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vellum/core/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Face IDs start at 1; 0 means "no face". */
#define VELLUM_FONT_NO_FACE 0u

typedef struct VellumFontSystem VellumFontSystem;

/* Pixel-space glyph geometry. UVs address the single R8A8 atlas page; RGB is white
 * and coverage is in alpha so the renderer can tint per vertex. */
typedef struct {
    uint32_t glyph_index;
    uint32_t page_index;
    float u0;
    float v0;
    float u1;
    float v1;
    float width;
    float height;
    float bearing_x;
    float bearing_y;
    float advance_x;
    float advance_y;
} VellumGlyph;

/* `page_size` must be a power of two in [64, 4096]. The first implementation has
 * one page and returns VELLUM_ERR_CAPACITY when it fills. */
VellumFontSystem *vellum_font_system_create(uint32_t page_size, VellumErrorInfo *error);
void vellum_font_system_destroy(VellumFontSystem *system);

uint32_t vellum_font_system_add_face(VellumFontSystem *system, const char *path,
                                     VellumErrorInfo *error);

bool vellum_font_system_char_index(VellumFontSystem *system, uint32_t face_id, uint32_t codepoint,
                                   uint32_t *glyph_index, VellumErrorInfo *error);

const VellumGlyph *vellum_font_system_glyph(VellumFontSystem *system, uint32_t face_id,
                                            uint32_t glyph_index, uint32_t pixel_size,
                                            VellumErrorInfo *error);

const VellumGlyph *vellum_font_system_glyph_for_char(VellumFontSystem *system, uint32_t face_id,
                                                     uint32_t codepoint, uint32_t pixel_size,
                                                     VellumErrorInfo *error);

bool vellum_font_system_face_metrics(VellumFontSystem *system, uint32_t face_id,
                                     uint32_t pixel_size, float *ascender, float *descender,
                                     float *line_height, VellumErrorInfo *error);

/* RGBA8888 pixels, row-major. The pointer is owned by the font system. */
const uint8_t *vellum_font_system_atlas_pixels(const VellumFontSystem *system, uint32_t *width,
                                               uint32_t *height);
bool vellum_font_system_atlas_is_dirty(const VellumFontSystem *system);
void vellum_font_system_atlas_clear_dirty(VellumFontSystem *system);

/* Observability for cache and scale behaviour. `rasterizations` counts every glyph
 * ever rendered into the atlas (including re-rasterizations at new pixel sizes);
 * the other fields describe the current cache. Returns false only for NULL args. */
typedef struct {
    uint64_t rasterizations;
    size_t cached_glyphs;
    size_t cache_capacity;
} VellumFontStats;

bool vellum_font_system_stats(const VellumFontSystem *system, VellumFontStats *stats);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_GRAPHICS_FONT_H */
