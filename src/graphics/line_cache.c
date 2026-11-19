/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#include "vellum/graphics/line_cache.h"

#include <stdlib.h>
#include <string.h>

#include "font_internal.h"
#include "vellum/text/unicode.h"

#define VELLUM_LINE_CACHE_MIN_CAPACITY 8u

/* Quad geometry relative to (params.x, params.baseline_y). Absolute position,
 * texture, and tint are reapplied on every layout so cached lines can move. */
typedef struct {
    float dx;
    float dy;
    float width;
    float height;
    float u0;
    float v0;
    float u1;
    float v1;
} CachedQuad;

typedef struct {
    uint64_t key;
    uint64_t stamp;
    bool occupied;
    char *text;
    size_t length;
    uint32_t pixel_size;
    VellumTextDirection direction;
    float box_width;
    size_t face_count;
    CachedQuad *quads;
    size_t quad_count;
    VellumTextLayoutMetrics metrics;
} LineEntry;

struct VellumLineCache {
    VellumFontSystem *fonts;
    VellumShaper *shaper;
    LineEntry *entries;
    size_t capacity;
    size_t mask;
    uint64_t next_stamp;
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
    size_t entry_count;
    size_t cached_quads;
    size_t cached_bytes;
};

static size_t round_capacity(size_t requested)
{
    size_t capacity =
        requested < VELLUM_LINE_CACHE_MIN_CAPACITY ? VELLUM_LINE_CACHE_MIN_CAPACITY : requested;
    /* Round up to a power of two so the key can select a slot with a mask. */
    size_t power = VELLUM_LINE_CACHE_MIN_CAPACITY / 2u;
    while (power < capacity) {
        if (power > SIZE_MAX / 2u) {
            return 0u;
        }
        power *= 2u;
    }
    return power;
}

static uint64_t line_key(const char *text, size_t length, uint32_t pixel_size,
                         VellumTextDirection direction, float box_width, size_t face_count)
{
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (size_t i = 0u; i < length; ++i) {
        hash ^= (uint8_t)text[i];
        hash *= 0x100000001b3ULL;
    }
    uint32_t width_bits = 0u;
    memcpy(&width_bits, &box_width, sizeof(width_bits));
    hash ^= (uint64_t)length * 0x9E3779B1u;
    hash ^= (uint64_t)pixel_size * 0x85EBCA6Bu;
    hash ^= (uint64_t)direction * 0xC2B2AE35u;
    hash ^= (uint64_t)width_bits * 0x27D4EB2Fu;
    hash ^= (uint64_t)face_count * 0x165667B1u;
    return hash;
}

static bool entry_matches(const LineEntry *entry, uint64_t key, const char *text, size_t length,
                          uint32_t pixel_size, VellumTextDirection direction, float box_width,
                          size_t face_count)
{
    return entry->occupied && entry->key == key && entry->length == length &&
           entry->pixel_size == pixel_size && entry->direction == direction &&
           entry->box_width == box_width && entry->face_count == face_count &&
           (length == 0u || memcmp(entry->text, text, length) == 0);
}

/* Returns an exact match, or the best slot to use for a new entry: the first
 * empty slot if any, otherwise the oldest occupied entry (FIFO eviction). */
static LineEntry *cache_slot(VellumLineCache *cache, uint64_t key, const char *text, size_t length,
                             uint32_t pixel_size, VellumTextDirection direction, float box_width,
                             size_t face_count)
{
    LineEntry *empty = NULL;
    LineEntry *oldest = NULL;
    const size_t mask = cache->mask;
    size_t index = (size_t)(key & (uint64_t)mask);
    for (size_t probe = 0u; probe < cache->capacity; ++probe) {
        LineEntry *entry = &cache->entries[index];
        if (!entry->occupied) {
            if (empty == NULL) {
                empty = entry;
            }
        } else {
            if (entry_matches(entry, key, text, length, pixel_size, direction, box_width,
                              face_count)) {
                return entry;
            }
            if (oldest == NULL || entry->stamp < oldest->stamp) {
                oldest = entry;
            }
        }
        index = (index + 1u) & mask;
    }
    return empty != NULL ? empty : oldest;
}

