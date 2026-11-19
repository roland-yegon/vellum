/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#include "vellum/graphics/font.h"

#include "font_internal.h"

#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#define VELLUM_FONT_MAX_FACES 16u
#define VELLUM_FONT_PADDING 1u
#define VELLUM_FONT_INITIAL_CACHE 256u

typedef struct {
    FT_Face face;
} VellumFontFaceEntry;

typedef struct {
    uint32_t face_id;
    uint32_t glyph_index;
    uint32_t pixel_size;
    bool occupied;
    VellumGlyph glyph;
} VellumGlyphCacheEntry;

struct VellumFontSystem {
    FT_Library library;
    VellumFontFaceEntry faces[VELLUM_FONT_MAX_FACES];
    size_t face_count;
    uint32_t page_size;
    uint8_t *atlas;
    uint32_t pen_x;
    uint32_t pen_y;
    uint32_t row_height;
    bool dirty;
    VellumGlyphCacheEntry *cache;
    size_t cache_capacity;
    size_t cache_count;
    uint64_t rasterizations;
};

static bool fail_freetype(VellumErrorInfo *error, const char *operation, FT_Error result)
{
    const char *description = FT_Error_String(result);
    vellum_error_set(error, VELLUM_ERR_PLATFORM, "%s failed: %s (%d)", operation,
                     description == NULL ? "unknown FreeType error" : description, (int)result);
    return false;
}

static bool valid_page_size(uint32_t page_size)
{
    return page_size >= 64u && page_size <= 4096u && (page_size & (page_size - 1u)) == 0u;
}

static uint64_t glyph_hash(uint32_t face_id, uint32_t glyph_index, uint32_t pixel_size)
{
    return ((uint64_t)face_id * 0x9E3779B1u) ^ ((uint64_t)glyph_index * 0x85EBCA6Bu) ^
           ((uint64_t)pixel_size * 0xC2B2AE35u);
}

static VellumGlyphCacheEntry *cache_slot(VellumFontSystem *system, uint32_t face_id,
                                         uint32_t glyph_index, uint32_t pixel_size)
{
    size_t mask = system->cache_capacity - 1u;
    size_t index = (size_t)(glyph_hash(face_id, glyph_index, pixel_size) & (uint64_t)mask);
    for (size_t probe = 0u; probe < system->cache_capacity; ++probe) {
        VellumGlyphCacheEntry *entry = &system->cache[index];
        if (!entry->occupied || (entry->face_id == face_id && entry->glyph_index == glyph_index &&
                                 entry->pixel_size == pixel_size)) {
            return entry;
        }
        index = (index + 1u) & mask;
    }
    return NULL;
}

static bool cache_grow(VellumFontSystem *system, VellumErrorInfo *error)
{
    const size_t new_capacity = system->cache_capacity * 2u;
    if (new_capacity < system->cache_capacity) {
        vellum_error_set(error, VELLUM_ERR_CAPACITY, "glyph cache cannot grow further");
        return false;
    }

    VellumGlyphCacheEntry *new_cache = calloc(new_capacity, sizeof(*new_cache));
    if (new_cache == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not grow glyph cache");
        return false;
    }

    VellumGlyphCacheEntry *old_cache = system->cache;
    const size_t old_capacity = system->cache_capacity;
    system->cache = new_cache;
    system->cache_capacity = new_capacity;
    for (size_t i = 0u; i < old_capacity; ++i) {
        if (!old_cache[i].occupied) {
            continue;
        }
        VellumGlyphCacheEntry *target = cache_slot(
            system, old_cache[i].face_id, old_cache[i].glyph_index, old_cache[i].pixel_size);
        if (target == NULL) {
            system->cache = old_cache;
            system->cache_capacity = old_capacity;
            free(new_cache);
            vellum_error_set(error, VELLUM_ERR_INTERNAL, "could not rehash glyph cache");
            return false;
        }
        *target = old_cache[i];
    }
    free(old_cache);
    return true;
}

