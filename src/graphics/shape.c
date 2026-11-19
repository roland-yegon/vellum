/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#include "vellum/graphics/shape.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <hb-ft.h>
#include <hb.h>
#include <unicode/utf8.h>

#include "font_internal.h"

#define VELLUM_SHAPE_INITIAL_CACHE 128u
#define VELLUM_HB_FONT_INITIAL_CAPACITY 8u

typedef struct {
    uint32_t face_id;
    uint32_t pixel_size;
    hb_font_t *font;
} HbFontEntry;

typedef struct {
    uint64_t key;
    bool occupied;
    char *text;
    size_t length;
    uint32_t pixel_size;
    VellumTextDirection direction;
    size_t face_count;
    VellumShapedGlyph *glyphs;
    size_t count;
} ShapeCacheEntry;

typedef struct {
    size_t start;
    size_t end;
    uint32_t face_id;
} TextSegment;

typedef struct {
    VellumShapedGlyph *data;
    size_t count;
    size_t capacity;
} GlyphBuffer;

struct VellumShaper {
    VellumFontSystem *fonts;
    HbFontEntry *hb_fonts;
    size_t hb_font_count;
    size_t hb_font_capacity;
    ShapeCacheEntry *cache;
    size_t cache_capacity;
    size_t cache_count;
};

static uint64_t shape_key(const char *text, size_t length, uint32_t pixel_size,
                          VellumTextDirection direction, size_t face_count)
{
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (size_t i = 0u; i < length; ++i) {
        hash ^= (uint8_t)text[i];
        hash *= 0x100000001b3ULL;
    }
    hash ^= (uint64_t)length * 0x9E3779B1u;
    hash ^= (uint64_t)pixel_size * 0x85EBCA6Bu;
    hash ^= (uint64_t)direction * 0xC2B2AE35u;
    hash ^= (uint64_t)face_count * 0x27D4EB2Fu;
    return hash;
}

static bool cache_entry_matches(const ShapeCacheEntry *entry, uint64_t key, const char *text,
                                size_t length, uint32_t pixel_size, VellumTextDirection direction,
                                size_t face_count)
{
    return entry->occupied && entry->key == key && entry->length == length &&
           entry->pixel_size == pixel_size && entry->direction == direction &&
           entry->face_count == face_count &&
           (length == 0u || memcmp(entry->text, text, length) == 0);
}

/* Returns an exact match or the first free slot. The hash alone is never trusted:
 * two different paragraphs with the same digest must not share shaped glyphs. */
static ShapeCacheEntry *cache_lookup(VellumShaper *shaper, uint64_t key, const char *text,
                                     size_t length, uint32_t pixel_size,
                                     VellumTextDirection direction, size_t face_count)
{
    size_t empty = SIZE_MAX;
    size_t mask = shaper->cache_capacity - 1u;
    size_t index = (size_t)(key & (uint64_t)mask);
    for (size_t probe = 0u; probe < shaper->cache_capacity; ++probe) {
        ShapeCacheEntry *entry = &shaper->cache[index];
        if (!entry->occupied) {
            if (empty == SIZE_MAX) {
                empty = index;
            }
        } else if (cache_entry_matches(entry, key, text, length, pixel_size, direction,
                                       face_count)) {
            return entry;
        }
        index = (index + 1u) & mask;
    }
    return empty == SIZE_MAX ? NULL : &shaper->cache[empty];
}

static bool shape_cache_grow(VellumShaper *shaper, VellumErrorInfo *error)
{
    const size_t new_capacity = shaper->cache_capacity * 2u;
    ShapeCacheEntry *new_cache = calloc(new_capacity, sizeof(*new_cache));
    if (new_cache == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not grow shape cache");
        return false;
    }

    ShapeCacheEntry *old_cache = shaper->cache;
    const size_t old_capacity = shaper->cache_capacity;
    shaper->cache = new_cache;
    shaper->cache_capacity = new_capacity;
    const size_t mask = new_capacity - 1u;
    for (size_t i = 0u; i < old_capacity; ++i) {
        if (!old_cache[i].occupied) {
            continue;
        }
        size_t index = (size_t)(old_cache[i].key & (uint64_t)mask);
        while (new_cache[index].occupied) {
            index = (index + 1u) & mask;
        }
        new_cache[index] = old_cache[i];
    }
    free(old_cache);
    return true;
}

