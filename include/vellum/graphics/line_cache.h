/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2026 Roland Yegon
 */
#ifndef VELLUM_GRAPHICS_LINE_CACHE_H
#define VELLUM_GRAPHICS_LINE_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vellum/core/error.h"
#include "vellum/graphics/draw_list.h"
#include "vellum/graphics/font.h"
#include "vellum/graphics/shape.h"
#include "vellum/graphics/text_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Cache of fully laid-out single-line geometry. The shaper already caches shaped
 * runs and the font system caches rasterized glyphs; this cache goes one step
 * further and remembers the final quad positions so a repeated line needs no
 * shaping, rasterization, or heap allocation at all. */
typedef struct VellumLineCache VellumLineCache;

typedef struct {
    uint64_t hits;       /* Layouts served from cached geometry. */
    uint64_t misses;     /* Layouts that shaped/rasterized a new entry. */
    uint64_t evictions;  /* Entries replaced to make room. */
    size_t entries;      /* Occupied entries. */
    size_t capacity;     /* Maximum entries. */
    size_t cached_quads; /* Quads held by all entries. */
    size_t cached_bytes; /* Approximate bytes held by text + quad geometry. */
} VellumLineCacheStats;

/* `capacity` is rounded up to a power of two (minimum 8). The cache owns its
 * entries but borrows `fonts` and `shaper`, which must outlive it. Like the rest
 * of the text pipeline, it is main-thread only. */
VellumLineCache *vellum_line_cache_create(VellumFontSystem *fonts, VellumShaper *shaper,
                                          size_t capacity, VellumErrorInfo *error);
void vellum_line_cache_destroy(VellumLineCache *cache);

/* Lays out one single-line paragraph and appends its quads to `list`. Geometry is
 * identical to vellum_text_layout_line(). Repeated calls with the same text, byte
 * length, pixel size, effective direction, box width, and face set reuse cached
 * quads; `params.x`, `params.baseline_y`, `params.texture`, and `params.color` are
 * applied on every call, so moving or re-tinting a cached line is free.
 *
 * A cache miss may rasterize new glyphs; callers must upload the atlas before
 * submitting `list` to the renderer, exactly as for vellum_text_layout_line(). */
bool vellum_line_cache_layout(VellumLineCache *cache, VellumDrawList *list,
                              const VellumTextLayoutParams *params, const char *text, size_t length,
                              uint32_t pixel_size, VellumTextLayoutMetrics *metrics,
                              VellumErrorInfo *error);

/* Drops all cached geometry (for example after changing the font set). Hit,
 * miss, and eviction counters are lifetime counters and are not reset. */
void vellum_line_cache_clear(VellumLineCache *cache);

/* Returns false only when `cache` or `stats` is NULL. */
bool vellum_line_cache_stats(const VellumLineCache *cache, VellumLineCacheStats *stats);

#ifdef __cplusplus
}
#endif

#endif /* VELLUM_GRAPHICS_LINE_CACHE_H */