static bool face_from_id(VellumFontSystem *system, uint32_t face_id, FT_Face *face,
                         VellumErrorInfo *error)
{
    if (system == NULL || face == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT,
                         "font system and face output required");
        return false;
    }
    if (face_id == VELLUM_FONT_NO_FACE || face_id > system->face_count) {
        vellum_error_set(error, VELLUM_ERR_NOT_FOUND, "unknown font face %u", face_id);
        return false;
    }
    *face = system->faces[face_id - 1u].face;
    return true;
}

VellumFontSystem *vellum_font_system_create(uint32_t page_size, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (!valid_page_size(page_size)) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT,
                         "atlas page size must be a power of two in [64, 4096]");
        return NULL;
    }

    VellumFontSystem *system = calloc(1u, sizeof(*system));
    if (system == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate font system");
        return NULL;
    }

    FT_Error ft_error = FT_Init_FreeType(&system->library);
    if (ft_error != 0) {
        (void)fail_freetype(error, "FT_Init_FreeType", ft_error);
        free(system);
        return NULL;
    }

    system->page_size = page_size;
    system->atlas = calloc((size_t)page_size * (size_t)page_size * 4u, 1u);
    system->cache = calloc(VELLUM_FONT_INITIAL_CACHE, sizeof(*system->cache));
    if (system->atlas == NULL || system->cache == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate glyph atlas");
        vellum_font_system_destroy(system);
        return NULL;
    }
    system->cache_capacity = VELLUM_FONT_INITIAL_CACHE;
    return system;
}

void vellum_font_system_destroy(VellumFontSystem *system)
{
    if (system == NULL) {
        return;
    }
    for (size_t i = 0u; i < system->face_count; ++i) {
        if (system->faces[i].face != NULL) {
            FT_Done_Face(system->faces[i].face);
        }
    }
    free(system->cache);
    free(system->atlas);
    if (system->library != NULL) {
        FT_Done_FreeType(system->library);
    }
    free(system);
}

uint32_t vellum_font_system_add_face(VellumFontSystem *system, const char *path,
                                     VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (system == NULL || path == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "font system and path are required");
        return VELLUM_FONT_NO_FACE;
    }
    if (system->face_count == VELLUM_FONT_MAX_FACES) {
        vellum_error_set(error, VELLUM_ERR_CAPACITY, "font face limit reached");
        return VELLUM_FONT_NO_FACE;
    }

    FT_Face face = NULL;
    const FT_Error ft_error = FT_New_Face(system->library, path, 0, &face);
    if (ft_error != 0) {
        (void)fail_freetype(error, "FT_New_Face", ft_error);
        return VELLUM_FONT_NO_FACE;
    }

    system->faces[system->face_count].face = face;
    ++system->face_count;
    return (uint32_t)system->face_count;
}

bool vellum_font_system_char_index(VellumFontSystem *system, uint32_t face_id, uint32_t codepoint,
                                   uint32_t *glyph_index, VellumErrorInfo *error)
{
    vellum_error_init(error);
    FT_Face face = NULL;
    if (glyph_index == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "glyph index output is required");
        return false;
    }
    if (!face_from_id(system, face_id, &face, error)) {
        return false;
    }

    *glyph_index = (uint32_t)FT_Get_Char_Index(face, (FT_ULong)codepoint);
    return true;
}

static bool set_pixel_size(FT_Face face, uint32_t pixel_size, VellumErrorInfo *error)
{
    const FT_Error ft_error = FT_Set_Pixel_Sizes(face, 0, (FT_UInt)pixel_size);
    if (ft_error != 0) {
        return fail_freetype(error, "FT_Set_Pixel_Sizes", ft_error);
    }
    return true;
}

static bool atlas_reserve(VellumFontSystem *system, uint32_t width, uint32_t height,
                          uint32_t *out_x, uint32_t *out_y, VellumErrorInfo *error)
{
    if (width == 0u || height == 0u) {
        *out_x = 0u;
        *out_y = 0u;
        return true;
    }
    if (width > system->page_size || height > system->page_size) {
        vellum_error_set(error, VELLUM_ERR_CAPACITY, "glyph is larger than the atlas page");
        return false;
    }
    if (system->pen_x + width + VELLUM_FONT_PADDING > system->page_size) {
        system->pen_x = 0u;
        system->pen_y += system->row_height + VELLUM_FONT_PADDING;
        system->row_height = 0u;
    }
    if (system->pen_y + height + VELLUM_FONT_PADDING > system->page_size) {
        vellum_error_set(error, VELLUM_ERR_CAPACITY, "glyph atlas page is full");
        return false;
    }

    *out_x = system->pen_x;
    *out_y = system->pen_y;
    system->pen_x += width + VELLUM_FONT_PADDING;
    if (height > system->row_height) {
        system->row_height = height;
    }
    return true;
}