static bool glyph_buffer_push(GlyphBuffer *buffer, const VellumShapedGlyph *glyph,
                              VellumErrorInfo *error)
{
    if (buffer->count == buffer->capacity) {
        size_t capacity = buffer->capacity == 0u ? 32u : buffer->capacity * 2u;
        if (capacity < buffer->capacity) {
            vellum_error_set(error, VELLUM_ERR_CAPACITY, "shaped run cannot grow further");
            return false;
        }
        VellumShapedGlyph *data = realloc(buffer->data, capacity * sizeof(*data));
        if (data == NULL) {
            vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not grow shaped run");
            return false;
        }
        buffer->data = data;
        buffer->capacity = capacity;
    }
    buffer->data[buffer->count++] = *glyph;
    return true;
}

static bool segment_push(TextSegment **segments, size_t *count, size_t *capacity, size_t start,
                         size_t end, uint32_t face_id, VellumErrorInfo *error)
{
    if (*count == *capacity) {
        size_t next = *capacity == 0u ? 8u : *capacity * 2u;
        TextSegment *data = realloc(*segments, next * sizeof(*data));
        if (data == NULL) {
            vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not grow font segments");
            return false;
        }
        *segments = data;
        *capacity = next;
    }
    (*segments)[*count].start = start;
    (*segments)[*count].end = end;
    (*segments)[*count].face_id = face_id;
    ++*count;
    return true;
}

static bool build_segments(VellumShaper *shaper, const char *text, size_t length,
                           TextSegment **out_segments, size_t *out_count, VellumErrorInfo *error)
{
    TextSegment *segments = NULL;
    size_t count = 0u;
    size_t capacity = 0u;
    const size_t face_count = vellum_font_system_face_count(shaper->fonts);
    if (face_count == 0u) {
        vellum_error_set(error, VELLUM_ERR_NOT_FOUND, "no font faces have been loaded");
        return false;
    }

    const uint8_t *bytes = (const uint8_t *)text;
    const int32_t limit = (int32_t)length;
    int32_t index = 0;
    size_t segment_start = 0u;
    uint32_t current_face = VELLUM_FONT_NO_FACE;

    while (index < limit) {
        const int32_t codepoint_start = index;
        UChar32 codepoint = U_SENTINEL;
        U8_NEXT(bytes, index, limit, codepoint);
        if (codepoint < 0) {
            vellum_error_set(error, VELLUM_ERR_PARSE, "invalid UTF-8 at byte %d", codepoint_start);
            free(segments);
            return false;
        }

        uint32_t chosen = VELLUM_FONT_NO_FACE;
        for (size_t face = 0u; face < face_count; ++face) {
            const uint32_t face_id = (uint32_t)face + 1u;
            uint32_t glyph_index = 0u;
            if (!vellum_font_system_char_index(shaper->fonts, face_id, (uint32_t)codepoint,
                                               &glyph_index, error)) {
                free(segments);
                return false;
            }
            if (glyph_index != 0u) {
                chosen = face_id;
                break;
            }
        }
        if (chosen == VELLUM_FONT_NO_FACE) {
            chosen = 1u; /* The primary face's .notdef glyph is the last resort. */
        }

        if (current_face == VELLUM_FONT_NO_FACE) {
            current_face = chosen;
            segment_start = (size_t)codepoint_start;
        } else if (chosen != current_face) {
            if (!segment_push(&segments, &count, &capacity, segment_start, (size_t)codepoint_start,
                              current_face, error)) {
                free(segments);
                return false;
            }
            current_face = chosen;
            segment_start = (size_t)codepoint_start;
        }
    }

    if (current_face != VELLUM_FONT_NO_FACE) {
        if (!segment_push(&segments, &count, &capacity, segment_start, length, current_face,
                          error)) {
            free(segments);
            return false;
        }
    }
    *out_segments = segments;
    *out_count = count;
    return true;
}