static void free_entry(VellumLineCache *cache, LineEntry *entry)
{
    if (entry->quads != NULL) {
        cache->cached_bytes -= entry->quad_count * sizeof(*entry->quads);
        cache->cached_quads -= entry->quad_count;
    }
    if (entry->text != NULL) {
        cache->cached_bytes -= entry->length;
    }
    free(entry->quads);
    free(entry->text);
    memset(entry, 0, sizeof(*entry));
    --cache->entry_count;
}

VellumLineCache *vellum_line_cache_create(VellumFontSystem *fonts, VellumShaper *shaper,
                                          size_t capacity, VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (fonts == NULL || shaper == NULL) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT,
                         "line cache requires a font system and shaper");
        return NULL;
    }
    const size_t rounded = round_capacity(capacity);
    if (rounded == 0u) {
        vellum_error_set(error, VELLUM_ERR_CAPACITY, "line cache capacity is too large");
        return NULL;
    }

    VellumLineCache *cache = calloc(1u, sizeof(*cache));
    if (cache == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate line cache");
        return NULL;
    }
    cache->entries = calloc(rounded, sizeof(*cache->entries));
    if (cache->entries == NULL) {
        vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not allocate line cache entries");
        free(cache);
        return NULL;
    }
    cache->fonts = fonts;
    cache->shaper = shaper;
    cache->capacity = rounded;
    cache->mask = rounded - 1u;
    return cache;
}

void vellum_line_cache_destroy(VellumLineCache *cache)
{
    if (cache == NULL) {
        return;
    }
    if (cache->entries != NULL) {
        for (size_t i = 0u; i < cache->capacity; ++i) {
            if (cache->entries[i].occupied) {
                free(cache->entries[i].quads);
                free(cache->entries[i].text);
            }
        }
    }
    free(cache->entries);
    free(cache);
}

void vellum_line_cache_clear(VellumLineCache *cache)
{
    if (cache == NULL || cache->entries == NULL) {
        return;
    }
    for (size_t i = 0u; i < cache->capacity; ++i) {
        if (cache->entries[i].occupied) {
            free_entry(cache, &cache->entries[i]);
        }
    }
}

bool vellum_line_cache_stats(const VellumLineCache *cache, VellumLineCacheStats *stats)
{
    if (cache == NULL || stats == NULL) {
        return false;
    }
    stats->hits = cache->hits;
    stats->misses = cache->misses;
    stats->evictions = cache->evictions;
    stats->entries = cache->entry_count;
    stats->capacity = cache->capacity;
    stats->cached_quads = cache->cached_quads;
    stats->cached_bytes = cache->cached_bytes;
    return true;
}

static bool append_cached(const LineEntry *entry, VellumDrawList *list,
                          const VellumTextLayoutParams *params)
{
    for (size_t i = 0u; i < entry->quad_count; ++i) {
        const CachedQuad *quad = &entry->quads[i];
        if (!vellum_draw_list_textured_quad(
                list, params->texture, params->x + quad->dx, params->baseline_y + quad->dy,
                quad->width, quad->height, quad->u0, quad->v0, quad->u1, quad->v1, params->color)) {
            return false;
        }
    }
    return true;
}