static void atlas_blit(VellumFontSystem *system, uint32_t x, uint32_t y, const FT_Bitmap *bitmap)
{
    for (uint32_t row = 0u; row < (uint32_t)bitmap->rows; ++row) {
        const uint8_t *source = bitmap->buffer;
        if (bitmap->pitch >= 0) {
            source += (size_t)row * (size_t)bitmap->pitch;
        } else {
            source +=
                (size_t)((uint32_t)bitmap->rows - 1u - row) * (size_t)(-(int64_t)bitmap->pitch);
        }
        uint8_t *target = system->atlas + ((size_t)(y + row) * system->page_size + x) * 4u;
        for (uint32_t column = 0u; column < (uint32_t)bitmap->width; ++column) {
            target[column * 4u + 0u] = 255u;
            target[column * 4u + 1u] = 255u;
            target[column * 4u + 2u] = 255u;
            target[column * 4u + 3u] = source[column];
        }
    }
}

const VellumGlyph *vellum_font_system_glyph(VellumFontSystem *system, uint32_t face_id,
                                            uint32_t glyph_index, uint32_t pixel_size,
                                            VellumErrorInfo *error)
{
    vellum_error_init(error);
    FT_Face face = NULL;
    if (!face_from_id(system, face_id, &face, error)) {
        return NULL;
    }
    if (pixel_size == 0u) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "invalid glyph request");
        return NULL;
    }

    VellumGlyphCacheEntry *entry = cache_slot(system, face_id, glyph_index, pixel_size);
    if (entry == NULL) {
        vellum_error_set(error, VELLUM_ERR_INTERNAL, "glyph cache is full");
        return NULL;
    }
    if (entry->occupied) {
        return &entry->glyph;
    }

    /* Keep the load factor below 70% so linear probing stays short. */
    if ((system->cache_count + 1u) * 10u >= system->cache_capacity * 7u) {
        if (!cache_grow(system, error)) {
            return NULL;
        }
        entry = cache_slot(system, face_id, glyph_index, pixel_size);
        if (entry == NULL) {
            vellum_error_set(error, VELLUM_ERR_INTERNAL, "glyph cache is full");
            return NULL;
        }
    }

    if (!set_pixel_size(face, pixel_size, error)) {
        return NULL;
    }
    FT_Error ft_error = FT_Load_Glyph(face, (FT_UInt)glyph_index, FT_LOAD_DEFAULT);
    if (ft_error != 0) {
        (void)fail_freetype(error, "FT_Load_Glyph", ft_error);
        return NULL;
    }
    ft_error = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
    if (ft_error != 0) {
        (void)fail_freetype(error, "FT_Render_Glyph", ft_error);
        return NULL;
    }

    const FT_Bitmap *bitmap = &face->glyph->bitmap;
    if (bitmap->pixel_mode != FT_PIXEL_MODE_GRAY) {
        vellum_error_set(error, VELLUM_ERR_UNSUPPORTED, "unsupported FreeType bitmap pixel mode %u",
                         (unsigned int)bitmap->pixel_mode);
        return NULL;
    }

    uint32_t atlas_x = 0u;
    uint32_t atlas_y = 0u;
    if (!atlas_reserve(system, (uint32_t)bitmap->width, (uint32_t)bitmap->rows, &atlas_x, &atlas_y,
                       error)) {
        return NULL;
    }
    if (bitmap->rows > 0 && bitmap->width > 0) {
        atlas_blit(system, atlas_x, atlas_y, bitmap);
    }

    VellumGlyph glyph;
    memset(&glyph, 0, sizeof(glyph));
    glyph.glyph_index = glyph_index;
    glyph.page_index = 0u;
    glyph.width = (float)bitmap->width;
    glyph.height = (float)bitmap->rows;
    glyph.bearing_x = (float)face->glyph->bitmap_left;
    glyph.bearing_y = (float)face->glyph->bitmap_top;
    glyph.advance_x = (float)face->glyph->metrics.horiAdvance / 64.0f;
    glyph.advance_y = (float)face->glyph->metrics.vertAdvance / 64.0f;
    if (bitmap->width > 0 && bitmap->rows > 0) {
        const float inverse = 1.0f / (float)system->page_size;
        glyph.u0 = (float)atlas_x * inverse;
        glyph.v0 = (float)atlas_y * inverse;
        glyph.u1 = (float)(atlas_x + (uint32_t)bitmap->width) * inverse;
        glyph.v1 = (float)(atlas_y + (uint32_t)bitmap->rows) * inverse;
    }

    entry->face_id = face_id;
    entry->glyph_index = glyph_index;
    entry->pixel_size = pixel_size;
    entry->occupied = true;
    entry->glyph = glyph;
    ++system->cache_count;
    ++system->rasterizations;
    system->dirty = true;
    return &entry->glyph;
}