static hb_font_t *get_hb_font(VellumShaper *shaper, uint32_t face_id, uint32_t pixel_size,
                              VellumErrorInfo *error)
{
    for (size_t i = 0u; i < shaper->hb_font_count; ++i) {
        if (shaper->hb_fonts[i].face_id == face_id &&
            shaper->hb_fonts[i].pixel_size == pixel_size) {
            return shaper->hb_fonts[i].font;
        }
    }

    FT_Face native = vellum_font_native_face(shaper->fonts, face_id);
    if (native == NULL) {
        vellum_error_set(error, VELLUM_ERR_NOT_FOUND, "unknown font face %u", face_id);
        return NULL;
    }
    if (!vellum_font_native_set_pixel_size(native, pixel_size, error)) {
        return NULL;
    }
    hb_font_t *font = hb_ft_font_create(native, NULL);
    if (font == NULL) {
        vellum_error_set(error, VELLUM_ERR_INTERNAL, "hb_ft_font_create failed");
        return NULL;
    }
    const int scale = (int)(pixel_size * 64u);
    hb_font_set_scale(font, scale, scale);

    if (shaper->hb_font_count == shaper->hb_font_capacity) {
        size_t capacity = shaper->hb_font_capacity == 0u ? VELLUM_HB_FONT_INITIAL_CAPACITY
                                                         : shaper->hb_font_capacity * 2u;
        HbFontEntry *entries = realloc(shaper->hb_fonts, capacity * sizeof(*entries));
        if (entries == NULL) {
            hb_font_destroy(font);
            vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not grow HarfBuzz font cache");
            return NULL;
        }
        shaper->hb_fonts = entries;
        shaper->hb_font_capacity = capacity;
    }
    HbFontEntry *entry = &shaper->hb_fonts[shaper->hb_font_count++];
    entry->face_id = face_id;
    entry->pixel_size = pixel_size;
    entry->font = font;
    return font;
}

static bool shape_segment(VellumShaper *shaper, const TextSegment *segment, const char *text,
                          uint32_t pixel_size, VellumTextDirection direction, GlyphBuffer *output,
                          VellumErrorInfo *error)
{
    hb_font_t *font = get_hb_font(shaper, segment->face_id, pixel_size, error);
    if (font == NULL) {
        return false;
    }

    /* The atlas and shaper share each FT_Face. Atlas rasterization may have changed
     * its size since this hb_font was cached, so resync before shaping. */
    FT_Face native = vellum_font_native_face(shaper->fonts, segment->face_id);
    if (native == NULL || !vellum_font_native_set_pixel_size(native, pixel_size, error)) {
        return false;
    }
    hb_ft_font_changed(font);
    const int scale = (int)(pixel_size * 64u);
    hb_font_set_scale(font, scale, scale);

    hb_buffer_t *buffer = hb_buffer_create();
    if (buffer == NULL) {
        vellum_error_set(error, VELLUM_ERR_INTERNAL, "hb_buffer_create failed");
        return false;
    }

    const int segment_length = (int)(segment->end - segment->start);
    hb_buffer_add_utf8(buffer, text + segment->start, segment_length, 0, segment_length);
    hb_buffer_set_direction(buffer,
                            direction == VELLUM_TEXT_RTL ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    hb_buffer_guess_segment_properties(buffer);

    /* JetBrains Mono exposes its programming ligatures through contextual
     * alternates; standard ligatures cover fonts that use the traditional feature. */
    hb_feature_t features[2];
    if (!hb_feature_from_string("calt=1", -1, &features[0]) ||
        !hb_feature_from_string("liga=1", -1, &features[1])) {
        hb_buffer_destroy(buffer);
        vellum_error_set(error, VELLUM_ERR_INTERNAL, "could not parse HarfBuzz features");
        return false;
    }
    hb_shape(font, buffer, features, 2u);

    unsigned int count = 0u;
    const hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buffer, &count);
    const hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buffer, &count);
    bool ok = true;
    for (unsigned int i = 0u; i < count && ok; ++i) {
        VellumShapedGlyph glyph;
        glyph.face_id = segment->face_id;
        glyph.glyph_index = infos[i].codepoint;
        glyph.cluster = infos[i].cluster + (uint32_t)segment->start;
        glyph.x_advance = (float)positions[i].x_advance / 64.0f;
        glyph.y_advance = (float)positions[i].y_advance / 64.0f;
        glyph.x_offset = (float)positions[i].x_offset / 64.0f;
        glyph.y_offset = (float)positions[i].y_offset / 64.0f;
        ok = glyph_buffer_push(output, &glyph, error);
    }
    hb_buffer_destroy(buffer);
    return ok;
}