static bool store_entry(VellumLineCache *cache, LineEntry *entry, uint64_t key, const char *text,
                        size_t length, uint32_t pixel_size, VellumTextDirection direction,
                        float box_width, size_t face_count, const VellumTexturedQuad *quads,
                        size_t quad_count, float origin_x, float origin_y,
                        const VellumTextLayoutMetrics *metrics, VellumErrorInfo *error)
{
    CachedQuad *stored = NULL;
    if (quad_count > 0u) {
        stored = malloc(quad_count * sizeof(*stored));
        if (stored == NULL) {
            vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not cache line geometry");
            return false;
        }
        for (size_t i = 0u; i < quad_count; ++i) {
            stored[i].dx = quads[i].x - origin_x;
            stored[i].dy = quads[i].y - origin_y;
            stored[i].width = quads[i].width;
            stored[i].height = quads[i].height;
            stored[i].u0 = quads[i].u0;
            stored[i].v0 = quads[i].v0;
            stored[i].u1 = quads[i].u1;
            stored[i].v1 = quads[i].v1;
        }
    }

    char *text_copy = NULL;
    if (length > 0u) {
        text_copy = malloc(length);
        if (text_copy == NULL) {
            free(stored);
            vellum_error_set(error, VELLUM_ERR_ALLOC_FAILED, "could not cache line text");
            return false;
        }
        memcpy(text_copy, text, length);
    }

    entry->key = key;
    entry->stamp = cache->next_stamp++;
    entry->occupied = true;
    entry->text = text_copy;
    entry->length = length;
    entry->pixel_size = pixel_size;
    entry->direction = direction;
    entry->box_width = box_width;
    entry->face_count = face_count;
    entry->quads = stored;
    entry->quad_count = quad_count;
    entry->metrics = *metrics;
    cache->entry_count++;
    cache->cached_quads += quad_count;
    cache->cached_bytes += quad_count * sizeof(*stored) + length;
    return true;
}

bool vellum_line_cache_layout(VellumLineCache *cache, VellumDrawList *list,
                              const VellumTextLayoutParams *params, const char *text, size_t length,
                              uint32_t pixel_size, VellumTextLayoutMetrics *metrics,
                              VellumErrorInfo *error)
{
    vellum_error_init(error);
    if (cache == NULL || list == NULL || params == NULL || text == NULL || params->texture == 0u) {
        vellum_error_set(error, VELLUM_ERR_INVALID_ARGUMENT,
                         "cache, draw list, params, texture, and text are required");
        return false;
    }
    if (metrics != NULL) {
        metrics->glyph_count = 0u;
        metrics->quad_count = 0u;
        metrics->advance = 0.0f;
    }
    if (length == 0u) {
        return vellum_text_layout_line(cache->shaper, cache->fonts, list, params, text, length,
                                       pixel_size, metrics, error);
    }

    VellumTextDirection direction =
        params->auto_direction ? vellum_text_base_direction(text, length) : params->direction;
    if (direction != VELLUM_TEXT_RTL) {
        direction = VELLUM_TEXT_LTR;
    }
    const size_t face_count = vellum_font_system_face_count(cache->fonts);
    const uint64_t key =
        line_key(text, length, pixel_size, direction, params->box_width, face_count);
    LineEntry *entry =
        cache_slot(cache, key, text, length, pixel_size, direction, params->box_width, face_count);
    if (entry == NULL) {
        vellum_error_set(error, VELLUM_ERR_INTERNAL, "line cache has no usable slot");
        return false;
    }

    if (entry->occupied && entry_matches(entry, key, text, length, pixel_size, direction,
                                         params->box_width, face_count)) {
        cache->hits++;
        if (!append_cached(entry, list, params)) {
            return false;
        }
        if (metrics != NULL) {
            *metrics = entry->metrics;
        }
        return true;
    }

    cache->misses++;
    if (entry->occupied) {
        free_entry(cache, entry);
        cache->evictions++;
    }

    size_t before = 0u;
    (void)vellum_draw_list_quads(list, &before);
    VellumTextLayoutMetrics fresh = {0u, 0u, 0.0f};
    if (!vellum_text_layout_line(cache->shaper, cache->fonts, list, params, text, length,
                                 pixel_size, &fresh, error)) {
        return false;
    }

    size_t after = 0u;
    const VellumTexturedQuad *quads = vellum_draw_list_quads(list, &after);
    if (after < before) {
        vellum_error_set(error, VELLUM_ERR_INTERNAL, "draw list quad count decreased");
        return false;
    }
    const size_t added = after - before;
    if (!store_entry(cache, entry, key, text, length, pixel_size, direction, params->box_width,
                     face_count, quads + before, added, params->x, params->baseline_y, &fresh,
                     error)) {
        return false;
    }
    if (metrics != NULL) {
        *metrics = fresh;
    }
    return true;
}