const VellumGlyph *vellum_font_system_glyph_for_char(VellumFontSystem *system, uint32_t face_id,
                                                     uint32_t codepoint, uint32_t pixel_size,
                                                     VellumErrorInfo *error)
{
    uint32_t glyph_index = 0u;
    if (!vellum_font_system_char_index(system, face_id, codepoint, &glyph_index, error)) {
        return NULL;
    }
    return vellum_font_system_glyph(system, face_id, glyph_index, pixel_size, error);
}

bool vellum_font_system_face_metrics(VellumFontSystem *system, uint32_t face_id,
                                     uint32_t pixel_size, float *ascender, float *descender,
                                     float *line_height, VellumErrorInfo *error)
{
    vellum_error_init(error);
    FT_Face face = NULL;
    if (!face_from_id(system, face_id, &face, error)) {
        return false;
    }
    if (ascender == NULL || descender == NULL || line_height == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "metric outputs are required");
        return false;
    }
    if (!set_pixel_size(face, pixel_size, error)) {
        return false;
    }

    *ascender = (float)face->size->metrics.ascender / 64.0f;
    *descender = (float)face->size->metrics.descender / 64.0f;
    *line_height = (float)face->size->metrics.height / 64.0f;
    return true;
}

const uint8_t *vellum_font_system_atlas_pixels(const VellumFontSystem *system, uint32_t *width,
                                               uint32_t *height)
{
    if (system == NULL) {
        return NULL;
    }
    if (width != NULL) {
        *width = system->page_size;
    }
    if (height != NULL) {
        *height = system->page_size;
    }
    return system->atlas;
}

bool vellum_font_system_atlas_is_dirty(const VellumFontSystem *system)
{
    return system != NULL && system->dirty;
}

void vellum_font_system_atlas_clear_dirty(VellumFontSystem *system)
{
    if (system != NULL) {
        system->dirty = false;
    }
}

bool vellum_font_system_stats(const VellumFontSystem *system, VellumFontStats *stats)
{
    if (system == NULL || stats == NULL) {
        return false;
    }
    stats->rasterizations = system->rasterizations;
    stats->cached_glyphs = system->cache_count;
    stats->cache_capacity = system->cache_capacity;
    return true;
}

FT_Face vellum_font_native_face(VellumFontSystem *system, uint32_t face_id)
{
    if (system == NULL || face_id == VELLUM_FONT_NO_FACE || face_id > system->face_count) {
        return NULL;
    }
    return system->faces[face_id - 1u].face;
}

size_t vellum_font_system_face_count(const VellumFontSystem *system)
{
    return system == NULL ? 0u : system->face_count;
}

bool vellum_font_native_set_pixel_size(FT_Face face, uint32_t pixel_size, VellumErrorInfo *error)
{
    if (face == NULL || pixel_size == 0u) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT,
                         "native face and positive pixel size are required");
        return false;
    }
    return set_pixel_size(face, pixel_size, error);
}