VellumShaper *vellum_shaper_create(VellumFontSystem *fonts, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (fonts == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "shaper requires a font system");
        return NULL;
    }

    VellumShaper *shaper = calloc(1u, sizeof(*shaper));
    if (shaper == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate shaper");
        return NULL;
    }
    shaper->fonts = fonts;
    shaper->cache = calloc(VELLUM_SHAPE_INITIAL_CACHE, sizeof(*shaper->cache));
    if (shaper->cache == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate shape cache");
        free(shaper);
        return NULL;
    }
    shaper->cache_capacity = VELLUM_SHAPE_INITIAL_CACHE;
    return shaper;
}

void vellum_shaper_destroy(VellumShaper *shaper)
{
    if (shaper == NULL) {
        return;
    }
    for (size_t i = 0u; i < shaper->cache_capacity; ++i) {
        free(shaper->cache[i].text);
        free(shaper->cache[i].glyphs);
    }
    for (size_t i = 0u; i < shaper->hb_font_count; ++i) {
        hb_font_destroy(shaper->hb_fonts[i].font);
    }
    free(shaper->hb_fonts);
    free(shaper->cache);
    free(shaper);
}

const VellumShapedGlyph *vellum_shaper_shape(VellumShaper *shaper, const char *text, size_t length,
                                             uint32_t pixel_size, VellumTextDirection direction,
                                             size_t *glyph_count, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (shaper == NULL || text == NULL || glyph_count == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT,
                         "shaper, text, and count are required");
        return NULL;
    }
    *glyph_count = 0u;
    if (pixel_size == 0u) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT, "pixel size must be positive");
        return NULL;
    }
    if (length > (size_t)INT32_MAX) {
        vellum_error_set(error, VELLUM_ERR_UNSUPPORTED, "shaped text exceeds ICU int32 range");
        return NULL;
    }
    VellumUtf8Error utf8_error;
    if (!vellum_utf8_validate(text, length, &utf8_error)) {
        vellum_error_set(error, VELLUM_ERR_PARSE, "invalid UTF-8 at byte %zu: %s",
                         utf8_error.offset, vellum_utf8_status_name(utf8_error.status));
        return NULL;
    }

    const size_t face_count = vellum_font_system_face_count(shaper->fonts);
    const uint64_t key = shape_key(text, length, pixel_size, direction, face_count);
    ShapeCacheEntry *entry =
        cache_lookup(shaper, key, text, length, pixel_size, direction, face_count);
    if (entry == NULL) {
        vellum_error_set(error, VELLUM_ERR_INTERNAL, "shape cache is full");
        return NULL;
    }
    if (entry->occupied) {
        *glyph_count = entry->count;
        return entry->glyphs;
    }
    if ((shaper->cache_count + 1u) * 10u >= shaper->cache_capacity * 7u) {
        if (!shape_cache_grow(shaper, error)) {
            return NULL;
        }
        entry = cache_lookup(shaper, key, text, length, pixel_size, direction, face_count);
        if (entry == NULL) {
            vellum_error_set(error, VELLUM_ERR_INTERNAL, "shape cache is full");
            return NULL;
        }
    }

    TextSegment *segments = NULL;
    size_t segment_count = 0u;
    if (!build_segments(shaper, text, length, &segments, &segment_count, error)) {
        return NULL;
    }

    GlyphBuffer output = {0u};
    bool ok = true;
    for (size_t i = 0u; i < segment_count && ok; ++i) {
        ok = shape_segment(shaper, &segments[i], text, pixel_size, direction, &output, error);
    }
    free(segments);
    if (!ok) {
        free(output.data);
        return NULL;
    }

    char *text_copy = NULL;
    if (length > 0u) {
        text_copy = malloc(length);
        if (text_copy == NULL) {
            free(output.data);
            vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not cache shaped text");
            return NULL;
        }
        memcpy(text_copy, text, length);
    }

    entry->key = key;
    entry->occupied = true;
    entry->text = text_copy;
    entry->length = length;
    entry->pixel_size = pixel_size;
    entry->direction = direction;
    entry->face_count = face_count;
    entry->glyphs = output.data;
    entry->count = output.count;
    ++shaper->cache_count;
    *glyph_count = output.count;
    return output.data;
}
